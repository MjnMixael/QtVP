#include "VpWriter.h"

#include <QDir>
#include <QHash>
#include <QLocale>
#include <QtEndian>

#include <cstring>
#include <functional>

namespace {

constexpr int NameSize = 32;            // 31 characters and a terminator
constexpr qint64 MaxVpSize = 0xFFFFFFFFLL;

struct Record
{
    quint32 offset = 0;
    quint32 size = 0;
    quint32 timestamp = 0;
    QByteArray name;
};

void appendU32(QByteArray& out, quint32 value)
{
    char bytes[4];
    qToLittleEndian(value, bytes);
    out.append(bytes, 4);
}

// Empty if the name can go in a VP as it is
QString nameProblem(const QString& name)
{
    if (!VpArchive::isSafeName(name))
        return VpWriter::tr("not a valid file name");
    for (const QChar c : name) {
        if (c.unicode() > 0xFF)
            return VpWriter::tr("has characters a VP cannot store");
    }
    if (name.size() > NameSize - 1)
        return VpWriter::tr("is %1 characters long; VPs allow %2").arg(name.size()).arg(NameSize - 1);
    return QString();
}

} // namespace

VpWriter::Problems VpWriter::check(const VpDocument& document)
{
    Problems problems;
    qint64 total = 16;

    std::function<void(int)> checkFolder = [&](int id) {
        const VpDocFolder& folder = document.folders()[id];
        const QString where = id == VpDocument::RootFolder ? QString() : document.folderPath(id) + '/';

        if (id != VpDocument::RootFolder) {
            const QString problem = nameProblem(folder.name);
            if (!problem.isEmpty())
                problems.errors << tr("Folder %1: %2").arg(document.folderPath(id), problem);
            total += 2 * 44;    // the folder record and its ".."
        }

        QHash<QString, QString> seen;
        auto checkCase = [&](const QString& name) {
            const QString key = name.toLower();
            const auto other = seen.constFind(key);
            if (other != seen.constEnd())
                problems.warnings << tr("%1%2 and %1%3 are the same name to the engine, which will only see one").arg(where, other.value(), name);
            else
                seen.insert(key, name);
        };

        for (const int file : folder.files) {
            const VpDocFile& f = document.files()[file];
            const QString problem = nameProblem(f.name);
            if (!problem.isEmpty())
                problems.errors << tr("%1%2: %3").arg(where, f.name, problem);
            if (f.size == 0)
                problems.errors << tr("%1%2 is empty; a VP cannot hold an empty file").arg(where, f.name);
            checkCase(f.name);
            total += f.size + 44;
        }
        for (const int sub : folder.folders) {
            checkCase(document.folders()[sub].name);
            checkFolder(sub);
        }
    };
    checkFolder(VpDocument::RootFolder);

    if (total > MaxVpSize) {
        problems.errors << tr("The VP would be %1, over the format's 4 GB limit.")
            .arg(QLocale().formattedDataSize(total));
    }
    return problems;
}

VpWriter::VpWriter(const QString& path)
    : m_file(path)
{
}

// Layout: header, then every file's bytes from offset 16, then the directory. In the
// directory each folder is a size 0 record named after it, then its files, then its
// subfolders, then a ".." record. Folder records get offset 0 and time 0, the most
// common choice among existing VPs; the engine reads neither.
bool VpWriter::write(const VpDocument& document, const ProgressFn& progress)
{
    m_canceled = false;
    if (!m_file.open(QIODevice::WriteOnly)) {
        m_error = tr("Could not create %1: %2").arg(QDir::toNativeSeparators(m_file.fileName()), m_file.errorString());
        return false;
    }

    const QByteArray header(16, '\0');
    if (m_file.write(header) != header.size()) {
        m_error = m_file.errorString();
        cancel();
        return false;
    }

    std::vector<Record> records;
    qint64 pos = header.size();
    int done = 0;
    const int total = document.fileCount();

    std::function<bool(int)> writeFolder = [&](int id) -> bool {
        const VpDocFolder& folder = document.folders()[id];
        if (id != VpDocument::RootFolder)
            records.push_back({ 0, 0, 0, folder.name.toLatin1() });

        for (const int file : folder.files) {
            const VpDocFile& f = document.files()[file];
            const QString path = document.filePath(file);
            if (progress && !progress(done, total, path)) {
                m_canceled = true;
                return false;
            }

            const qint64 start = pos;
            QString error;
            QString writeError;
            const VpArchive::Sink sink = [&](const char* data, qint64 length) {
                if (pos + length > MaxVpSize) {
                    writeError = tr("the VP would go over the format's 4 GB limit");
                    return false;
                }
                if (m_file.write(data, length) != length) {
                    writeError = m_file.errorString();
                    return false;
                }
                pos += length;
                return true;
            };

            // Entries from the open VP go across exactly as stored
            const bool ok = f.source.fromArchive() ? f.source.archive->streamRaw(f.source.entry, sink, &error)
                                                   : f.source.stream(sink, &error);
            if (!ok) {
                m_error = tr("%1: %2").arg(path, writeError.isEmpty() ? error : writeError);
                return false;
            }
            if (pos == start) {
                m_error = tr("%1 is empty; a VP cannot hold an empty file.").arg(path);
                return false;
            }

            records.push_back({ quint32(start), quint32(pos - start), f.timestamp, f.name.toLatin1() });
            ++done;
        }

        for (const int sub : folder.folders) {
            if (!writeFolder(sub))
                return false;
        }

        if (id != VpDocument::RootFolder)
            records.push_back({ 0, 0, 0, QByteArrayLiteral("..") });
        return true;
    };

    if (!writeFolder(VpDocument::RootFolder)) {
        cancel();
        return false;
    }

    const qint64 dirOffset = pos;
    QByteArray dir;
    dir.reserve(qsizetype(records.size()) * 44);
    for (const Record& r : records) {
        appendU32(dir, r.offset);
        appendU32(dir, r.size);
        QByteArray name = r.name.left(NameSize - 1);
        name.append(QByteArray(NameSize - name.size(), '\0'));
        dir.append(name);
        appendU32(dir, r.timestamp);
    }

    QByteArray head("VPVP", 4);
    appendU32(head, 2);
    appendU32(head, quint32(dirOffset));
    appendU32(head, quint32(records.size()));

    if (m_file.write(dir) != dir.size() || !m_file.seek(0) || m_file.write(head) != head.size()) {
        m_error = m_file.errorString();
        cancel();
        return false;
    }

    if (progress)
        progress(total, total, QString());
    return true;
}

bool VpWriter::commit()
{
    if (!m_file.commit()) {
        m_error = tr("Could not save %1: %2").arg(QDir::toNativeSeparators(m_file.fileName()), m_file.errorString());
        return false;
    }
    return true;
}

void VpWriter::cancel()
{
    m_file.cancelWriting();
    m_file.commit();    // with writing canceled this just discards the temporary file
}
