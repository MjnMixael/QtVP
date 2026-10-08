#pragma once

#include <QCoreApplication>
#include <QSaveFile>
#include <QString>
#include <QStringList>

#include "Core/VpDocument.h"

// Writes a VpDocument out as a VP. Files from the open VP are copied exactly as
// stored, so LZ41 entries stay compressed; files from disk are copied as they are.
//
// Saving happens in two steps so a failed save never damages anything: write()
// streams everything into a temporary file beside the target, and commit() swaps it
// in. Saving over the document's own VP reads from it during write(), so close the
// archive between write() and commit(); Windows will not replace an open file.
class VpWriter
{
    Q_DECLARE_TR_FUNCTIONS(VpWriter)

public:
    using ProgressFn = VpDocument::ProgressFn;

    struct Problems
    {
        QStringList errors;     // the VP cannot be written until these are fixed
        QStringList warnings;   // it can, but the engine may not see what the user expects
    };

    // Names the format cannot store (over 31 characters, outside Latin-1, unsafe),
    // empty files (a size 0 entry means a folder to the engine), anything over 4 GB,
    // and names that differ only by case in one folder (the engine sees one file)
    static Problems check(const VpDocument& document);

    // Why a file or folder name cannot go in a VP, or empty if it can
    static QString nameProblem(const QString& name);

    explicit VpWriter(const QString& path);

    bool write(const VpDocument& document, const ProgressFn& progress = {});
    bool commit();
    void cancel();

    bool wasCanceled() const { return m_canceled; }
    QString errorString() const { return m_error; }

private:
    QSaveFile m_file;
    QString m_error;
    bool m_canceled = false;
};
