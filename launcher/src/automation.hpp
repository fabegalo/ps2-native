#pragma once
// Scripted runs of the real window for tests and visual validation: import
// files, wait for identification, visit every page (optionally capturing each
// to PNG), optionally run the boot diagnostic, then check what was persisted.
// Used by CTest with the offscreen platform and by developers with a real one.
#include <QObject>
#include <QStringList>

#include <functional>

class QQmlApplicationEngine;

namespace ps2launcher {
class Launcher;

// Automation mode: Qt messages go to stderr (a WIN32 GUI process has no
// console otherwise) and any warning raised by QML code fails the run.
void install_automation_message_handler();

struct AutomationOptions {
    QStringList imports;          // files or folders, kind detected automatically
    QString screenshot_dir;       // empty: no captures
    int expect_entries{-1};       // library entries expected after the run (-1: not checked)
    int expect_valid{-1};         // valid entries expected (-1: not checked)
    bool run_diagnostic{};        // start ps2n-boot on the active BIOS and wait for its result
    bool expect_diagnostic_result{};
};

class Automation : public QObject {
    Q_OBJECT
public:
    Automation(Launcher& launcher, QQmlApplicationEngine& engine, AutomationOptions options, QObject* parent = nullptr);
    void start();

private:
    void step();
    void wait_idle(const std::function<void()>& next, int timeout_ms = 120000);
    void capture(const QString& name);
    void fail(const QString& message);
    void finish();

    Launcher& launcher_;
    QQmlApplicationEngine& engine_;
    AutomationOptions options_;
    QStringList pages_;
    int failures_{};
};
} // namespace ps2launcher
