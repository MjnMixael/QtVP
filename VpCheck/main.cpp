#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTextStream>

#include "Core/VpArchive.h"
#include "Core/VpDocument.h"
#include "Core/VpWriter.h"

#include <QHash>
#include <memory>

// Command-line harness for VpArchive, so the VP core can be tested without the GUI

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

int usage()
{
    err << "Usage:\n"
           "  VpCheck list <file.vp>\n"
           "  VpCheck verify <file.vp> [more.vp ...]\n"
           "  VpCheck extract <file.vp> <target folder>\n"
           "  VpCheck roundtrip <file.vp> <copy.vp>\n";
    return 2;
}

bool openArchive(VpArchive& vp, const QString& path)
{
    if (!vp.open(path)) {
        err << QDir::toNativeSeparators(path) << ": " << vp.errorString() << "\n";
        return false;
    }
    for (const QString& warning : vp.warnings())
        err << QDir::toNativeSeparators(path) << ": warning: " << warning << "\n";
    return true;
}

int list(const QString& path)
{
    VpArchive vp;
    if (!openArchive(vp, path))
        return 1;

    for (int i = 0; i < int(vp.entries().size()); ++i) {
        const VpEntry& e = vp.entries()[i];
        const QString compressed = vp.isCompressed(i)
            ? QString("LZ41 %1").arg(vp.uncompressedSize(i), 10)
            : QString(15, ' ');
        out << QString("%1  %2  %3  %4\n")
            .arg(e.size, 10)
            .arg(compressed)
            .arg(QDateTime::fromSecsSinceEpoch(e.timestamp).toString("yyyy-MM-dd HH:mm:ss"))
            .arg(vp.entryPath(i));
    }
    out << vp.entries().size() << " files in " << vp.folders().size() - 1 << " folders\n";
    return 0;
}

// Reads and decompresses every entry
int verify(const QStringList& paths)
{
    int failedArchives = 0;
    for (const QString& path : paths) {
        VpArchive vp;
        if (!openArchive(vp, path)) {
            ++failedArchives;
            continue;
        }

        int compressed = 0;
        int errors = 0;
        for (int i = 0; i < int(vp.entries().size()); ++i) {
            QString error;
            const QByteArray data = vp.readEntry(i, &error);
            if (!error.isEmpty()) {
                err << "  " << vp.entryPath(i) << ": " << error << "\n";
                ++errors;
            } else if (data.size() != vp.uncompressedSize(i)) {
                err << "  " << vp.entryPath(i) << ": read " << data.size() << " bytes, expected "
                    << vp.uncompressedSize(i) << "\n";
                ++errors;
            }
            if (vp.isCompressed(i))
                ++compressed;
        }

        out << QDir::toNativeSeparators(path) << ": " << vp.entries().size() << " files, "
            << compressed << " LZ41, " << errors << " errors\n";
        if (errors > 0)
            ++failedArchives;
    }
    return failedArchives > 0 ? 1 : 0;
}

int extract(const QString& path, const QString& target)
{
    auto archive = std::make_unique<VpArchive>();
    if (!openArchive(*archive, path))
        return 1;
    const VpDocument document(std::move(archive));

    const std::vector<int> files = document.filesUnder(VpDocument::RootFolder);
    const VpDocument::ExtractResult result = document.extract(files, target);
    for (const QString& error : result.errors)
        err << "  " << error << "\n";
    out << "Extracted " << result.extracted << " of " << files.size() << " files to "
        << QDir::toNativeSeparators(target) << "\n";
    return result.errors.isEmpty() ? 0 : 1;
}

// Writes the VP back out through VpDocument and VpWriter, then checks the copy holds
// the same files at the same paths, with the same timestamps and contents
int roundtrip(const QString& path, const QString& copyPath)
{
    auto archive = std::make_unique<VpArchive>();
    if (!openArchive(*archive, path))
        return 1;
    const VpDocument document(std::move(archive));
    const VpArchive& original = *document.archive();

    const VpWriter::Problems problems = VpWriter::check(document);
    for (const QString& warning : problems.warnings)
        err << "  warning: " << warning << "\n";
    for (const QString& error : problems.errors)
        err << "  error: " << error << "\n";
    if (!problems.errors.isEmpty())
        return 1;

    VpWriter writer(copyPath);
    if (!writer.write(document) || !writer.commit()) {
        err << writer.errorString() << "\n";
        return 1;
    }

    VpArchive copy;
    if (!openArchive(copy, copyPath))
        return 1;

    QHash<QString, int> copyEntries;
    for (int i = 0; i < int(copy.entries().size()); ++i)
        copyEntries.insert(copy.entryPath(i), i);

    int mismatches = 0;
    auto mismatch = [&](const QString& what) {
        err << "  " << what << "\n";
        ++mismatches;
    };

    if (copy.entries().size() != original.entries().size())
        mismatch(QString("%1 files in the copy, %2 in the original").arg(copy.entries().size()).arg(original.entries().size()));

    for (int i = 0; i < int(original.entries().size()); ++i) {
        const QString entryPath = original.entryPath(i);
        const int c = copyEntries.value(entryPath, -1);
        if (c < 0) {
            mismatch(entryPath + ": missing from the copy");
            continue;
        }
        if (copy.entries()[c].timestamp != original.entries()[i].timestamp)
            mismatch(entryPath + ": timestamp differs");
        if (copy.entries()[c].size != original.entries()[i].size || copy.isCompressed(c) != original.isCompressed(i))
            mismatch(entryPath + ": stored size or compression differs");

        QString errorA;
        QString errorB;
        if (original.readEntry(i, &errorA) != copy.readEntry(c, &errorB) || errorA != errorB)
            mismatch(entryPath + ": contents differ");
    }

    out << QDir::toNativeSeparators(copyPath) << ": " << copy.entries().size() << " files, "
        << copy.folders().size() - 1 << " folders, " << mismatches << " mismatches\n";
    return mismatches == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 3)
        return usage();

    const QString command = args.at(1);
    if (command == "list" && args.size() == 3)
        return list(args.at(2));
    if (command == "verify")
        return verify(args.mid(2));
    if (command == "extract" && args.size() == 4)
        return extract(args.at(2), args.at(3));
    if (command == "roundtrip" && args.size() == 4)
        return roundtrip(args.at(2), args.at(3));
    return usage();
}
