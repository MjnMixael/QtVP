#include "VpDocument.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace {

void detach(std::vector<int>& list, int id)
{
    list.erase(std::remove(list.begin(), list.end(), id), list.end());
}

void attach(std::vector<int>& list, int id)
{
    if (std::find(list.begin(), list.end(), id) == list.end())
        list.push_back(id);
}

// A read-only window onto one plainly stored entry. Unbuffered, so pos() inside
// readData() is where the read starts.
class EntryDevice : public QIODevice
{
public:
    EntryDevice(const VpArchive* archive, int entry, QObject* parent)
        : QIODevice(parent)
        , m_archive(archive)
        , m_entry(entry)
        , m_size(archive->entries()[entry].size)
    {
    }

    bool isSequential() const override { return false; }
    qint64 size() const override { return m_size; }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        return m_archive->readRaw(m_entry, pos(), data, maxSize);
    }

    qint64 writeData(const char*, qint64) override { return -1; }

private:
    const VpArchive* m_archive;
    int m_entry;
    qint64 m_size;
};

} // namespace

VpFileSource VpFileSource::fromEntry(const VpArchive* archive, int entry)
{
    VpFileSource source;
    source.archive = archive;
    source.entry = entry;
    return source;
}

VpFileSource VpFileSource::fromDisk(const QString& path)
{
    VpFileSource source;
    source.diskPath = path;
    return source;
}

bool VpFileSource::stream(const VpArchive::Sink& sink, QString* error) const
{
    if (fromArchive())
        return archive->streamEntry(entry, sink, error);

    QFile file(diskPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = VpDocument::tr("Could not read %1: %2").arg(QDir::toNativeSeparators(diskPath), file.errorString());
        return false;
    }

    constexpr qint64 ChunkSize = 1024 * 1024;
    QByteArray buffer(ChunkSize, Qt::Uninitialized);
    while (!file.atEnd()) {
        const qint64 n = file.read(buffer.data(), ChunkSize);
        if (n < 0) {
            if (error)
                *error = VpDocument::tr("Could not read %1: %2").arg(QDir::toNativeSeparators(diskPath), file.errorString());
            return false;
        }
        if (n > 0 && !sink(buffer.constData(), n))
            return false;
    }
    return true;
}

QByteArray VpFileSource::readHead(qint64 length, QString* error) const
{
    QByteArray head;
    stream([&](const char* data, qint64 n) {
        head.append(data, std::min<qint64>(n, length - head.size()));
        return head.size() < length;
    }, error);
    return head;
}

QIODevice* VpFileSource::openDevice(QObject* parent, QString* error) const
{
    if (!fromArchive()) {
        auto* file = new QFile(diskPath, parent);
        if (!file->open(QIODevice::ReadOnly)) {
            if (error)
                *error = file->errorString();
            delete file;
            return nullptr;
        }
        return file;
    }

    if (!archive->isCompressed(entry)) {
        auto* device = new EntryDevice(archive, entry, parent);
        device->open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        return device;
    }

    QString readError;
    const QByteArray data = archive->readEntry(entry, &readError);
    if (!readError.isEmpty()) {
        if (error)
            *error = readError;
        return nullptr;
    }
    auto* buffer = new QBuffer(parent);
    buffer->setData(data);
    buffer->open(QIODevice::ReadOnly);
    return buffer;
}

QByteArray VpFileSource::read(QString* error) const
{
    if (fromArchive())
        return archive->readEntry(entry, error);

    QByteArray result;
    const bool ok = stream([&](const char* data, qint64 length) {
        result.append(data, length);
        return true;
    }, error);
    return ok ? result : QByteArray();
}

VpDocument::VpDocument()
{
    m_folders.push_back(VpDocFolder{});
}

VpDocument::VpDocument(std::unique_ptr<VpArchive> archive)
    : m_archive(std::move(archive))
{
    // Same shape and the same indices as the archive
    for (const VpFolder& f : m_archive->folders()) {
        VpDocFolder folder;
        folder.name = f.name;
        folder.parent = f.parent;
        folder.folders = f.folders;
        folder.files = f.entries;
        m_folders.push_back(folder);
    }

    const std::vector<VpEntry>& entries = m_archive->entries();
    m_files.reserve(entries.size());
    for (int i = 0; i < int(entries.size()); ++i) {
        VpDocFile file;
        file.name = entries[i].name;
        file.folder = entries[i].folder;
        file.timestamp = entries[i].timestamp;
        file.size = entries[i].size;
        file.source = VpFileSource::fromEntry(m_archive.get(), i);
        m_files.push_back(file);
    }
}

QString VpDocument::folderPath(int folder, int relativeTo) const
{
    QStringList parts;
    for (int f = folder; f > RootFolder && f != relativeTo; f = m_folders[f].parent)
        parts.prepend(m_folders[f].name);
    return parts.join('/');
}

QString VpDocument::filePath(int file, int relativeTo) const
{
    const VpDocFile& f = m_files[file];
    const QString folder = folderPath(f.folder, relativeTo);
    return folder.isEmpty() ? f.name : folder + '/' + f.name;
}

std::vector<int> VpDocument::filesUnder(int folder) const
{
    std::vector<int> result;
    std::vector<int> pending{ folder };
    while (!pending.empty()) {
        const VpDocFolder& f = m_folders[pending.back()];
        pending.pop_back();
        result.insert(result.end(), f.files.begin(), f.files.end());
        pending.insert(pending.end(), f.folders.rbegin(), f.folders.rend());
    }
    return result;
}

int VpDocument::fileCount() const
{
    return int(filesUnder(RootFolder).size());
}

int VpDocument::findFile(int folder, const QString& name) const
{
    for (const int file : m_folders[folder].files) {
        if (m_files[file].name.compare(name, Qt::CaseInsensitive) == 0)
            return file;
    }
    return -1;
}

int VpDocument::findFolder(int parent, const QString& name) const
{
    for (const int folder : m_folders[parent].folders) {
        if (m_folders[folder].name.compare(name, Qt::CaseInsensitive) == 0)
            return folder;
    }
    return -1;
}

bool VpDocument::isFolderLive(int folder) const
{
    for (int f = folder; f >= 0; f = m_folders[f].parent) {
        if (m_folders[f].removed)
            return false;
    }
    return true;
}

int VpDocument::addFile(int folder, const QString& name, const VpFileSource& source, qint64 size, quint32 timestamp)
{
    VpDocFile file;
    file.name = name;
    file.folder = folder;
    file.timestamp = timestamp;
    file.size = size;
    file.source = source;
    const int id = int(m_files.size());
    m_files.push_back(file);
    m_folders[folder].files.push_back(id);
    return id;
}

int VpDocument::addFolder(int parent, const QString& name)
{
    VpDocFolder folder;
    folder.name = name;
    folder.parent = parent;
    const int id = int(m_folders.size());
    m_folders.push_back(folder);
    m_folders[parent].folders.push_back(id);
    return id;
}

void VpDocument::setFileRemoved(int file, bool removed)
{
    VpDocFile& f = m_files[file];
    f.removed = removed;
    if (removed)
        detach(m_folders[f.folder].files, file);
    else
        attach(m_folders[f.folder].files, file);
}

// The folder's contents stay linked to it, so restoring it brings them back too
void VpDocument::setFolderRemoved(int folder, bool removed)
{
    VpDocFolder& f = m_folders[folder];
    f.removed = removed;
    if (removed)
        detach(m_folders[f.parent].folders, folder);
    else
        attach(m_folders[f.parent].folders, folder);
}

void VpDocument::renameFile(int file, const QString& name)
{
    m_files[file].name = name;
}

void VpDocument::renameFolder(int folder, const QString& name)
{
    m_folders[folder].name = name;
}

void VpDocument::moveFile(int file, int folder)
{
    VpDocFile& f = m_files[file];
    if (!f.removed) {
        detach(m_folders[f.folder].files, file);
        attach(m_folders[folder].files, file);
    }
    f.folder = folder;
}

void VpDocument::moveFolder(int folder, int parent)
{
    VpDocFolder& f = m_folders[folder];
    if (!f.removed) {
        detach(m_folders[f.parent].folders, folder);
        attach(m_folders[parent].folders, folder);
    }
    f.parent = parent;
}

void VpDocument::setFileSource(int file, const VpFileSource& source, qint64 size, quint32 timestamp)
{
    VpDocFile& f = m_files[file];
    f.source = source;
    f.size = size;
    f.timestamp = timestamp;
}

VpDocument::ExtractResult VpDocument::extract(const std::vector<int>& files, const QString& targetDir,
    int baseFolder, const ProgressFn& progress) const
{
    ExtractResult result;
    const QDir target(targetDir);
    const int total = int(files.size());

    for (int i = 0; i < total; ++i) {
        const int file = files[i];
        const QString relative = safeRelativePath(file, baseFolder);

        if (progress && !progress(i, total, relative.isEmpty() ? m_files[file].name : relative)) {
            result.canceled = true;
            return result;
        }

        if (relative.isEmpty()) {
            result.errors << tr("%1: the name is not a valid file name, skipped.").arg(filePath(file));
            continue;
        }

        QString error;
        if (extractOne(file, target.filePath(relative), &error))
            ++result.extracted;
        else
            result.errors << tr("%1: %2").arg(relative, error);
    }

    if (progress)
        progress(total, total, QString());
    return result;
}

bool VpDocument::extractOne(int file, const QString& outPath, QString* error) const
{
    const QString dir = QFileInfo(outPath).absolutePath();
    if (!QDir().mkpath(dir)) {
        *error = tr("Could not create the folder %1.").arg(QDir::toNativeSeparators(dir));
        return false;
    }

    // QSaveFile leaves any existing file alone unless the whole file is written
    QSaveFile out(outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        *error = out.errorString();
        return false;
    }

    QString writeError;
    const bool ok = m_files[file].source.stream([&](const char* data, qint64 length) {
        if (out.write(data, length) == length)
            return true;
        writeError = out.errorString();
        return false;
    }, error);

    if (!ok) {
        out.cancelWriting();
        if (!writeError.isEmpty())
            *error = writeError;
        return false;
    }
    if (!out.commit()) {
        *error = out.errorString();
        return false;
    }

    const quint32 timestamp = m_files[file].timestamp;
    if (timestamp != 0) {
        QFile written(outPath);
        if (written.open(QIODevice::ReadWrite | QIODevice::ExistingOnly))
            written.setFileTime(QDateTime::fromSecsSinceEpoch(timestamp), QFileDevice::FileModificationTime);
    }
    return true;
}

// Empty if any part could escape the target folder or is not a legal file name
QString VpDocument::safeRelativePath(int file, int baseFolder) const
{
    QStringList parts{ m_files[file].name };
    for (int folder = m_files[file].folder; folder > RootFolder && folder != baseFolder; folder = m_folders[folder].parent)
        parts.prepend(m_folders[folder].name);

    for (const QString& part : parts) {
        if (!VpArchive::isSafeName(part))
            return QString();
    }
    return parts.join('/');
}
