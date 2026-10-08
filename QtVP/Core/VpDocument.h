#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

#include "Core/VpArchive.h"

// Where a file's bytes come from: an entry in an open VP, or a file on disk.
// A plain value, so a worker thread can read through it without the document.
struct VpFileSource
{
    const VpArchive* archive = nullptr;
    int entry = -1;
    QString diskPath;

    static VpFileSource fromEntry(const VpArchive* archive, int entry);
    static VpFileSource fromDisk(const QString& path);

    bool fromArchive() const { return archive && entry >= 0; }

    // The contents, decompressed if needed
    QByteArray read(QString* error) const;
    bool stream(const VpArchive::Sink& sink, QString* error) const;
};

struct VpDocFile
{
    QString name;
    int folder = 0;
    quint32 timestamp = 0;  // Unix time
    qint64 size = 0;        // as stored in the VP, or on disk
    VpFileSource source;
    bool removed = false;
};

struct VpDocFolder
{
    QString name;           // empty for the root
    int parent = -1;
    std::vector<int> folders;   // children that are not removed
    std::vector<int> files;
    bool removed = false;
};

// A VP as it is being edited: the opened archive's contents plus any changes, none of
// which touch the file until it is saved. Edits only change fields or flag items as
// removed, so ids stay valid for the document's life and every edit can be reversed
// by making the same call with the old value. Files and folders mirrored from the
// archive keep the archive's entry and folder indices as their ids.
class VpDocument
{
    Q_DECLARE_TR_FUNCTIONS(VpDocument)

public:
    static constexpr int RootFolder = 0;
    using ProgressFn = VpArchive::ProgressFn;
    using ExtractResult = VpArchive::ExtractResult;

    // A new, empty VP
    VpDocument();

    // Mirrors an opened archive and keeps it for reading
    explicit VpDocument(std::unique_ptr<VpArchive> archive);

    VpDocument(const VpDocument&) = delete;
    VpDocument& operator=(const VpDocument&) = delete;

    // Null for a new VP. Saving over the VP must close it first (see VpWriter).
    VpArchive* archive() const { return m_archive.get(); }
    QString path() const { return m_archive ? m_archive->path() : QString(); }

    const std::vector<VpDocFile>& files() const { return m_files; }
    const std::vector<VpDocFolder>& folders() const { return m_folders; }

    // Paths use '/' and are relative to relativeTo, which must be an ancestor
    QString folderPath(int folder, int relativeTo = RootFolder) const;
    QString filePath(int file, int relativeTo = RootFolder) const;

    // Files in the folder and its subfolders, skipping anything removed
    std::vector<int> filesUnder(int folder) const;
    int fileCount() const;

    // Case-insensitive, like the engine; -1 if there is none
    int findFile(int folder, const QString& name) const;
    int findFolder(int parent, const QString& name) const;

    // False if the folder or any folder above it is removed
    bool isFolderLive(int folder) const;

    // Edits; each returns or takes ids
    int addFile(int folder, const QString& name, const VpFileSource& source, qint64 size, quint32 timestamp);
    int addFolder(int parent, const QString& name);
    void setFileRemoved(int file, bool removed);
    void setFolderRemoved(int folder, bool removed);
    void renameFile(int file, const QString& name);
    void renameFolder(int folder, const QString& name);
    void moveFile(int file, int folder);
    void moveFolder(int folder, int parent);
    void setFileSource(int file, const VpFileSource& source, qint64 size, quint32 timestamp);

    // Writes each file under targetDir at its path relative to baseFolder, restoring
    // timestamps. Failures are collected and the rest carry on.
    ExtractResult extract(const std::vector<int>& files, const QString& targetDir,
        int baseFolder = RootFolder, const ProgressFn& progress = {}) const;

private:
    bool extractOne(int file, const QString& outPath, QString* error) const;
    QString safeRelativePath(int file, int baseFolder) const;

    std::unique_ptr<VpArchive> m_archive;
    std::vector<VpDocFile> m_files;
    std::vector<VpDocFolder> m_folders;
};
