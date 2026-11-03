#include "entry_model.hpp"

#include "qt_support.hpp"

#include <QFileInfo>
#include <QUrl>

#include <algorithm>

namespace ps2launcher {
using ps2native::frontend::Compatibility;
using ps2native::frontend::LibraryEntry;

QString human_size(std::uint64_t bytes) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0 ? QStringLiteral("%1 B").arg(bytes)
                     : QStringLiteral("%1 %2").arg(value, 0, 'f', value < 10.0 ? 2 : 1).arg(QLatin1String(units[unit]));
}

EntryListModel::EntryListModel(ps2native::frontend::ContentKind kind, QObject* parent)
    : QAbstractListModel(parent), kind_(kind) {}

int EntryListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }

QVariant EntryListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
    const LibraryEntry& e = entries_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole: return qstr(e.title);
    case IdRole: return qstr(e.id);
    case PathRole: return qpath(e.path);
    case FolderRole: return qpath(e.path.parent_path());
    case FileNameRole: return qpath(e.path.filename());
    case StatusRole: return QString::fromLatin1(ps2native::frontend::probe_status_name(e.status));
    case ErrorRole: return qstr(e.error);
    case WarningRole: return qstr(e.value("warning"));
    case CompatibilityRole: return QString::fromLatin1(ps2native::frontend::compatibility_name(compatibility_of(e)));
    case SerialRole: return qstr(e.value("serial"));
    case RegionRole: return qstr(e.value("region"));
    case VideoModeRole: return qstr(e.value("video_mode"));
    case FormatRole: return qstr(e.value("format"));
    case PlatformRole: return qstr(e.value("platform"));
    case CoverRole: {
        const auto cover = e.value("cover");
        return cover.empty() ? QString() : QUrl::fromLocalFile(qstr(cover)).toString();
    }
    case SizeRole: return human_size(e.stamp.size);
    case AddedRole: return static_cast<double>(e.added);
    case FavoriteRole: return e.favorite;
    case ActiveRole: return !active_.isEmpty() && active_ == qstr(e.id);
    case RomverRole: return qstr(e.value("romver"));
    case VersionRole: return qstr(e.value("version"));
    case ConsoleKindRole: return qstr(e.value("console_kind"));
    case BuildDateRole: return qstr(e.value("build_date"));
    case Sha256Role: return qstr(e.value("sha256"));
    case ConsoleProfileRole: return qstr(e.console_profile);
    case NvmStatusRole:
        return e.nvm_path.empty() ? QString() : QString::fromLatin1(ps2native::frontend::probe_status_name(e.nvm_status));
    case SearchRole:
        return qstr(e.title + ' ' + e.value("serial") + ' ' + e.value("region") + ' ' + e.value("volume_id") + ' ' +
                    e.value("romver") + ' ' + ps2native::path_utf8(e.path.filename()));
    default: return {};
    }
}

QHash<int, QByteArray> EntryListModel::roleNames() const {
    return {{IdRole, "entryId"},         {TitleRole, "title"},
            {PathRole, "path"},          {FolderRole, "folder"},
            {FileNameRole, "fileName"},  {StatusRole, "status"},
            {ErrorRole, "error"},        {WarningRole, "warning"},
            {CompatibilityRole, "compatibility"}, {SerialRole, "serial"},
            {RegionRole, "region"},      {VideoModeRole, "videoMode"},
            {FormatRole, "format"},      {PlatformRole, "platform"},
            {CoverRole, "cover"},        {SizeRole, "sizeText"},
            {AddedRole, "added"},        {FavoriteRole, "favorite"},
            {ActiveRole, "active"},      {RomverRole, "romver"},
            {VersionRole, "biosVersion"}, {ConsoleKindRole, "consoleKind"},
            {BuildDateRole, "buildDate"}, {Sha256Role, "sha256"},
            {ConsoleProfileRole, "consoleProfile"}, {NvmStatusRole, "nvmStatus"},
            {SearchRole, "searchText"}};
}

void EntryListModel::reset(const std::vector<LibraryEntry>& entries) {
    beginResetModel();
    entries_.clear();
    for (const auto& e : entries)
        if (e.kind == kind_) entries_.push_back(e);
    endResetModel();
    emit countChanged();
}

void EntryListModel::upsert(const LibraryEntry& entry) {
    if (entry.kind != kind_) return;
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const LibraryEntry& e) { return e.id == entry.id; });
    if (it != entries_.end()) {
        *it = entry;
        const auto row = static_cast<int>(it - entries_.begin());
        emit dataChanged(index(row), index(row));
        return;
    }
    beginInsertRows({}, count(), count());
    entries_.push_back(entry);
    endInsertRows();
    emit countChanged();
}

void EntryListModel::remove(const QString& id) {
    const auto key = utf8(id);
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const LibraryEntry& e) { return e.id == key; });
    if (it == entries_.end()) return;
    const auto row = static_cast<int>(it - entries_.begin());
    beginRemoveRows({}, row, row);
    entries_.erase(it);
    endRemoveRows();
    emit countChanged();
}

void EntryListModel::setActiveId(const QString& id) {
    if (active_ == id) return;
    active_ = id;
    if (!entries_.empty()) emit dataChanged(index(0), index(count() - 1), {ActiveRole});
}

int EntryListModel::indexOfId(const QString& id) const {
    const auto key = utf8(id);
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].id == key) return static_cast<int>(i);
    return -1;
}

EntryFilterModel::EntryFilterModel(EntryListModel* source, QObject* parent) : QSortFilterProxyModel(parent) {
    setSourceModel(source);
    setDynamicSortFilter(true);
    sort(0);
    connect(this, &QAbstractItemModel::rowsInserted, this, &EntryFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &EntryFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &EntryFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &EntryFilterModel::countChanged);
}
void EntryFilterModel::setSearch(const QString& value) {
    if (search_ == value) return;
    search_ = value;
    invalidateFilter();
    emit searchChanged();
    emit countChanged();
}
void EntryFilterModel::setFilter(const QString& value) {
    if (filter_ == value) return;
    filter_ = value;
    invalidateFilter();
    emit filterChanged();
    emit countChanged();
}
void EntryFilterModel::setSortMode(const QString& value) {
    if (sort_ == value) return;
    sort_ = value;
    invalidate();
    sort(0);
    emit sortModeChanged();
}
QString EntryFilterModel::idAt(int row) const {
    return data(index(row, 0), EntryListModel::IdRole).toString();
}
int EntryFilterModel::rowOfId(const QString& id) const {
    for (int row = 0; row < rowCount(); ++row)
        if (idAt(row) == id) return row;
    return -1;
}
bool EntryFilterModel::filterAcceptsRow(int row, const QModelIndex& parent) const {
    const auto index = sourceModel()->index(row, 0, parent);
    if (!search_.trimmed().isEmpty()) {
        const auto haystack = index.data(EntryListModel::SearchRole).toString();
        const auto words = search_.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const auto& word : words)
            if (!haystack.contains(word, Qt::CaseInsensitive)) return false;
    }
    const auto compatibility = index.data(EntryListModel::CompatibilityRole).toString();
    if (filter_ == QLatin1String("ps2")) return compatibility == QLatin1String("runtime-unavailable");
    if (filter_ == QLatin1String("issues"))
        return compatibility == QLatin1String("missing") || compatibility == QLatin1String("invalid-image") ||
               compatibility == QLatin1String("unsupported-format"); // frontend::needs_attention
    if (filter_ == QLatin1String("favorites")) return index.data(EntryListModel::FavoriteRole).toBool();
    return true;
}
bool EntryFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    if (sort_ == QLatin1String("recent")) {
        const auto a = left.data(EntryListModel::AddedRole).toDouble();
        const auto b = right.data(EntryListModel::AddedRole).toDouble();
        if (a != b) return a > b;
    } else if (sort_ == QLatin1String("region")) {
        const auto a = left.data(EntryListModel::RegionRole).toString();
        const auto b = right.data(EntryListModel::RegionRole).toString();
        if (a != b) return QString::localeAwareCompare(a, b) < 0;
    }
    return QString::localeAwareCompare(left.data(EntryListModel::TitleRole).toString(),
                                       right.data(EntryListModel::TitleRole).toString()) < 0;
}
} // namespace ps2launcher
