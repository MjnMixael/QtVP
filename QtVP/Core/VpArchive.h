#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

struct VpEntry
{
    QString name;
    quint32 offset = 0;     // absolute position in the VP
    quint32 size = 0;       // stored size; smaller than the contents for LZ41 entries
    quint32 timestamp = 0;  // Unix time
    int folder = 0;
};

struct VpFolder
{
    QString name;           // empty for the root
    int parent = -1;
    std::vector<int> folders;
    std::vector<int> entries;
};

// Read-only view of a VP archive. Only the directory is read on open; entry
// bytes are read on demand. Safe to read from several threads.
class VpArchive
{
    Q_DECLARE_TR_FUNCTIONS(VpArchive)

public:
    static constexpr int RootFolder = 0;

    // Called before each entry and once at the end; return false to cancel
    using ProgressFn = std::function<bool(int done, int total, const QString& path)>;

    struct ExtractResult
    {
        int extracted = 0;
        bool canceled = false;
        QStringList errors;
    };

    VpArchive() = default;
    VpArchive(const VpArchive&) = delete;
    VpArchive& operator=(const VpArchive&) = delete;

    // On failure the archive is left closed and errorString() says why
    bool open(const QString& path);
    void close();

    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_path; }
    QString errorString() const { return m_error; }

    // Problems the engine would tolerate, found by the last successful open
    QStringList warnings() const { return m_warnings; }

    const std::vector<VpEntry>& entries() const { return m_entries; }
    const std::vector<VpFolder>& folders() const { return m_folders; }

    // Paths use '/' and are relative to relativeTo, which must be an ancestor
    QString folderPath(int folder, int relativeTo = RootFolder) const;
    QString entryPath(int entry, int relativeTo = RootFolder) const;

    // Every entry in the folder and its subfolders
    std::vector<int> entriesUnder(int folder) const;

    // These read the entry's header on first use and cache the result
    bool isCompressed(int entry) const;
    qint64 uncompressedSize(int entry) const;   // -1 if the entry is damaged

    // Returns the contents, decompressed if needed. Empty with *error set on failure.
    QByteArray readEntry(int entry, QString* error = nullptr) const;

    // Writes each entry under targetDir at its path relative to baseFolder,
    // restoring timestamps. Failures are collected and the rest carry on.
    ExtractResult extract(const std::vector<int>& entries, const QString& targetDir,
        int baseFolder = RootFolder, const ProgressFn& progress = {}) const;

private:
    using Sink = std::function<bool(const char* data, qint64 length)>;

    struct Lz41Info
    {
        quint32 size = 0;
        quint32 blockSize = 0;
        std::vector<quint32> offsets;   // relative to the entry; one more than the block count
    };

    enum class Kind { Plain, Lz41, Damaged };

    bool fail(const QString& message);
    int childFolder(int parent, const QString& name);
    bool readAt(qint64 pos, char* dst, qint64 length) const;
    Kind probeLocked(int entry, Lz41Info* info, QString* error) const;
    bool streamEntry(int entry, const Sink& sink, QString* error) const;
    bool extractOne(int entry, const QString& outPath, QString* error) const;
    QString safeRelativePath(int entry, int baseFolder) const;

    QString m_path;
    QString m_error;
    QStringList m_warnings;
    std::vector<VpEntry> m_entries;
    std::vector<VpFolder> m_folders;

    mutable QFile m_file;
    mutable QMutex m_mutex;
    mutable std::vector<qint64> m_sizeCache;   // -2 not probed yet, -1 damaged
    mutable std::vector<char> m_compressedCache;
};
