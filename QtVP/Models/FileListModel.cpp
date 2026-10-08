#include "FileListModel.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QMimeData>
#include <QStyle>

#include "Core/VpArchive.h"

namespace {

QString typeName(const QString& fileName)
{
    const int dot = fileName.lastIndexOf('.');
    if (dot < 0 || dot == fileName.size() - 1)
        return FileListModel::tr("File");
    return FileListModel::tr("%1 File").arg(fileName.mid(dot + 1).toUpper());
}

} // namespace

FileListModel::FileListModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    m_fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
}

void FileListModel::setArchive(const VpArchive* archive)
{
    beginResetModel();
    m_archive = archive;
    m_entries.clear();
    endResetModel();
}

void FileListModel::setEntries(std::vector<int> entries)
{
    beginResetModel();
    m_entries = m_archive ? std::move(entries) : std::vector<int>();
    endResetModel();
}

int FileListModel::entryAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() >= int(m_entries.size()))
        return -1;
    return m_entries[index.row()];
}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

int FileListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant FileListModel::data(const QModelIndex& index, int role) const
{
    const int entry = entryAt(index);
    if (entry < 0)
        return {};
    const VpEntry& e = m_archive->entries()[entry];

    if (role == Qt::DisplayRole || role == SortRole) {
        switch (index.column()) {
        case NameColumn:
            return e.name;
        case PathColumn:
            return QDir::toNativeSeparators(m_archive->folderPath(e.folder));
        case TypeColumn:
            return typeName(e.name);
        case SizeColumn:
            return role == SortRole ? QVariant(qint64(e.size)) : QVariant(QLocale().toString(qint64(e.size)));
        case DateColumn:
            if (role == SortRole)
                return qint64(e.timestamp);
            if (e.timestamp == 0)
                return QString();
            return QLocale().toString(QDateTime::fromSecsSinceEpoch(e.timestamp), QLocale::ShortFormat);
        }
    }
    if (role == Qt::DecorationRole && index.column() == NameColumn)
        return m_fileIcon;
    if (role == Qt::TextAlignmentRole && index.column() == SizeColumn)
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

QVariant FileListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::TextAlignmentRole && section == SizeColumn)
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};

    switch (section) {
    case NameColumn: return tr("Name");
    case PathColumn: return tr("Folder");
    case TypeColumn: return tr("Type");
    case SizeColumn: return tr("Size");
    case DateColumn: return tr("Date & Time");
    }
    return {};
}

Qt::ItemFlags FileListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled | Qt::ItemNeverHasChildren;
}

QStringList FileListModel::mimeTypes() const
{
    return { QStringLiteral("text/uri-list") };
}

// Called when a drag starts; the files are extracted right here so Explorer gets real paths
QMimeData* FileListModel::mimeData(const QModelIndexList& indexes) const
{
    if (!m_dragProvider)
        return nullptr;

    std::vector<int> entries;
    for (const QModelIndex& index : indexes) {
        if (index.column() == NameColumn && entryAt(index) >= 0)
            entries.push_back(entryAt(index));
    }

    const QList<QUrl> urls = m_dragProvider(entries);
    if (urls.isEmpty())
        return nullptr;

    auto* mime = new QMimeData;
    mime->setUrls(urls);
    return mime;
}
