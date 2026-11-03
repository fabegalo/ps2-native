#pragma once
// Runs the existing ps2n-boot tool as a child process (a crash or a long run
// never blocks or takes down the window), streams its output into a log model
// and summarizes the result with the core's parser of that output.
#include "ps2native/frontend/runtime.hpp"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <vector>

namespace ps2launcher {
class LogModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role { TextRole = Qt::UserRole + 1, ChannelRole };
    explicit LogModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(lines_.size()); }
    void append(std::vector<std::pair<QString, int>>&& lines);
    void clear();
    [[nodiscard]] QString text() const;
    [[nodiscard]] int dropped() const { return dropped_; }

signals:
    void countChanged();

private:
    static constexpr int kMaxLines = 100000; // the oldest lines are dropped beyond this
    std::vector<std::pair<QString, int>> lines_; // text, channel (0 info, 1 stdout, 2 stderr)
    int dropped_{};
};

class DiagnosticRunner : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged) // idle | running | finished | failed | cancelled
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QString biosId READ biosId NOTIFY stateChanged)
    Q_PROPERTY(QString biosTitle READ biosTitle NOTIFY stateChanged)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY progressChanged)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(QString commandLine READ commandLine NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)
    Q_PROPERTY(LogModel* log READ log CONSTANT)
public:
    explicit DiagnosticRunner(QObject* parent = nullptr);
    ~DiagnosticRunner() override;

    // Starts ps2n-boot; returns false (with errorText) when it cannot start.
    bool start(const QString& tool, const ps2native::frontend::DiagnosticRequest& request, const QString& biosId,
               const QString& biosTitle, const QString& workingDirectory);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
    // Writes the captured output (asynchronously); the result is reported through saved().
    Q_INVOKABLE void saveLog(const QUrl& file);

    [[nodiscard]] QString state() const { return state_; }
    [[nodiscard]] bool running() const { return state_ == QLatin1String("running"); }
    [[nodiscard]] QString biosId() const { return bios_id_; }
    [[nodiscard]] QString biosTitle() const { return bios_title_; }
    [[nodiscard]] qint64 elapsedMs() const;
    [[nodiscard]] int lineCount() const { return lines_seen_; }
    [[nodiscard]] QVariantMap summary() const;
    [[nodiscard]] QString commandLine() const { return command_line_; }
    [[nodiscard]] QString errorText() const { return error_; }
    [[nodiscard]] LogModel* log() { return &log_; }

signals:
    void stateChanged();
    void progressChanged();
    void summaryChanged();
    void finished(bool success);
    void saved(bool ok, const QString& message);

private:
    void read_channel(QProcess::ProcessChannel channel);
    void take_lines(QByteArray& buffer, int channel, bool flush_partial);
    void flush_pending();
    void set_state(const QString& state, const QString& error = {});

    QPointer<QProcess> process_;
    LogModel log_;
    QTimer flush_timer_;
    QTimer tick_timer_;
    QElapsedTimer clock_;
    qint64 final_elapsed_{};
    QByteArray out_buffer_;
    QByteArray err_buffer_;
    std::vector<std::pair<QString, int>> pending_;
    ps2native::frontend::DiagnosticSummary summary_;
    QString state_{QStringLiteral("idle")};
    QString bios_id_;
    QString bios_title_;
    QString command_line_;
    QString error_;
    int lines_seen_{};
    bool cancelled_{};
};
} // namespace ps2launcher
