#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QUrl>

#include <functional>
#include <vector>

class VpDocument;

// A flat list of VP entries: the files in one folder, or every file
class FileListModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { NameColumn, PathColumn, TypeColumn, SizeColumn, DateColumn, ColumnCount };

    // Raw values for sorting (sizes and timestamps as numbers)
    static constexpr int SortRole = Qt::UserRole;

    // Extracts the dragged entries somewhere Explorer can copy them from
    using DragProvider = std::function<QList<QUrl>(const std::vector<int>& entries)>;

    explicit FileListModel(QObject* parent = nullptr);

    // The document must outlive the model or be replaced with nullptr first.
    // Call again with the same document after edits to rebuild.
    void setDocument(const VpDocument* document);
    void setEntries(std::vector<int> entries);
    void setDragProvider(DragProvider provider) { m_dragProvider = std::move(provider); }

    int entryAt(const QModelIndex& index) const;

    // Row of each listed entry, for restoring a selection after a rebuild
    QHash<int, int> rowLookup() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

private:
    const VpDocument* m_document = nullptr;
    std::vector<int> m_entries;
    DragProvider m_dragProvider;
};
