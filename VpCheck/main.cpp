#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTextStream>

#include "Core/VpArchive.h"

// Command-line harness for VpArchive, so the VP core can be tested without the GUI

namespace {

QTextStream out(stdout);
QTextStream err(stderr);

int usage()
{
    err << "Usage:\n"
           "  VpCheck list <file.vp>\n"
           "  VpCheck verify <file.vp> [more.vp ...]\n"
           "  VpCheck extract <file.vp> <target folder>\n";
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
    VpArchive vp;
    if (!openArchive(vp, path))
        return 1;

    const VpArchive::ExtractResult result = vp.extract(vp.entriesUnder(VpArchive::RootFolder), target);
    for (const QString& error : result.errors)
        err << "  " << error << "\n";
    out << "Extracted " << result.extracted << " of " << vp.entries().size() << " files to "
        << QDir::toNativeSeparators(target) << "\n";
    return result.errors.isEmpty() ? 0 : 1;
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
    return usage();
}
