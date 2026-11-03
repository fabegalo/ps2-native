#include "diagnostic_runner.hpp"

#include "qt_support.hpp"

#include <QDir>
#include <QFileInfo>
#include <QThreadPool>
#include <QUrl>

namespace ps2launcher {
int LogModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }
QVariant LogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= count()) return {};
    const auto& [text, channel] = lines_[static_cast<std::size_t>(index.row())];
    if (role == TextRole || role == Qt::DisplayRole) return text;
    if (role == ChannelRole) return channel;
    return {};
}
QHash<int, QByteArray> LogModel::roleNames() const { return {{TextRole, "text"}, {ChannelRole, "channel"}}; }
void LogModel::append(std::vector<std::pair<QString, int>>&& lines) {
    if (lines.empty()) return;
    const int overflow = count() + static_cast<int>(lines.size()) - kMaxLines;
    if (overflow > 0) {
        const int removed = std::min(overflow, count());
        if (removed > 0) {
            beginRemoveRows({}, 0, removed - 1);
            lines_.erase(lines_.begin(), lines_.begin() + removed);
            endRemoveRows();
            dropped_ += removed;
        }
        if (static_cast<int>(lines.size()) > kMaxLines) {
            dropped_ += static_cast<int>(lines.size()) - kMaxLines;
            lines.erase(lines.begin(), lines.end() - kMaxLines);
        }
    }
    beginInsertRows({}, count(), count() + static_cast<int>(lines.size()) - 1);
    for (auto& line : lines) lines_.push_back(std::move(line));
    endInsertRows();
    emit countChanged();
}
void LogModel::clear() {
    beginResetModel();
    lines_.clear();
    dropped_ = 0;
    endResetModel();
    emit countChanged();
}
QString LogModel::text() const {
    QString out;
    if (dropped_ > 0) out += QStringLiteral("# (%1 earlier lines were not kept by the launcher)\n").arg(dropped_);
    for (const auto& [text, channel] : lines_) {
        if (channel == 2) out += QStringLiteral("[stderr] ");
        out += text;
        out += QLatin1Char('\n');
    }
    return out;
}

DiagnosticRunner::DiagnosticRunner(QObject* parent) : QObject(parent) {
    flush_timer_.setInterval(60);
    connect(&flush_timer_, &QTimer::timeout, this, &DiagnosticRunner::flush_pending);
    tick_timer_.setInterval(100);
    connect(&tick_timer_, &QTimer::timeout, this, &DiagnosticRunner::progressChanged);
}
DiagnosticRunner::~DiagnosticRunner() {
    if (process_) {
        process_->disconnect(this);
        process_->kill();
        process_->waitForFinished(3000);
    }
}

bool DiagnosticRunner::start(const QString& tool, const ps2native::frontend::DiagnosticRequest& request,
                             const QString& biosId, const QString& biosTitle, const QString& workingDirectory) {
    if (running()) {
        error_ = tr("A diagnostic is already running.");
        emit stateChanged();
        return false;
    }
    QStringList arguments;
    try {
        for (const auto& argument : ps2native::frontend::diagnostic_arguments(request)) arguments << qstr(argument);
    } catch (const std::exception& error) {
        set_state(QStringLiteral("failed"), qstr(std::string(error.what())));
        return false;
    }
    if (tool.isEmpty() || !QFileInfo(tool).isFile()) {
        set_state(QStringLiteral("failed"),
                  tr("ps2n-boot was not found. Build it with the project, or set its location in Settings."));
        return false;
    }
    log_.clear();
    pending_.clear();
    out_buffer_.clear();
    err_buffer_.clear();
    summary_ = {};
    lines_seen_ = 0;
    cancelled_ = false;
    final_elapsed_ = 0;
    bios_id_ = biosId;
    bios_title_ = biosTitle;
    command_line_ = QDir::toNativeSeparators(tool) + QLatin1Char(' ') + arguments.join(QLatin1Char(' '));
    emit summaryChanged();

    auto* process = new QProcess(this);
    process_ = process;
    process->setProgram(tool);
    process->setArguments(arguments);
    if (!workingDirectory.isEmpty() && QDir(workingDirectory).exists()) process->setWorkingDirectory(workingDirectory);
    connect(process, &QProcess::readyReadStandardOutput, this, [this] { read_channel(QProcess::StandardOutput); });
    connect(process, &QProcess::readyReadStandardError, this, [this] { read_channel(QProcess::StandardError); });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || process != process_) return;
        tick_timer_.stop();
        flush_timer_.stop();
        set_state(QStringLiteral("failed"), tr("ps2n-boot could not start: %1").arg(process->errorString()));
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (process != process_) return;
        read_channel(QProcess::StandardOutput);
        read_channel(QProcess::StandardError);
        take_lines(out_buffer_, 1, true);
        take_lines(err_buffer_, 2, true);
        flush_pending();
        final_elapsed_ = clock_.elapsed();
        tick_timer_.stop();
        flush_timer_.stop();
        process->deleteLater();
        emit summaryChanged();
        emit progressChanged();
        if (cancelled_) {
            set_state(QStringLiteral("cancelled"), tr("The diagnostic was cancelled."));
            emit finished(false);
        } else if (status == QProcess::CrashExit) {
            set_state(QStringLiteral("failed"), tr("ps2n-boot terminated abnormally."));
            emit finished(false);
        } else if (code != 0 || !summary_.finished) {
            const auto reason = summary_.tool_error.empty() ? tr("ps2n-boot exited with code %1.").arg(code)
                                                            : qstr(summary_.tool_error);
            set_state(QStringLiteral("failed"), reason);
            emit finished(false);
        } else {
            set_state(QStringLiteral("finished"));
            emit finished(true);
        }
    });
    clock_.start();
    tick_timer_.start();
    flush_timer_.start();
    pending_.emplace_back(QStringLiteral("$ ") + command_line_, 0);
    set_state(QStringLiteral("running"));
    process->start(QIODevice::ReadOnly);
    return true;
}

void DiagnosticRunner::cancel() {
    if (!running() || !process_) return;
    cancelled_ = true;
    process_->kill();
}
void DiagnosticRunner::clear() {
    if (running()) return;
    log_.clear();
    summary_ = {};
    command_line_.clear();
    lines_seen_ = 0;
    final_elapsed_ = 0;
    set_state(QStringLiteral("idle"));
    emit summaryChanged();
    emit progressChanged();
}

void DiagnosticRunner::saveLog(const QUrl& file) {
    const QString path = local_path(file);
    if (path.isEmpty()) {
        emit saved(false, tr("Choose a local file to save the log."));
        return;
    }
    QString text = log_.text();
    QPointer<DiagnosticRunner> self(this);
    QThreadPool::globalInstance()->start([self, path, text = std::move(text)] {
        QString message;
        bool ok = true;
        try {
            const QByteArray bytes = text.toUtf8();
            ps2native::write_file_atomically(fspath(path), {reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                                            static_cast<std::size_t>(bytes.size())});
            message = QDir::toNativeSeparators(path);
        } catch (const std::exception& error) {
            ok = false;
            message = qstr(std::string(error.what()));
        }
        QMetaObject::invokeMethod(
            QThreadPool::globalInstance(),
            [self, ok, message] {
                if (self) emit self->saved(ok, message);
            },
            Qt::QueuedConnection);
    });
}

qint64 DiagnosticRunner::elapsedMs() const { return running() ? clock_.elapsed() : final_elapsed_; }

QVariantMap DiagnosticRunner::summary() const {
    QVariantMap map;
    map.insert(QStringLiteral("finished"), summary_.finished);
    map.insert(QStringLiteral("outcome"), qstr(summary_.outcome));
    map.insert(QStringLiteral("blockPc"), qstr(summary_.block_pc));
    map.insert(QStringLiteral("blockCategory"), qstr(summary_.block_category));
    map.insert(QStringLiteral("blockReason"), qstr(summary_.block_reason));
    map.insert(QStringLiteral("retired"), static_cast<double>(summary_.retired));
    map.insert(QStringLiteral("romver"), qstr(summary_.romver));
    map.insert(QStringLiteral("console"), qstr(summary_.console));
    map.insert(QStringLiteral("engine"), qstr(summary_.engine));
    map.insert(QStringLiteral("iop"), qstr(summary_.iop));
    map.insert(QStringLiteral("sio"), qlist(summary_.sio_lines));
    map.insert(QStringLiteral("toolError"), qstr(summary_.tool_error));
    return map;
}

void DiagnosticRunner::read_channel(QProcess::ProcessChannel channel) {
    if (!process_) return;
    if (channel == QProcess::StandardOutput) {
        out_buffer_ += process_->readAllStandardOutput();
        take_lines(out_buffer_, 1, false);
    } else {
        err_buffer_ += process_->readAllStandardError();
        take_lines(err_buffer_, 2, false);
    }
}
void DiagnosticRunner::take_lines(QByteArray& buffer, int channel, bool flush_partial) {
    qsizetype start = 0;
    bool summary_touched = false;
    for (;;) {
        const auto newline = buffer.indexOf('\n', start);
        if (newline < 0) break;
        QByteArray line = buffer.mid(start, newline - start);
        if (line.endsWith('\r')) line.chop(1);
        ps2native::frontend::parse_diagnostic_line(summary_, std::string_view(line.constData(), static_cast<std::size_t>(line.size())));
        summary_touched = summary_touched || line.startsWith("# ") || line.startsWith("ps2n-boot:");
        pending_.emplace_back(QString::fromUtf8(line), channel);
        ++lines_seen_;
        start = newline + 1;
    }
    buffer.remove(0, start);
    if (flush_partial && !buffer.isEmpty()) {
        ps2native::frontend::parse_diagnostic_line(summary_, std::string_view(buffer.constData(), static_cast<std::size_t>(buffer.size())));
        pending_.emplace_back(QString::fromUtf8(buffer), channel);
        ++lines_seen_;
        buffer.clear();
        summary_touched = true;
    }
    if (summary_touched) emit summaryChanged();
}
void DiagnosticRunner::flush_pending() {
    if (pending_.empty()) return;
    log_.append(std::move(pending_));
    pending_.clear();
}
void DiagnosticRunner::set_state(const QString& state, const QString& error) {
    state_ = state;
    error_ = error;
    emit stateChanged();
}
} // namespace ps2launcher
