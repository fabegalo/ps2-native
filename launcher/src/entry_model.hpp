#pragma once
// List models over the library entries of one kind (discs or BIOS dumps), and a
// filter/sort proxy for search in the UI.
#include "ps2native/frontend/library.hpp"

#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace ps2launcher {
class EntryListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        PathRole,
        FolderRole,
        FileNameRole,
        StatusRole,
        ErrorRole,
        WarningRole,
        CompatibilityRole,
        SerialRole,
        RegionRole,
        VideoModeRole,
        FormatRole,
        PlatformRole,
        CoverRole,
        SizeRole,
        AddedRole,
        FavoriteRole,
        ActiveRole,
        RomverRole,
        VersionRole,
        ConsoleKindRole,
        BuildDateRole,
        Sha256Role,
        ConsoleProfileRole,
        NvmStatusRole,
        SearchRole
    };
    explicit EntryListModel(ps2native::frontend::ContentKind kind, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(entries_.size()); }

    void reset(const std::vector<ps2native::frontend::LibraryEntry>& entries);
    void upsert(const ps2native::frontend::LibraryEntry& entry);
    void remove(const QString& id);
    void setActiveId(const QString& id);
    [[nodiscard]] ps2native::frontend::ContentKind kind() const { return kind_; }
    [[nodiscard]] const std::vector<ps2native::frontend::LibraryEntry>& entries() const { return entries_; }
    Q_INVOKABLE int indexOfId(const QString& id) const;

signals:
    void countChanged();

private:
    ps2native::frontend::ContentKind kind_;
    std::vector<ps2native::frontend::LibraryEntry> entries_;
    QString active_;
};

class EntryFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged) // all | ps2 | issues | favorites
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged) // title | recent | region
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit EntryFilterModel(EntryListModel* source, QObject* parent = nullptr);
    [[nodiscard]] QString search() const { return search_; }
    void setSearch(const QString& value);
    [[nodiscard]] QString filter() const { return filter_; }
    void setFilter(const QString& value);
    [[nodiscard]] QString sortMode() const { return sort_; }
    void setSortMode(const QString& value);
    [[nodiscard]] int count() const { return rowCount(); }
    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE int rowOfId(const QString& id) const;

signals:
    void searchChanged();
    void filterChanged();
    void sortModeChanged();
    void countChanged();

protected:
    [[nodiscard]] bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    [[nodiscard]] bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    QString search_;
    QString filter_{QStringLiteral("all")};
    QString sort_{QStringLiteral("title")};
};
} // namespace ps2launcher
