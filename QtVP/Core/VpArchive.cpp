#include "VpArchive.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <cstring>

#include "Dependencies/lz4/lz4.h"

namespace {

constexpr qint64 HeaderSize = 16;
constexpr qint64 DirEntrySize = 44;
constexpr int NameSize = 32;
constexpr qint64 Lz41TrailerSize = 12;

// Guards against garbage trailers asking for huge allocations; real files use 64 KB or so
constexpr quint32 MaxLz41BlockSize = 64 * 1024 * 1024;

quint32 readU32(const char* p)
{
    return qFromLittleEndian<quint32>(p);
}

// One path component that is safe to create on disk under the target folder
bool isSafeComponent(const QString& name)
{
    if (name.isEmpty() || name == "." || name == "..")
        return false;
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || QStringLiteral("<>:\"/\\|?*").contains(c))
            return false;
    }
    return true;
}

} // namespace

bool VpArchive::isSafeName(const QString& name)
{
    return isSafeComponent(name);
}

bool VpArchive::open(const QString& path)
{
    close();
    m_error.clear();
    m_warnings.clear();
    m_path = path;

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly))
        return fail(tr("Could not open the file: %1").arg(m_file.errorString()));

    const qint64 fileSize = m_file.size();
    if (fileSize < HeaderSize)
        return fail(tr("The file is too small to be a VP archive."));

    char header[HeaderSize];
    if (!readAt(0, header, HeaderSize))
        return fail(tr("Could not read the header: %1").arg(m_file.errorString()));
    if (std::memcmp(header, "VPVP", 4) != 0)
        return fail(tr("Not a VP archive (the header does not start with \"VPVP\")."));

    // The engine ignores the version field, so we do too
    const quint32 dirOffset = readU32(header + 8);
    quint32 count = readU32(header + 12);
    if (dirOffset < HeaderSize || dirOffset > fileSize) {
        return fail(tr("The directory offset (%1) is outside the file (%2 bytes).")
            .arg(dirOffset).arg(fileSize));
    }

    // Some old tools wrote a count one too high. The engine stops at the end of
    // the file and keeps what it read, so do the same and say so.
    const quint32 fits = quint32((fileSize - dirOffset) / DirEntrySize);
    if (count > fits) {
        m_warnings << tr("The header lists %1 directory entries but only %2 fit in the file; the rest were ignored.")
            .arg(count).arg(fits);
        count = fits;
    }

    QByteArray dir(qsizetype(count) * DirEntrySize, Qt::Uninitialized);
    if (!readAt(dirOffset, dir.data(), dir.size()))
        return fail(tr("Could not read the directory: %1").arg(m_file.errorString()));

    m_folders.push_back(VpFolder{});
    int current = RootFolder;

    for (quint32 i = 0; i < count; ++i) {
        const char* rec = dir.constData() + qsizetype(i) * DirEntrySize;
        const quint32 offset = readU32(rec);
        const quint32 size = readU32(rec + 4);
        const char* rawName = rec + 8;
        const quint32 timestamp = readU32(rec + 8 + NameSize);

        // The engine only honors 31 characters, whatever the writer put in the last byte
        const int nameLength = int(std::find(rawName, rawName + NameSize - 1, '\0') - rawName);
        const QString name = QString::fromLatin1(rawName, nameLength);

        // Size 0 opens a folder, or closes the current one when named ".."
        if (size == 0) {
            if (name == "..") {
                if (current != RootFolder)
                    current = m_folders[current].parent;
            } else {
                current = childFolder(current, name);
            }
            continue;
        }

        if (qint64(offset) + qint64(size) > fileSize) {
            const QString folder = current == RootFolder ? tr("the root") : folderPath(current);
            return fail(tr("Entry \"%1\" in %2 (offset %3, %4 bytes) runs past the end of the file (%5 bytes).")
                .arg(name, folder).arg(offset).arg(size).arg(fileSize));
        }

        VpEntry entry;
        entry.name = name;
        entry.offset = offset;
        entry.size = size;
        entry.timestamp = timestamp;
        entry.folder = current;
        m_folders[current].entries.push_back(int(m_entries.size()));
        m_entries.push_back(entry);
    }

    m_sizeCache.assign(m_entries.size(), -2);
    m_compressedCache.assign(m_entries.size(), 0);
    return true;
}

void VpArchive::close()
{
    QMutexLocker lock(&m_mutex);
    m_file.close();
    m_path.clear();
    m_entries.clear();
    m_folders.clear();
    m_sizeCache.clear();
    m_compressedCache.clear();
}

bool VpArchive::fail(const QString& message)
{
    close();
    m_error = message;
    m_warnings.clear();
    return false;
}

int VpArchive::childFolder(int parent, const QString& name)
{
    // Archives can open the same folder more than once; the engine merges them, case-insensitively
    for (const int child : m_folders[parent].folders) {
        if (m_folders[child].name.compare(name, Qt::CaseInsensitive) == 0)
            return child;
    }

    VpFolder folder;
    folder.name = name;
    folder.parent = parent;
    const int index = int(m_folders.size());
    m_folders.push_back(folder);
    m_folders[parent].folders.push_back(index);
    return index;
}

QString VpArchive::folderPath(int folder, int relativeTo) const
{
    QStringList parts;
    for (int f = folder; f > RootFolder && f != relativeTo; f = m_folders[f].parent)
        parts.prepend(m_folders[f].name);
    return parts.join('/');
}

QString VpArchive::entryPath(int entry, int relativeTo) const
{
    const VpEntry& e = m_entries[entry];
    const QString folder = folderPath(e.folder, relativeTo);
    return folder.isEmpty() ? e.name : folder + '/' + e.name;
}

std::vector<int> VpArchive::entriesUnder(int folder) const
{
    std::vector<int> result;
    std::vector<int> pending{ folder };
    while (!pending.empty()) {
        const VpFolder& f = m_folders[pending.back()];
        pending.pop_back();
        result.insert(result.end(), f.entries.begin(), f.entries.end());
        pending.insert(pending.end(), f.folders.rbegin(), f.folders.rend());
    }
    return result;
}

bool VpArchive::isCompressed(int entry) const
{
    uncompressedSize(entry);
    QMutexLocker lock(&m_mutex);
    return m_compressedCache[entry] != 0;
}

qint64 VpArchive::uncompressedSize(int entry) const
{
    QMutexLocker lock(&m_mutex);
    if (m_sizeCache[entry] == -2)
        probeLocked(entry, nullptr, nullptr);
    return m_sizeCache[entry];
}

bool VpArchive::readAt(qint64 pos, char* dst, qint64 length) const
{
    if (!m_file.seek(pos))
        return false;
    qint64 done = 0;
    while (done < length) {
        const qint64 n = m_file.read(dst + done, length - done);
        if (n <= 0)
            return false;
        done += n;
    }
    return true;
}

// Works out whether an entry is LZ41 and, if info is given, loads its offset table.
// Caller holds m_mutex.
VpArchive::Kind VpArchive::probeLocked(int entry, Lz41Info* info, QString* error) const
{
    const VpEntry& e = m_entries[entry];
    auto damaged = [&](const QString& message) {
        m_sizeCache[entry] = -1;
        if (error)
            *error = message;
        return Kind::Damaged;
    };

    char magic[4] = {};
    if (e.size >= 4 && !readAt(e.offset, magic, 4))
        return damaged(tr("Read error: %1").arg(m_file.errorString()));

    if (std::memcmp(magic, "LZ41", 4) != 0) {
        m_sizeCache[entry] = e.size;
        m_compressedCache[entry] = 0;
        return Kind::Plain;
    }
    m_compressedCache[entry] = 1;

    // LZ41 layout: "LZ41", blocks, offset table, then offset count, size and block size
    char trailer[Lz41TrailerSize];
    if (e.size < 4 + Lz41TrailerSize || !readAt(qint64(e.offset) + e.size - Lz41TrailerSize, trailer, Lz41TrailerSize))
        return damaged(tr("The LZ41 trailer is missing."));

    const quint32 numOffsets = readU32(trailer);
    const quint32 size = readU32(trailer + 4);
    const quint32 blockSize = readU32(trailer + 8);
    const qint64 tableStart = qint64(e.size) - Lz41TrailerSize - qint64(numOffsets) * 4;

    if (numOffsets < 1 || tableStart < 4)
        return damaged(tr("The LZ41 offset count (%1) does not fit the entry.").arg(numOffsets));
    if (blockSize <= 16 || blockSize > MaxLz41BlockSize)
        return damaged(tr("The LZ41 block size (%1) is invalid.").arg(blockSize));
    if (qint64(numOffsets - 1) * blockSize < size)
        return damaged(tr("The LZ41 block count is too small for %1 bytes.").arg(size));

    m_sizeCache[entry] = size;
    if (!info)
        return Kind::Lz41;

    QByteArray table(qsizetype(numOffsets) * 4, Qt::Uninitialized);
    if (!readAt(qint64(e.offset) + tableStart, table.data(), table.size()))
        return damaged(tr("Could not read the LZ41 offset table."));

    info->size = size;
    info->blockSize = blockSize;
    info->offsets.resize(numOffsets);
    quint32 previous = 4;
    for (quint32 i = 0; i < numOffsets; ++i) {
        const quint32 offset = readU32(table.constData() + qsizetype(i) * 4);
        if (offset < previous || offset > tableStart)
            return damaged(tr("The LZ41 offset table is corrupt."));
        info->offsets[i] = offset;
        previous = offset;
    }
    return Kind::Lz41;
}

bool VpArchive::streamEntry(int entry, const Sink& sink, QString* error) const
{
    QMutexLocker lock(&m_mutex);
    if (!m_file.isOpen()) {
        if (error)
            *error = tr("No archive is open.");
        return false;
    }

    const VpEntry& e = m_entries[entry];
    Lz41Info info;
    const Kind kind = probeLocked(entry, &info, error);
    if (kind == Kind::Damaged)
        return false;

    if (kind == Kind::Plain) {
        constexpr qint64 ChunkSize = 1024 * 1024;
        std::vector<char> buffer(size_t(std::min<qint64>(ChunkSize, e.size)));
        for (qint64 done = 0; done < e.size;) {
            const qint64 n = std::min<qint64>(ChunkSize, e.size - done);
            if (!readAt(qint64(e.offset) + done, buffer.data(), n)) {
                if (error)
                    *error = tr("Read error: %1").arg(m_file.errorString());
                return false;
            }
            if (!sink(buffer.data(), n))
                return false;
            done += n;
        }
        return true;
    }

    // Blocks are compressed independently, which is what lets the engine seek within them
    std::vector<char> src;
    std::vector<char> dst(info.blockSize);
    qint64 remaining = info.size;
    for (size_t block = 0; remaining > 0; ++block) {
        if (block + 1 >= info.offsets.size()) {
            if (error)
                *error = tr("The LZ41 data ends early.");
            return false;
        }

        const quint32 compressed = info.offsets[block + 1] - info.offsets[block];
        src.resize(compressed);
        if (!readAt(qint64(e.offset) + info.offsets[block], src.data(), compressed)) {
            if (error)
                *error = tr("Read error: %1").arg(m_file.errorString());
            return false;
        }

        const int expected = int(std::min<qint64>(info.blockSize, remaining));
        const int decoded = LZ4_decompress_safe(src.data(), dst.data(), int(compressed), int(info.blockSize));
        if (decoded < expected) {
            if (error)
                *error = tr("LZ41 block %1 is corrupt.").arg(block);
            return false;
        }

        if (!sink(dst.data(), expected))
            return false;
        remaining -= expected;
    }
    return true;
}

bool VpArchive::streamRaw(int entry, const Sink& sink, QString* error) const
{
    QMutexLocker lock(&m_mutex);
    if (!m_file.isOpen()) {
        if (error)
            *error = tr("No archive is open.");
        return false;
    }

    const VpEntry& e = m_entries[entry];
    constexpr qint64 ChunkSize = 1024 * 1024;
    std::vector<char> buffer(size_t(std::min<qint64>(ChunkSize, e.size)));
    for (qint64 done = 0; done < e.size;) {
        const qint64 n = std::min<qint64>(ChunkSize, e.size - done);
        if (!readAt(qint64(e.offset) + done, buffer.data(), n)) {
            if (error)
                *error = tr("Read error: %1").arg(m_file.errorString());
            return false;
        }
        if (!sink(buffer.data(), n))
            return false;
        done += n;
    }
    return true;
}

QByteArray VpArchive::readEntry(int entry, QString* error) const
{
    QByteArray result;
    const bool ok = streamEntry(entry, [&](const char* data, qint64 length) {
        result.append(data, length);
        return true;
    }, error);
    return ok ? result : QByteArray();
}

VpArchive::ExtractResult VpArchive::extract(const std::vector<int>& entries, const QString& targetDir,
    int baseFolder, const ProgressFn& progress) const
{
    ExtractResult result;
    const QDir target(targetDir);
    const int total = int(entries.size());

    for (int i = 0; i < total; ++i) {
        const int entry = entries[i];
        const QString relative = safeRelativePath(entry, baseFolder);

        if (progress && !progress(i, total, relative.isEmpty() ? m_entries[entry].name : relative)) {
            result.canceled = true;
            return result;
        }

        if (relative.isEmpty()) {
            result.errors << tr("%1: the name is not a valid file name, skipped.").arg(entryPath(entry));
            continue;
        }

        QString error;
        if (extractOne(entry, target.filePath(relative), &error))
            ++result.extracted;
        else
            result.errors << tr("%1: %2").arg(relative, error);
    }

    if (progress)
        progress(total, total, QString());
    return result;
}

bool VpArchive::extractOne(int entry, const QString& outPath, QString* error) const
{
    const QString dir = QFileInfo(outPath).absolutePath();
    if (!QDir().mkpath(dir)) {
        *error = tr("Could not create the folder %1.").arg(QDir::toNativeSeparators(dir));
        return false;
    }

    // QSaveFile leaves any existing file alone unless the whole entry is written
    QSaveFile out(outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        *error = out.errorString();
        return false;
    }

    QString writeError;
    const bool ok = streamEntry(entry, [&](const char* data, qint64 length) {
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

    const quint32 timestamp = m_entries[entry].timestamp;
    if (timestamp != 0) {
        QFile written(outPath);
        if (written.open(QIODevice::ReadWrite | QIODevice::ExistingOnly))
            written.setFileTime(QDateTime::fromSecsSinceEpoch(timestamp), QFileDevice::FileModificationTime);
    }
    return true;
}

// Empty if any component could escape the target folder or is not a legal file name
QString VpArchive::safeRelativePath(int entry, int baseFolder) const
{
    QStringList parts{ m_entries[entry].name };
    int folder = m_entries[entry].folder;
    for (; folder > RootFolder && folder != baseFolder; folder = m_folders[folder].parent)
        parts.prepend(m_folders[folder].name);

    for (const QString& part : parts) {
        if (!isSafeComponent(part))
            return QString();
    }
    return parts.join('/');
}
