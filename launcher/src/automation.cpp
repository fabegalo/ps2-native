#include "automation.hpp"

#include "launcher.hpp"
#include "qt_support.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>

namespace ps2launcher {
namespace {
std::atomic<int> qml_warnings{0};

void automation_message(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const char* name = "debug";
    switch (type) {
    case QtInfoMsg: name = "info"; break;
    case QtWarningMsg: name = "warning"; break;
    case QtCriticalMsg: name = "critical"; break;
    case QtFatalMsg: name = "fatal"; break;
    default: break;
    }
    const bool from_qml = message.contains(QLatin1String("qrc:/")) || message.contains(QLatin1String(".qml")) ||
                          (context.file != nullptr && QByteArray(context.file).endsWith(".qml"));
    if (type != QtDebugMsg && type != QtInfoMsg && from_qml) ++qml_warnings;
    std::fprintf(stderr, "qt %s: %s\n", name, message.toUtf8().constData());
    std::fflush(stderr);
    if (type == QtFatalMsg) std::abort();
}
QTextStream& out() {
    static QTextStream stream(stdout);
    return stream;
}
QQuickWindow* window_of(QQmlApplicationEngine& engine) {
    const auto roots = engine.rootObjects();
    return roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.first());
}
} // namespace

Automation::Automation(Launcher& launcher, QQmlApplicationEngine& engine, AutomationOptions options, QObject* parent)
    : QObject(parent), launcher_(launcher), engine_(engine), options_(std::move(options)) {
    pages_ = {QStringLiteral("home"), QStringLiteral("library"), QStringLiteral("library-detail"), QStringLiteral("bios"),
              QStringLiteral("bios-detail"), QStringLiteral("settings"), QStringLiteral("about")};
}

void Automation::start() {
    out() << "automation: data directory " << launcher_.dataDir() << Qt::endl;
    wait_idle([this] {
        if (!launcher_.loaded()) {
            fail(QStringLiteral("library did not load"));
            return finish();
        }
        out() << "automation: loaded " << launcher_.gameCount() << " disc(s), " << launcher_.biosCount() << " BIOS" << Qt::endl;
        if (!options_.imports.isEmpty()) launcher_.importPaths(options_.imports, QStringLiteral("auto"));
        wait_idle([this] { step(); });
    });
}

void Automation::wait_idle(const std::function<void()>& next, int timeout_ms) {
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    auto timer = new QTimer(this);
    timer->setInterval(50);
    // Idle twice in a row: a finished task may queue the next one (identification after a scan).
    auto idle_ticks = std::make_shared<int>(0);
    connect(timer, &QTimer::timeout, this, [this, timer, clock, idle_ticks, next, timeout_ms] {
        if (launcher_.loaded() && launcher_.pendingTasks() == 0) ++*idle_ticks;
        else *idle_ticks = 0;
        if (*idle_ticks >= 3) {
            timer->stop();
            timer->deleteLater();
            next();
        } else if (clock->elapsed() > timeout_ms) {
            timer->stop();
            timer->deleteLater();
            fail(QStringLiteral("timed out waiting for background work (%1)").arg(launcher_.activityText()));
            finish();
        }
    });
    timer->start();
}

void Automation::step() {
    auto* window = window_of(engine_);
    if (window == nullptr) {
        fail(QStringLiteral("no window"));
        return finish();
    }
    if (pages_.isEmpty()) return finish();
    const QString page = pages_.takeFirst();
    QVariant ok;
    if (!QMetaObject::invokeMethod(window, "automationShow", Q_RETURN_ARG(QVariant, ok), Q_ARG(QVariant, page)) || !ok.toBool())
        fail(QStringLiteral("page '%1' could not be shown").arg(page));
    // Let bindings, image loading and transitions settle before capturing.
    QTimer::singleShot(options_.screenshot_dir.isEmpty() ? 150 : 900, this, [this, page] {
        auto* current = window_of(engine_);
        const QString shown = current ? current->property("currentPageName").toString() : QString();
        if (!page.startsWith(shown)) fail(QStringLiteral("expected page '%1', window shows '%2'").arg(page, shown));
        else out() << "automation: page " << page << " ok" << Qt::endl;
        capture(page);
        step();
    });
}

void Automation::capture(const QString& name) {
    if (options_.screenshot_dir.isEmpty()) return;
    auto* window = window_of(engine_);
    if (window == nullptr) return;
    QDir().mkpath(options_.screenshot_dir);
    const QString file = QDir(options_.screenshot_dir).filePath(name + QStringLiteral(".png"));
    const QImage image = window->grabWindow();
    if (image.isNull() || !image.save(file)) fail(QStringLiteral("could not capture %1").arg(file));
    else out() << "automation: captured " << QDir::toNativeSeparators(file) << " (" << image.width() << "x" << image.height() << ")" << Qt::endl;
}

void install_automation_message_handler() { qInstallMessageHandler(automation_message); }

void Automation::fail(const QString& message) {
    ++failures_;
    out() << "automation: FAIL " << message << Qt::endl;
}

void Automation::finish() {
    const auto previousLanguage = launcher_.language();
    launcher_.setLanguage(QStringLiteral("pt-BR"));
    if (QCoreApplication::translate("SettingsPage", "Settings") != QString::fromUtf8("Configurações"))
        fail(QStringLiteral("Portuguese translation unavailable"));
    launcher_.setLanguage(QStringLiteral("en"));
    if (QCoreApplication::translate("SettingsPage", "Settings") != QStringLiteral("Settings"))
        fail(QStringLiteral("English translation unavailable"));
    launcher_.setLanguage(previousLanguage);

    launcher_.flush();
    // The persisted library, read back from disk, is what the next start restores.
    try {
        const auto library = ps2native::frontend::Library::load(fspath(launcher_.libraryFile()));
        int valid = 0;
        for (const auto& e : library.entries()) valid += e.status == ps2native::frontend::ProbeStatus::Valid ? 1 : 0;
        out() << "automation: persisted " << library.entries().size() << " entries (" << valid << " valid)" << Qt::endl;
        if (options_.expect_entries >= 0 && static_cast<int>(library.entries().size()) != options_.expect_entries)
            fail(QStringLiteral("expected %1 persisted entries").arg(options_.expect_entries));
        if (options_.expect_valid >= 0 && valid != options_.expect_valid)
            fail(QStringLiteral("expected %1 valid entries").arg(options_.expect_valid));
    } catch (const std::exception& error) {
        fail(QStringLiteral("library file unreadable: %1").arg(QString::fromUtf8(error.what())));
    }
    if (const int warnings = qml_warnings.load(); warnings > 0)
        fail(QStringLiteral("%1 QML warning(s) reported").arg(warnings));
    out() << (failures_ == 0 ? "automation: passed" : "automation: failed") << Qt::endl;
    QCoreApplication::exit(failures_ == 0 ? 0 : 1);
}
} // namespace ps2launcher
