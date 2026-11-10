#include "launcher.hpp"

#include "ps2native/console.hpp"
#include "ps2native/frontend/runtime.hpp"
#include "qt_support.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLocale>
#include <QSysInfo>
#include <QThread>

#include <algorithm>
#include <iomanip>
#include <sstream>

#ifndef PS2N_VERSION
#define PS2N_VERSION "0.0.0"
#endif

namespace ps2launcher {
namespace fe = ps2native::frontend;
using fe::ContentKind;
using fe::LibraryEntry;
using fe::ProbeStatus;

struct Launcher::Batch {
    ContentKind kind{ContentKind::Disc};
    bool announce{true};
    int added{};
    int duplicates{};
    int valid{};
    int invalid{};
    int refused{};      // folder candidates that were not ROM0 dumps
    int remaining{};    // probes still running
    bool submitted{};   // all paths of the batch were added
    QString single_title; // title of the only entry, for a precise message
    QString single_error;
};

namespace {
std::int64_t now_unix() { return QDateTime::currentSecsSinceEpoch(); }
QString capability_key(fe::Capability capability) {
    switch (capability) {
    case fe::Capability::FirmwareBrowser: return QStringLiteral("browser");
    case fe::Capability::DiscBoot: return QStringLiteral("disc-boot");
    case fe::Capability::Controller: return QStringLiteral("controller");
    case fe::Capability::Audio: return QStringLiteral("audio");
    case fe::Capability::MemoryCards: return QStringLiteral("memory-cards");
    case fe::Capability::GsFaithful: return QStringLiteral("gs-faithful");
    case fe::Capability::GsNative: return QStringLiteral("gs-native");
    }
    return QStringLiteral("unknown");
}
QString hex32(std::uint32_t value) {
    return QStringLiteral("0x%1").arg(value, 8, 16, QLatin1Char('0')).toUpper().replace(QLatin1String("0X"), QLatin1String("0x"));
}
QString exe_name(const QString& base) {
#ifdef Q_OS_WIN
    return base + QStringLiteral(".exe");
#else
    return base;
#endif
}
} // namespace

Launcher::Launcher(const QString& data_dir, QObject* parent)
    : QObject(parent),
      data_dir_(QDir::toNativeSeparators(data_dir)),
      game_model_(std::make_unique<EntryListModel>(ContentKind::Disc)),
      bios_model_(std::make_unique<EntryListModel>(ContentKind::Bios)),
      games_(std::make_unique<EntryFilterModel>(game_model_.get())),
      recent_(std::make_unique<EntryFilterModel>(game_model_.get())) {
    game_.setProcessChannelMode(QProcess::MergedChannels);
    game_.setStandardOutputFile(QDir(data_dir).filePath(QStringLiteral("ps2n.log")));
    connect(&game_, &QProcess::stateChanged, this, &Launcher::gameRunningChanged);
    connect(&game_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code == 0)
            emit notify(QStringLiteral("info"), game_title_, tr("Finished."));
        else
            emit notify(QStringLiteral("error"), game_title_,
                        tr("ps2n stopped (exit code %1). See %2 and the logs in the game folder.").arg(code).arg(QDir(data_dir_).filePath(QStringLiteral("ps2n.log"))));
    });
    recent_->setSortMode(QStringLiteral("recent"));
    pool_.setMaxThreadCount(std::clamp(QThread::idealThreadCount(), 2, 4));
    writer_.setMaxThreadCount(1);
    save_timer_.setSingleShot(true);
    save_timer_.setInterval(400);
    connect(&save_timer_, &QTimer::timeout, this, [this] {
        if (!library_dirty_ || library_readonly_) return;
        library_dirty_ = false;
        auto text = library_.serialize();
        const auto path = library_path();
        writer_.start([this, path, text = std::move(text)] {
            try {
                ps2native::write_file_atomically(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
            } catch (const std::exception& error) {
                const QString message = qstr(std::string(error.what()));
                QMetaObject::invokeMethod(this, [this, message] { emit notify(QStringLiteral("error"), tr("Library not saved"), message); },
                                          Qt::QueuedConnection);
            }
        });
    });
}

Launcher::~Launcher() {
    pool_.clear();
    pool_.waitForDone();
    flush();
}

void Launcher::flush() {
    save_timer_.stop();
    writer_.waitForDone();
    if (library_dirty_ && !library_readonly_ && loaded_) {
        try {
            library_.save(library_path());
            library_dirty_ = false;
        } catch (const std::exception&) {
        }
    }
}

std::filesystem::path Launcher::library_path() const { return fspath(data_dir_) / "library.ps2nlib"; }
std::filesystem::path Launcher::settings_path() const { return fspath(data_dir_) / "settings.cfg"; }
QString Launcher::libraryFile() const { return qpath(library_path()); }
QString Launcher::settingsFile() const { return qpath(settings_path()); }
fe::ProbeContext Launcher::probe_context() const { return fe::ProbeContext{settings_.covers_folder}; }

void Launcher::run(std::function<void()> work, QString label) {
    ++pending_tasks_;
    activity_.push_back(label);
    emit activityChanged();
    pool_.start([this, work = std::move(work), label] {
        try {
            work();
        } catch (const std::exception& error) {
            const QString message = qstr(std::string(error.what()));
            QMetaObject::invokeMethod(this, [this, label, message] { emit notify(QStringLiteral("error"), label, message); },
                                      Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, label] {
            activity_.removeOne(label);
            --pending_tasks_;
            emit activityChanged();
        }, Qt::QueuedConnection);
    });
}

QString Launcher::activityText() const {
    if (pending_tasks_ <= 0 || activity_.isEmpty()) return {};
    const QString current = activity_.last();
    return pending_tasks_ > 1 ? tr("%1 (%2 tasks)").arg(current).arg(pending_tasks_) : current;
}

void Launcher::initialize() {
    const auto library_file = library_path();
    const auto settings_file = settings_path();
    run([this, library_file, settings_file] {
        auto library = std::make_shared<fe::Library>();
        auto settings = std::make_shared<fe::LauncherSettings>();
        QStringList errors;
        bool readonly = false;
        try {
            ps2native::create_directories(library_file.parent_path());
        } catch (const std::exception& error) {
            errors << qstr(std::string(error.what()));
        }
        try {
            *settings = fe::LauncherSettings::load(settings_file);
        } catch (const std::exception& error) {
            errors << tr("Settings: %1").arg(qstr(std::string(error.what())));
        }
        try {
            *library = fe::Library::load(library_file);
        } catch (const std::exception& error) {
            readonly = true;
            errors << tr("Library: %1. The file is left untouched and changes are not saved.").arg(qstr(std::string(error.what())));
        }
        auto stamps = std::make_shared<std::vector<std::pair<std::string, fe::FileStamp>>>();
        for (const auto& e : library->entries()) stamps->emplace_back(e.id, fe::stamp_file(e.path));
        QMetaObject::invokeMethod(this, [this, library, settings, stamps, errors, readonly] {
            library_ = std::move(*library);
            settings_ = std::move(*settings);
            library_readonly_ = readonly;
            game_model_->reset(library_.entries());
            bios_model_->reset(library_.entries());
            if (!settings_.active_bios.empty() && library_.find(settings_.active_bios) == nullptr) settings_.active_bios.clear();
            bios_model_->setActiveId(qstr(settings_.active_bios));
            loaded_ = true;
            emit loadedChanged();
            emit settingsChanged();
            emit activeBiosChanged();
            emit statsChanged();
            for (const auto& message : errors) emit notify(QStringLiteral("error"), tr("Could not load"), message);
            for (const auto& [id, stamp] : *stamps) {
                const auto* e = library_.find(id);
                if (e == nullptr) continue;
                if (fe::needs_probe(*e, stamp)) queue_probe(id);
                else if (e->kind == ContentKind::Bios && !e->nvm_path.empty()) queue_nvm_probe(id);
            }
            if (settings_.scan_folders_on_start) scan_game_folders(false);
        }, Qt::QueuedConnection);
    }, tr("Loading library"));
}

void Launcher::update_entry(const LibraryEntry& entry) {
    if (entry.kind == ContentKind::Disc) game_model_->upsert(entry);
    else bios_model_->upsert(entry);
    emit entryChanged(qstr(entry.id));
    if (entry.id == settings_.active_bios) emit activeBiosChanged();
}

void Launcher::schedule_save() {
    library_dirty_ = true;
    save_timer_.start();
}

void Launcher::save_settings() {
    auto text = settings_.serialize();
    const auto path = settings_path();
    writer_.start([this, path, text = std::move(text)] {
        try {
            ps2native::write_file_atomically(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
        } catch (const std::exception& error) {
            const QString message = qstr(std::string(error.what()));
            QMetaObject::invokeMethod(this, [this, message] { emit notify(QStringLiteral("error"), tr("Settings not saved"), message); },
                                      Qt::QueuedConnection);
        }
    });
    emit settingsChanged();
}

void Launcher::queue_probe(const std::string& id, const std::shared_ptr<Batch>& batch) {
    const auto* e = library_.find(id);
    if (e == nullptr) return;
    const QString key = qstr(id);
    if (in_flight_.contains(key)) {
        reprobe_.insert(key);
        return;
    }
    in_flight_.insert(key);
    if (batch) ++batch->remaining;
    const auto kind = e->kind;
    const auto path = e->path;
    const auto context = probe_context();
    run([this, id, kind, path, context, batch] {
        const auto stamp = fe::stamp_file(path);
        auto result = std::make_shared<fe::ProbeResult>(fe::probe(kind, path, context));
        QMetaObject::invokeMethod(this, [this, id, result, stamp, batch] { on_probed(id, *result, stamp, batch); },
                                  Qt::QueuedConnection);
    }, kind == ContentKind::Bios ? tr("Identifying BIOS") : tr("Identifying discs"));
}

void Launcher::on_probed(const std::string& id, const fe::ProbeResult& result, const fe::FileStamp& stamp,
                         const std::shared_ptr<Batch>& batch) {
    const QString key = qstr(id);
    in_flight_.remove(key);
    if (auto* e = library_.find(id)) {
        fe::apply_probe(*e, result, stamp);
        update_entry(*e);
        schedule_save();
        if (e->kind == ContentKind::Bios && !e->nvm_path.empty()) queue_nvm_probe(id);
        if (batch) {
            if (result.status == ProbeStatus::Valid) ++batch->valid;
            else ++batch->invalid;
            batch->single_title = qstr(e->title);
            batch->single_error = qstr(e->error);
        }
        if (e->kind == ContentKind::Bios && e->status == ProbeStatus::Valid && settings_.active_bios.empty()) {
            setActiveBios(key);
            emit notify(QStringLiteral("info"), tr("Active BIOS selected"),
                        tr("%1 (%2) is now the active BIOS.").arg(qstr(e->title), qstr(e->value("romver"))));
        }
    }
    emit statsChanged();
    if (reprobe_.remove(key)) queue_probe(id);
    if (batch && --batch->remaining == 0 && batch->submitted) batch_done(batch);
}

void Launcher::queue_nvm_probe(const std::string& id) {
    const auto* e = library_.find(id);
    if (e == nullptr || e->nvm_path.empty()) return;
    const auto path = e->nvm_path;
    const auto profile = e->console_profile;
    run([this, id, path, profile] {
        auto result = std::make_shared<fe::ProbeResult>(fe::probe_nvm(path, profile));
        QMetaObject::invokeMethod(this, [this, id, path, profile, result] {
            auto* entry = library_.find(id);
            // Ignore a result for a configuration that changed meanwhile.
            if (entry == nullptr || entry->nvm_path != path || entry->console_profile != profile) return;
            fe::apply_nvm_probe(*entry, *result);
            update_entry(*entry);
            schedule_save();
        }, Qt::QueuedConnection);
    }, tr("Validating NVM"));
}

void Launcher::batch_done(const std::shared_ptr<Batch>& batch) {
    if (!batch->announce) return;
    const bool bios = batch->kind == ContentKind::Bios;
    if (batch->added == 0 && batch->duplicates == 0) {
        if (batch->refused > 0)
            emit notify(QStringLiteral("warning"), tr("No BIOS found"),
                        tr("%n file(s) of 4 MiB were examined; none is a valid ROM0 dump.", nullptr, batch->refused));
        else
            emit notify(QStringLiteral("info"), bios ? tr("No BIOS found") : tr("No disc images found"),
                        bios ? tr("No 4 MiB ROM0 candidates in that location.")
                             : tr("No .iso, .bin, .img, .cue, .cso or .chd files in that location."));
        return;
    }
    if (batch->added == 0) {
        emit notify(QStringLiteral("info"), tr("Already in the library"),
                    tr("%n item(s) were already registered.", nullptr, batch->duplicates));
        return;
    }
    if (batch->added == 1 && batch->invalid == 1) {
        emit notify(QStringLiteral("error"), bios ? tr("BIOS not recognized") : tr("Disc not recognized"),
                    QStringLiteral("%1: %2").arg(batch->single_title, batch->single_error));
        return;
    }
    QString message = bios ? tr("%n BIOS dump(s) added", nullptr, batch->added) : tr("%n disc image(s) added", nullptr, batch->added);
    if (batch->invalid > 0) message += QStringLiteral(" · ") + tr("%n with issues", nullptr, batch->invalid);
    if (batch->duplicates > 0) message += QStringLiteral(" · ") + tr("%n already registered", nullptr, batch->duplicates);
    if (batch->refused > 0) message += QStringLiteral(" · ") + tr("%n other 4 MiB file(s) skipped", nullptr, batch->refused);
    emit notify(batch->invalid > 0 ? QStringLiteral("warning") : QStringLiteral("success"),
                bios ? tr("BIOS imported") : tr("Library updated"), message);
}

void Launcher::add_paths(const std::vector<std::pair<std::filesystem::path, ContentKind>>& paths,
                         const std::shared_ptr<Batch>& batch) {
    for (const auto& [path, kind] : paths) {
        try {
            auto [entry, inserted] = library_.add(kind, path, now_unix());
            if (!inserted) {
                ++batch->duplicates;
                continue;
            }
            ++batch->added;
            update_entry(*entry);
            queue_probe(entry->id, batch);
        } catch (const std::exception& error) {
            ++batch->invalid;
            batch->single_error = qstr(std::string(error.what()));
        }
    }
    batch->submitted = true;
    if (batch->added > 0) schedule_save();
    emit statsChanged();
    if (batch->remaining == 0) batch_done(batch);
}

void Launcher::importUrls(const QList<QUrl>& urls, const QString& kind) {
    QStringList paths;
    for (const auto& url : urls)
        if (const auto path = local_path(url); !path.isEmpty()) paths << path;
    importPaths(paths, kind);
}

void Launcher::importPaths(const QStringList& paths, const QString& kind) {
    if (paths.isEmpty()) return;
    std::vector<std::filesystem::path> files;
    for (const auto& path : paths) files.push_back(fspath(path));
    run([this, files, kind] {
        auto discs = std::make_shared<std::vector<std::pair<std::filesystem::path, ContentKind>>>();
        auto bioses = std::make_shared<std::vector<std::pair<std::filesystem::path, ContentKind>>>();
        QStringList folders;
        QStringList nvms;
        QStringList unknown;
        for (const auto& file : files) {
            std::error_code error;
            if (std::filesystem::is_directory(ps2native::native_io_path(file), error)) {
                folders << qpath(file);
                continue;
            }
            std::optional<ContentKind> resolved;
            if (kind == QLatin1String("bios")) resolved = ContentKind::Bios;
            else if (kind == QLatin1String("disc")) resolved = ContentKind::Disc;
            else resolved = fe::guess_content_kind(file);
            if (!resolved) {
                (fe::is_nvm_candidate(file) ? nvms : unknown) << qpath(file.filename());
                continue;
            }
            (*resolved == ContentKind::Bios ? bioses : discs)->emplace_back(file, *resolved);
        }
        QMetaObject::invokeMethod(this, [this, discs, bioses, folders, nvms, unknown, kind] {
            if (!discs->empty()) {
                auto batch = std::make_shared<Batch>();
                batch->kind = ContentKind::Disc;
                add_paths(*discs, batch);
            }
            if (!bioses->empty()) {
                auto batch = std::make_shared<Batch>();
                batch->kind = ContentKind::Bios;
                add_paths(*bioses, batch);
            }
            for (const auto& folder : folders) importFolder(QUrl::fromLocalFile(folder), kind);
            if (!nvms.isEmpty())
                emit notify(QStringLiteral("info"), tr("NVM files are attached to a BIOS"),
                            tr("Open the BIOS page, select the console's BIOS and attach %1 there.").arg(nvms.join(QStringLiteral(", "))));
            if (!unknown.isEmpty())
                emit notify(QStringLiteral("warning"), tr("Not imported"),
                            tr("Not a disc image or a 4 MiB ROM0 dump: %1").arg(unknown.join(QStringLiteral(", "))));
        }, Qt::QueuedConnection);
    }, tr("Examining files"));
}

void Launcher::importFolder(const QUrl& url, const QString& kind) {
    const QString folder = local_path(url);
    if (folder.isEmpty()) return;
    const auto path = fspath(folder);
    // "bios": valid ROM0 dumps only. "disc": disc images, and the folder becomes
    // a game folder. "auto" (drag and drop): both; the folder is remembered only
    // when it holds disc images.
    const bool automatic = kind != QLatin1String("bios") && kind != QLatin1String("disc");
    // Both scans of an automatic import settle here, so an empty folder is still reported.
    struct AutoScan {
        int pending{2};
        bool found{};
    };
    const auto scan = automatic ? std::make_shared<AutoScan>() : std::shared_ptr<AutoScan>();
    const auto settle = [this, scan, folder](bool found) {
        if (!scan) return;
        scan->found = scan->found || found;
        if (--scan->pending == 0 && !scan->found)
            emit notify(QStringLiteral("info"), tr("Nothing to import"),
                        tr("No ROM0 dump or disc image in %1.").arg(QDir::toNativeSeparators(folder)));
    };
    if (kind == QLatin1String("bios") || automatic) {
        run([this, path, automatic, settle] {
            const auto candidates = fe::find_candidates(path, ContentKind::Bios, 2000);
            auto found = std::make_shared<std::vector<std::pair<std::filesystem::path, fe::ProbeResult>>>();
            int refused = 0;
            for (const auto& candidate : candidates) {
                auto result = fe::probe_bios(candidate);
                if (result.status == ProbeStatus::Valid) found->emplace_back(candidate, std::move(result));
                else ++refused;
            }
            QMetaObject::invokeMethod(this, [this, found, refused, automatic, settle] {
                settle(!found->empty() || refused > 0);
                auto batch = std::make_shared<Batch>();
                batch->kind = ContentKind::Bios;
                batch->refused = refused;
                // A dropped game folder has no ROM0 dumps; the disc scan speaks for it.
                batch->announce = !automatic || !found->empty() || refused > 0;
                for (const auto& [candidate, result] : *found) {
                    auto [entry, inserted] = library_.add(ContentKind::Bios, candidate, now_unix());
                    if (!inserted) {
                        ++batch->duplicates;
                        continue;
                    }
                    ++batch->added;
                    ++batch->valid;
                    fe::apply_probe(*entry, result, fe::stamp_file(candidate));
                    update_entry(*entry);
                    if (settings_.active_bios.empty()) setActiveBios(qstr(entry->id));
                }
                batch->submitted = true;
                if (batch->added > 0) schedule_save();
                emit statsChanged();
                batch_done(batch);
            }, Qt::QueuedConnection);
        }, tr("Scanning for BIOS dumps"));
        if (!automatic) return;
    }
    const auto normalized = fe::normalize_library_path(path);
    const auto remember = [this, normalized] {
        if (std::find(settings_.game_folders.begin(), settings_.game_folders.end(), normalized) == settings_.game_folders.end()) {
            settings_.game_folders.push_back(normalized);
            save_settings();
        }
    };
    if (!automatic) remember();
    run([this, normalized, automatic, remember, settle] {
        auto candidates = std::make_shared<std::vector<std::pair<std::filesystem::path, ContentKind>>>();
        for (auto& candidate : fe::find_candidates(normalized, ContentKind::Disc)) candidates->emplace_back(std::move(candidate), ContentKind::Disc);
        QMetaObject::invokeMethod(this, [this, candidates, automatic, remember, settle] {
            settle(!candidates->empty());
            if (automatic && candidates->empty()) return; // the BIOS scan reports this folder
            if (automatic) remember();
            auto batch = std::make_shared<Batch>();
            add_paths(*candidates, batch);
        }, Qt::QueuedConnection);
    }, tr("Scanning folder"));
}

void Launcher::scan_game_folders(bool announce) {
    for (const auto& folder : settings_.game_folders) {
        run([this, folder, announce] {
            std::error_code error;
            if (!std::filesystem::is_directory(ps2native::native_io_path(folder), error)) {
                const QString name = qpath(folder);
                if (announce)
                    QMetaObject::invokeMethod(this, [this, name] {
                        emit notify(QStringLiteral("warning"), tr("Folder not available"), name);
                    }, Qt::QueuedConnection);
                return;
            }
            auto candidates = std::make_shared<std::vector<std::pair<std::filesystem::path, ContentKind>>>();
            for (auto& candidate : fe::find_candidates(folder, ContentKind::Disc))
                candidates->emplace_back(std::move(candidate), ContentKind::Disc);
            QMetaObject::invokeMethod(this, [this, candidates, announce] {
                auto batch = std::make_shared<Batch>();
                batch->announce = announce;
                add_paths(*candidates, batch);
                // A startup scan only speaks when it found something new.
                if (!announce && batch->added > 0) {
                    batch->announce = true;
                    if (batch->remaining == 0) batch_done(batch);
                }
            }, Qt::QueuedConnection);
        }, tr("Scanning game folders"));
    }
}

void Launcher::removeGameFolder(const QString& folder) {
    const auto path = fe::normalize_library_path(fspath(folder));
    std::erase(settings_.game_folders, path);
    save_settings();
}

void Launcher::rescan() {
    for (const auto& e : library_.entries()) queue_probe(e.id);
    scan_game_folders(true);
}

void Launcher::removeEntry(const QString& id) {
    const auto key = utf8(id);
    const auto* e = library_.find(key);
    if (e == nullptr) return;
    const auto kind = e->kind;
    const QString title = qstr(e->title);
    library_.remove(key);
    (kind == ContentKind::Disc ? game_model_ : bios_model_)->remove(id);
    if (settings_.active_bios == key) {
        settings_.active_bios.clear();
        bios_model_->setActiveId({});
        save_settings();
        emit activeBiosChanged();
    }
    schedule_save();
    emit entryRemoved(id);
    emit statsChanged();
    emit notify(QStringLiteral("info"), tr("Removed from the library"), tr("%1 — the file itself was not touched.").arg(title));
}

void Launcher::reidentify(const QString& id) { queue_probe(utf8(id)); }

void Launcher::setFavorite(const QString& id, bool favorite) {
    if (auto* e = library_.find(utf8(id))) {
        e->favorite = favorite;
        update_entry(*e);
        schedule_save();
    }
}

void Launcher::renameEntry(const QString& id, const QString& title) {
    const auto text = title.trimmed();
    if (text.isEmpty()) return;
    if (auto* e = library_.find(utf8(id))) {
        e->title = utf8(text);
        update_entry(*e);
        schedule_save();
    }
}

QVariantMap Launcher::entry_map(const LibraryEntry& e) const {
    QVariantMap map;
    map.insert(QStringLiteral("id"), qstr(e.id));
    map.insert(QStringLiteral("kind"), QString::fromLatin1(fe::content_kind_name(e.kind)));
    map.insert(QStringLiteral("title"), qstr(e.title));
    map.insert(QStringLiteral("path"), qpath(e.path));
    map.insert(QStringLiteral("folder"), qpath(e.path.parent_path()));
    map.insert(QStringLiteral("fileName"), qpath(e.path.filename()));
    map.insert(QStringLiteral("status"), QString::fromLatin1(fe::probe_status_name(e.status)));
    map.insert(QStringLiteral("error"), qstr(e.error));
    map.insert(QStringLiteral("compatibility"), QString::fromLatin1(fe::compatibility_name(fe::compatibility_of(e))));
    map.insert(QStringLiteral("info"), qmap(e.info));
    map.insert(QStringLiteral("details"), qlist(e.details));
    map.insert(QStringLiteral("sizeText"), human_size(e.stamp.size));
    map.insert(QStringLiteral("sizeBytes"), static_cast<double>(e.stamp.size));
    map.insert(QStringLiteral("added"), QDateTime::fromSecsSinceEpoch(e.added).toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    map.insert(QStringLiteral("favorite"), e.favorite);
    map.insert(QStringLiteral("active"), e.id == settings_.active_bios && !e.id.empty());
    const auto cover = e.value("cover");
    map.insert(QStringLiteral("cover"), cover.empty() ? QString() : QUrl::fromLocalFile(qstr(cover)).toString());
    map.insert(QStringLiteral("consoleProfile"), qstr(e.console_profile));
    map.insert(QStringLiteral("nvmPath"), qpath(e.nvm_path));
    map.insert(QStringLiteral("nvmStatus"), e.nvm_path.empty() ? QString() : QString::fromLatin1(fe::probe_status_name(e.nvm_status)));
    map.insert(QStringLiteral("nvmError"), qstr(e.nvm_error));
    map.insert(QStringLiteral("nvmInfo"), qmap(e.nvm_info));
    return map;
}

QVariantMap Launcher::entry(const QString& id) const {
    const auto* e = library_.find(utf8(id));
    return e == nullptr ? QVariantMap{} : entry_map(*e);
}

QString Launcher::activeBiosId() const { return qstr(settings_.active_bios); }
QVariantMap Launcher::activeBios() const {
    if (settings_.active_bios.empty()) return {};
    return entry(qstr(settings_.active_bios));
}

bool Launcher::setActiveBios(const QString& id) {
    const auto key = utf8(id);
    if (!key.empty()) {
        const auto* e = library_.find(key);
        if (e == nullptr || e->kind != ContentKind::Bios) return false;
        if (e->status != ProbeStatus::Valid) {
            emit notify(QStringLiteral("error"), tr("Cannot use this BIOS"), qstr(e->error));
            return false;
        }
    }
    settings_.active_bios = key;
    bios_model_->setActiveId(id);
    save_settings();
    emit activeBiosChanged();
    return true;
}

QVariantList Launcher::profilesFor(const QString& biosId) const {
    QVariantList list;
    const auto* e = library_.find(utf8(biosId));
    if (e == nullptr) return list;
    for (const auto& name : fe::console_profiles_for_romver(e->value("romver"))) {
        const auto* hardware = ps2native::find_console_hardware(name);
        if (hardware == nullptr) continue;
        QVariantMap profile;
        profile.insert(QStringLiteral("id"), qstr(name));
        profile.insert(QStringLiteral("model"), qstr(hardware->model));
        profile.insert(QStringLiteral("mainboard"), qstr(hardware->mainboard));
        profile.insert(QStringLiteral("evidence"), QString::fromLatin1(ps2native::profile_evidence_name(hardware->evidence)));
        profile.insert(QStringLiteral("dEnabler"), hardware->d_enabler ? hex32(*hardware->d_enabler) : tr("not measured"));
        profile.insert(QStringLiteral("source"), qstr(hardware->source));
        profile.insert(QStringLiteral("iopRevision"), hardware->iop_prid ? hex32(*hardware->iop_prid) : tr("unknown"));
        profile.insert(QStringLiteral("nvmKnown"), hardware->mechacon.has_value());
        list.push_back(profile);
    }
    return list;
}

void Launcher::setConsoleProfile(const QString& biosId, const QString& profile) {
    auto* e = library_.find(utf8(biosId));
    if (e == nullptr) return;
    const auto name = utf8(profile);
    if (!name.empty()) {
        const auto allowed = fe::console_profiles_for_romver(e->value("romver"));
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            emit notify(QStringLiteral("error"), tr("Profile refused"),
                        tr("%1 does not ship with ROMVER %2.").arg(profile, qstr(e->value("romver"))));
            return;
        }
    }
    if (e->console_profile == name) return;
    e->console_profile = name;
    if (!e->nvm_path.empty()) {
        fe::apply_nvm_probe(*e, fe::ProbeResult{});
        queue_nvm_probe(e->id);
    }
    update_entry(*e);
    schedule_save();
}

void Launcher::attachNvm(const QString& biosId, const QUrl& file) {
    auto* e = library_.find(utf8(biosId));
    const QString path = local_path(file);
    if (e == nullptr || path.isEmpty()) return;
    e->nvm_path = fe::normalize_library_path(fspath(path));
    fe::apply_nvm_probe(*e, fe::ProbeResult{});
    update_entry(*e);
    schedule_save();
    queue_nvm_probe(e->id);
}

void Launcher::detachNvm(const QString& biosId) {
    if (auto* e = library_.find(utf8(biosId))) {
        e->nvm_path.clear();
        fe::apply_nvm_probe(*e, fe::ProbeResult{});
        update_entry(*e);
        schedule_save();
    }
}

bool Launcher::startGame(const QString& id) {
    return launchGame(id,false);
}
bool Launcher::prepareGame(const QString& id) {
    return launchGame(id,true);
}
bool Launcher::launchGame(const QString& id, bool prepare_only) {
    const auto fail = [&](const QString& reason) {
        emit notify(QStringLiteral("error"), tr("Game not started"), reason);
        return false;
    };
    const auto* game = library_.find(utf8(id));
    if (!game || game->kind != ContentKind::Disc || game->status != ProbeStatus::Valid || game->value("platform") != "PlayStation 2")
        return fail(tr("Select an identified PlayStation 2 disc."));
    if (gameRunning()) return fail(tr("Another game is running or being prepared."));
    if (!gameToolAvailable()) return fail(tr("ps2n was not found next to the launcher: %1").arg(gameTool()));
    // ps2n prepares (PS2Recomp analysis, recompilation, build) and runs the game in its own window.
    game_title_ = qstr(game->title);
    game_.start(gameTool(), {prepare_only ? QStringLiteral("prepare") : QStringLiteral("play"), qpath(game->path)});
    emit notify(QStringLiteral("info"), game_title_,
                prepare_only ? tr("Preparing with PS2Recomp. The first time takes several minutes.")
                             : tr("Starting. The first time PS2Recomp prepares the game, which takes several minutes."));
    return true;
}
void Launcher::stopGame() {
    if (gameRunning()) game_.kill();
}

int Launcher::gameCount() const { return game_model_->count(); }
int Launcher::identifiedGameCount() const {
    return static_cast<int>(std::count_if(game_model_->entries().begin(), game_model_->entries().end(), [](const LibraryEntry& e) {
        return fe::compatibility_of(e) == fe::Compatibility::RuntimeUnavailable;
    }));
}
int Launcher::issueGameCount() const {
    return static_cast<int>(std::count_if(game_model_->entries().begin(), game_model_->entries().end(), [](const LibraryEntry& e) {
        return fe::needs_attention(fe::compatibility_of(e));
    }));
}
int Launcher::biosCount() const { return bios_model_->count(); }
int Launcher::validBiosCount() const {
    return static_cast<int>(std::count_if(bios_model_->entries().begin(), bios_model_->entries().end(),
                                          [](const LibraryEntry& e) { return e.status == ProbeStatus::Valid; }));
}

QStringList Launcher::gameFolders() const {
    QStringList folders;
    for (const auto& folder : settings_.game_folders) folders << qpath(folder);
    return folders;
}
QString Launcher::coversFolder() const { return qpath(settings_.covers_folder); }
void Launcher::setCoversFolder(const QUrl& folder) {
    const QString path = local_path(folder);
    if (path.isEmpty()) return;
    settings_.covers_folder = fe::normalize_library_path(fspath(path));
    save_settings();
    for (const auto& e : library_.entries())
        if (e.kind == ContentKind::Disc) queue_probe(e.id);
}
void Launcher::clearCoversFolder() {
    settings_.covers_folder.clear();
    save_settings();
    for (const auto& e : library_.entries())
        if (e.kind == ContentKind::Disc) queue_probe(e.id);
}
QString Launcher::gameTool() const {
    return QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + QLatin1Char('/') + exe_name(QStringLiteral("ps2n")));
}
bool Launcher::gameToolAvailable() const { return QFileInfo(gameTool()).isFile(); }
QString Launcher::renderer() const { return qstr(settings_.renderer); }
void Launcher::setRenderer(const QString& value) {
    if (value != QLatin1String("gs-faithful") && value != QLatin1String("native")) return;
    settings_.renderer = utf8(value);
    save_settings();
}
bool Launcher::reduceMotion() const { return settings_.reduce_motion; }
void Launcher::setReduceMotion(bool value) {
    settings_.reduce_motion = value;
    save_settings();
}
bool Launcher::scanOnStart() const { return settings_.scan_folders_on_start; }
void Launcher::setScanOnStart(bool value) {
    settings_.scan_folders_on_start = value;
    save_settings();
}
QString Launcher::libraryView() const { return qstr(settings_.library_view); }
void Launcher::setLibraryView(const QString& value) {
    if (value != QLatin1String("grid") && value != QLatin1String("list")) return;
    settings_.library_view = utf8(value);
    save_settings();
}

QVariantList Launcher::capabilities() const {
    QVariantList list;
    for (const auto& c : fe::runtime_capabilities()) {
        QVariantMap map;
        map.insert(QStringLiteral("key"), capability_key(c.capability));
        map.insert(QStringLiteral("name"), qstr(c.name));
        map.insert(QStringLiteral("available"), c.available);
        map.insert(QStringLiteral("reason"), qstr(c.reason));
        list.push_back(map);
    }
    return list;
}

QVariantMap Launcher::buildInfo() const {
    QVariantMap map;
    map.insert(QStringLiteral("version"), QStringLiteral(PS2N_VERSION));
    map.insert(QStringLiteral("qtRuntime"), QString::fromLatin1(qVersion()));
    map.insert(QStringLiteral("qtBuild"), QStringLiteral(QT_VERSION_STR));
    map.insert(QStringLiteral("cpu"), QSysInfo::buildCpuArchitecture());
    map.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
    map.insert(QStringLiteral("platform"), QGuiApplication::platformName());
#if defined(_MSC_VER) && !defined(__clang__)
    map.insert(QStringLiteral("compiler"), QStringLiteral("MSVC %1").arg(_MSC_VER));
#elif defined(__clang__)
    map.insert(QStringLiteral("compiler"), QStringLiteral("Clang %1.%2").arg(__clang_major__).arg(__clang_minor__));
#elif defined(__GNUC__)
    map.insert(QStringLiteral("compiler"), QStringLiteral("GCC %1.%2").arg(__GNUC__).arg(__GNUC_MINOR__));
#else
    map.insert(QStringLiteral("compiler"), QStringLiteral("unknown"));
#endif
    return map;
}

QString Launcher::monoFamily() const {
    // A modern installed code font when present; otherwise the platform's fixed font.
    static const QString family = [] {
        const QStringList installed = QFontDatabase::families();
        for (const auto* name : {"Cascadia Mono", "Cascadia Code", "SF Mono", "Menlo", "JetBrains Mono", "Consolas",
                                 "DejaVu Sans Mono", "Liberation Mono", "Noto Sans Mono", "Ubuntu Mono"}) {
            const QString candidate = QString::fromLatin1(name);
            if (installed.contains(candidate, Qt::CaseInsensitive)) return candidate;
        }
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    }();
    return family;
}

void Launcher::openFolder(const QString& path) const {
    QFileInfo info(path);
    const QString folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}
void Launcher::openUrl(const QString& url) const {
    const QUrl target(url);
    if (target.scheme() == QLatin1String("https")) QDesktopServices::openUrl(target);
}
void Launcher::copyText(const QString& text) const {
    if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(text);
}
QString Launcher::fileUrl(const QString& path) const { return QUrl::fromLocalFile(path).toString(); }
QString Launcher::formatCount(double value) const { return QLocale().toString(static_cast<qlonglong>(value)); }
} // namespace ps2launcher

namespace ps2launcher {
QString Launcher::language() const { return qstr(settings_.language); }
void Launcher::setLanguage(const QString& value) {
    if (value != "auto" && value != "en" && value != "pt-BR") return;
    if (value == language()) return;
    settings_.language = utf8(value);
    save_settings();
}
}
