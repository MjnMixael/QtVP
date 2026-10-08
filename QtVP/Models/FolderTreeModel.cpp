#include "FolderTreeModel.h"

#include <QApplication>
#include <QStyle>

#include <algorithm>

#include "Core/VpArchive.h"

// Internal ids are the folder index + 1, so 0 can mean <All files>
namespace {
constexpr quintptr AllFilesId = 0;
}

FolderTreeModel::FolderTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    QStyle* s = QApplication::style();
    m_folderIcon = s->standardIcon(QStyle::SP_DirIcon);
    m_allFilesIcon = s->standardIcon(QStyle::SP_DirOpenIcon);
}

void FolderTreeModel::setArchive(const VpArchive* archive)
{
    beginResetModel();
    m_archive = archive;
    m_children.clear();
    m_row.clear();

    if (m_archive) {
        const auto& folders = m_archive->folders();
        m_children.resize(folders.size());
        m_row.resize(folders.size(), 0);

        for (size_t f = 0; f < folders.size(); ++f) {
            std::vector<int> children = folders[f].folders;
            std::sort(children.begin(), children.end(), [&](int a, int b) {
                return folders[a].name.compare(folders[b].name, Qt::CaseInsensitive) < 0;
            });

            // Top-level folders sit below <All files>
            const int first = f == size_t(VpArchive::RootFolder) ? 1 : 0;
            for (size_t i = 0; i < children.size(); ++i)
                m_row[children[i]] = first + int(i);
            m_children[f] = std::move(children);
        }
    }
    endResetModel();
}

int FolderTreeModel::folderAt(const QModelIndex& index) const
{
    if (!index.isValid() || !m_archive)
        return NoFolder;
    return index.internalId() == AllFilesId ? AllFiles : int(index.internalId() - 1);
}

QModelIndex FolderTreeModel::allFilesIndex() const
{
    return m_archive ? createIndex(0, 0, AllFilesId) : QModelIndex();
}

QModelIndex FolderTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    if (!parent.isValid()) {
        if (row == 0)
            return createIndex(0, 0, AllFilesId);
        return createIndex(row, 0, quintptr(m_children[VpArchive::RootFolder][row - 1] + 1));
    }

    const int folder = folderAt(parent);
    return createIndex(row, 0, quintptr(m_children[folder][row] + 1));
}

QModelIndex FolderTreeModel::parent(const QModelIndex& child) const
{
    const int folder = folderAt(child);
    if (folder < 0)
        return {};

    const int parentFolder = m_archive->folders()[folder].parent;
    if (parentFolder == VpArchive::RootFolder)
        return {};
    return createIndex(m_row[parentFolder], 0, quintptr(parentFolder + 1));
}

int FolderTreeModel::rowCount(const QModelIndex& parent) const
{
    if (!m_archive || parent.column() > 0)
        return 0;
    if (!parent.isValid())
        return 1 + int(m_children[VpArchive::RootFolder].size());

    const int folder = folderAt(parent);
    return folder < 0 ? 0 : int(m_children[folder].size());
}

int FolderTreeModel::columnCount(const QModelIndex&) const
{
    return 1;
}

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const
{
    const int folder = folderAt(index);
    if (folder == NoFolder)
        return {};

    if (role == Qt::DisplayRole) {
        if (folder == AllFiles)
            return tr("<All files> (%1)").arg(m_archive->entries().size());
        const VpFolder& f = m_archive->folders()[folder];
        const QString name = f.name.isEmpty() ? tr("(unnamed)") : f.name;
        return QString("%1 (%2)").arg(name).arg(f.entries.size());
    }
    if (role == Qt::DecorationRole)
        return folder == AllFiles ? m_allFilesIcon : m_folderIcon;
    if (role == Qt::ToolTipRole && folder >= 0)
        return tr("%n file(s) including subfolders", nullptr, int(m_archive->entriesUnder(folder).size()));
    return {};
}
