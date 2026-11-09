#pragma once
// The launcher backend exposed to QML as the Launcher singleton. All file I/O
// (loading, identification, folder scans, saving) runs on worker threads; the
// UI thread only applies results. The library and settings live in the data
// directory; user files stay where they are.
#include "diagnostic_runner.hpp"
#include "entry_model.hpp"
#include "ps2native/frontend/library.hpp"

#include <QObject>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace ps2launcher {
class Launcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(ps2launcher::EntryFilterModel* games READ games CONSTANT)
    Q_PROPERTY(ps2launcher::EntryFilterModel* recentGames READ recentGames CONSTANT)
    Q_PROPERTY(ps2launcher::EntryListModel* bioses READ bioses CONSTANT)
    Q_PROPERTY(ps2launcher::DiagnosticRunner* diagnostic READ diagnostic CONSTANT)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(int pendingTasks READ pendingTasks NOTIFY activityChanged)
    Q_PROPERTY(QString activityText READ activityText NOTIFY activityChanged)
    Q_PROPERTY(QString activeBiosId READ activeBiosId NOTIFY activeBiosChanged)
    Q_PROPERTY(QVariantMap activeBios READ activeBios NOTIFY activeBiosChanged)
    Q_PROPERTY(int gameCount READ gameCount NOTIFY statsChanged)
    Q_PROPERTY(int identifiedGameCount READ identifiedGameCount NOTIFY statsChanged)
    Q_PROPERTY(int issueGameCount READ issueGameCount NOTIFY statsChanged)
    Q_PROPERTY(int biosCount READ biosCount NOTIFY statsChanged)
    Q_PROPERTY(int validBiosCount READ validBiosCount NOTIFY statsChanged)
    Q_PROPERTY(QStringList gameFolders READ gameFolders NOTIFY settingsChanged)
    Q_PROPERTY(QString coversFolder READ coversFolder NOTIFY settingsChanged)
    Q_PROPERTY(QString bootToolOverride READ bootToolOverride NOTIFY settingsChanged)
    Q_PROPERTY(QString bootTool READ bootTool NOTIFY settingsChanged)
    Q_PROPERTY(bool bootToolAvailable READ bootToolAvailable NOTIFY settingsChanged)
    Q_PROPERTY(int diagnosticBudget READ diagnosticBudget WRITE setDiagnosticBudget NOTIFY settingsChanged)
    Q_PROPERTY(bool diagnosticFullTrace READ diagnosticFullTrace WRITE setDiagnosticFullTrace NOTIFY settingsChanged)
    Q_PROPERTY(QString renderer READ renderer WRITE setRenderer NOTIFY settingsChanged)
    Q_PROPERTY(bool reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY settingsChanged)
    Q_PROPERTY(bool scanOnStart READ scanOnStart WRITE setScanOnStart NOTIFY settingsChanged)
    Q_PROPERTY(QString libraryView READ libraryView WRITE setLibraryView NOTIFY settingsChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QString libraryFile READ libraryFile CONSTANT)
    Q_PROPERTY(QString settingsFile READ settingsFile CONSTANT)
    Q_PROPERTY(QVariantList capabilities READ capabilities CONSTANT)
    Q_PROPERTY(QVariantMap buildInfo READ buildInfo CONSTANT)
    Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT)
public:
    explicit Launcher(const QString& data_dir, QObject* parent = nullptr);
    ~Launcher() override;

    // Loads the library and settings, then revalidates entries and scans folders.
    void initialize();
    // Writes pending changes synchronously (application exit).
    void flush();

    EntryFilterModel* games() { return games_.get(); }
    EntryFilterModel* recentGames() { return recent_.get(); }
    EntryListModel* bioses() { return bios_model_.get(); }
    DiagnosticRunner* diagnostic() { return diagnostic_.get(); }
    [[nodiscard]] bool loaded() const { return loaded_; }
    [[nodiscard]] int pendingTasks() const { return pending_tasks_; }
    [[nodiscard]] QString activityText() const;
    [[nodiscard]] QString activeBiosId() const;
    [[nodiscard]] QVariantMap activeBios() const;
    [[nodiscard]] int gameCount() const;
    [[nodiscard]] int identifiedGameCount() const;
    [[nodiscard]] int issueGameCount() const;
    [[nodiscard]] int biosCount() const;
    [[nodiscard]] int validBiosCount() const;
    [[nodiscard]] QStringList gameFolders() const;
    [[nodiscard]] QString coversFolder() const;
    [[nodiscard]] QString bootToolOverride() const;
    [[nodiscard]] QString bootTool() const;
    [[nodiscard]] bool bootToolAvailable() const;
    [[nodiscard]] int diagnosticBudget() const;
    void setDiagnosticBudget(int value);
    [[nodiscard]] bool diagnosticFullTrace() const;
    void setDiagnosticFullTrace(bool value);
    [[nodiscard]] QString renderer() const;
    void setRenderer(const QString& value);
    [[nodiscard]] bool reduceMotion() const;
    void setReduceMotion(bool value);
    [[nodiscard]] bool scanOnStart() const;
    void setScanOnStart(bool value);
    [[nodiscard]] QString libraryView() const;
    void setLibraryView(const QString& value);
    QString language() const;
    void setLanguage(const QString& value);
    [[nodiscard]] QString dataDir() const { return data_dir_; }
    [[nodiscard]] QString libraryFile() const;
    [[nodiscard]] QString settingsFile() const;
    [[nodiscard]] QVariantList capabilities() const;
    [[nodiscard]] QVariantMap buildInfo() const;
    [[nodiscard]] QString monoFamily() const;

    // kind: "auto", "bios" or "disc".
    Q_INVOKABLE void importUrls(const QList<QUrl>& urls, const QString& kind);
    Q_INVOKABLE void importPaths(const QStringList& paths, const QString& kind);
    // Discs: the folder is remembered and scanned on start. BIOS: one-time scan, valid ROM0 only.
    Q_INVOKABLE void importFolder(const QUrl& folder, const QString& kind);
    Q_INVOKABLE void removeGameFolder(const QString& folder);
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void removeEntry(const QString& id);
    Q_INVOKABLE void reidentify(const QString& id);
    Q_INVOKABLE void setFavorite(const QString& id, bool favorite);
    Q_INVOKABLE void renameEntry(const QString& id, const QString& title);
    Q_INVOKABLE QVariantMap entry(const QString& id) const;
    Q_INVOKABLE bool setActiveBios(const QString& id);
    Q_INVOKABLE QVariantList profilesFor(const QString& biosId) const;
    Q_INVOKABLE void setConsoleProfile(const QString& biosId, const QString& profile);
    Q_INVOKABLE void attachNvm(const QString& biosId, const QUrl& file);
    Q_INVOKABLE void detachNvm(const QString& biosId);
    Q_INVOKABLE bool startDiagnostic(const QString& biosId);
    void setRuntimeSession(ps2native::frontend::RuntimeSession* session) { session_ = session; }
    Q_INVOKABLE bool startGame(const QString& id);
    Q_INVOKABLE bool prepareGame(const QString& id);
    Q_INVOKABLE void setCoversFolder(const QUrl& folder);
    Q_INVOKABLE void clearCoversFolder();
    Q_INVOKABLE void setBootToolOverride(const QUrl& file);
    Q_INVOKABLE void clearBootToolOverride();
    Q_INVOKABLE void openFolder(const QString& path) const;
    Q_INVOKABLE void openUrl(const QString& url) const;
    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE QString fileUrl(const QString& path) const;
    Q_INVOKABLE QString formatCount(double value) const;

signals:
    void gameStarted();
    void loadedChanged();
    void activityChanged();
    void activeBiosChanged();
    void statsChanged();
    void settingsChanged();
    void entryChanged(const QString& id);
    void entryRemoved(const QString& id);
    // level: info | success | warning | error
    void notify(const QString& level, const QString& title, const QString& message);

private:
    struct Batch;
    void run(std::function<void()> work, QString label);
    void finish_task();
    void queue_probe(const std::string& id, const std::shared_ptr<Batch>& batch = {});
    void queue_nvm_probe(const std::string& id);
    void on_probed(const std::string& id, const ps2native::frontend::ProbeResult& result,
                   const ps2native::frontend::FileStamp& stamp, const std::shared_ptr<Batch>& batch);
    void add_paths(const std::vector<std::pair<std::filesystem::path, ps2native::frontend::ContentKind>>& paths,
                   const std::shared_ptr<Batch>& batch);
    void scan_game_folders(bool announce);
    void update_entry(const ps2native::frontend::LibraryEntry& entry);
    void schedule_save();
    void save_settings();
    void batch_done(const std::shared_ptr<Batch>& batch);
    [[nodiscard]] QVariantMap entry_map(const ps2native::frontend::LibraryEntry& entry) const;
    [[nodiscard]] std::filesystem::path library_path() const;
    [[nodiscard]] std::filesystem::path settings_path() const;
    [[nodiscard]] ps2native::frontend::ProbeContext probe_context() const;

    QString data_dir_;
    bool launchGame(const QString& id,bool prepare_only);
    ps2native::frontend::RuntimeSession* session_{};
    ps2native::frontend::Library library_;
    ps2native::frontend::LauncherSettings settings_;
    std::unique_ptr<EntryListModel> game_model_;
    std::unique_ptr<EntryListModel> bios_model_;
    std::unique_ptr<EntryFilterModel> games_;
    std::unique_ptr<EntryFilterModel> recent_;
    std::unique_ptr<DiagnosticRunner> diagnostic_;
    QThreadPool pool_;
    QThreadPool writer_;
    QTimer save_timer_;
    QSet<QString> in_flight_;
    QSet<QString> reprobe_;
    QStringList activity_;
    int pending_tasks_{};
    bool loaded_{};
    bool library_dirty_{};
    bool library_readonly_{}; // a newer launcher wrote it: never overwrite
};
} // namespace ps2launcher
