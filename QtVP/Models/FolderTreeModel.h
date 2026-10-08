#pragma once

#include <QAbstractItemModel>
#include <QIcon>

#include <vector>

class VpDocument;

// The VP's folders, alphabetized, under an <All files> node that comes first
class FolderTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    static constexpr int AllFiles = -1;
    static constexpr int NoFolder = -2;

    explicit FolderTreeModel(QObject* parent = nullptr);

    // The document must outlive the model or be replaced with nullptr first.
    // Call again with the same document after edits to rebuild.
    void setDocument(const VpDocument* document);

    // AllFiles, a folder index, or NoFolder for an invalid index
    int folderAt(const QModelIndex& index) const;
    QModelIndex allFilesIndex() const;

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    const VpDocument* m_document = nullptr;
    std::vector<std::vector<int>> m_children;   // sorted subfolders of each folder
    std::vector<int> m_row;                     // each folder's row under its parent
    int m_fileCount = 0;
    QIcon m_folderIcon;
    QIcon m_allFilesIcon;
};
