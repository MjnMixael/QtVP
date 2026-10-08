#include "FileTypeIcons.h"

#include <QApplication>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QStyle>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace {

#ifdef Q_OS_WIN
// SHGFI_USEFILEATTRIBUTES makes the shell answer from the name alone, so a made-up
// "file.ext" gets the same icon a real one would
QPixmap shellIcon(const QString& extension, UINT sizeFlag)
{
    const std::wstring name = (QStringLiteral("file.") + extension).toStdWString();
    SHFILEINFOW info = {};
    if (!SHGetFileInfoW(name.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
            SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | sizeFlag) || !info.hIcon)
        return QPixmap();

    const QPixmap pixmap = QPixmap::fromImage(QImage::fromHICON(info.hIcon));
    DestroyIcon(info.hIcon);
    return pixmap;
}
#endif

} // namespace

QIcon FileTypeIcons::forFileName(const QString& fileName)
{
    static QHash<QString, QIcon> cache;

    const QString extension = QFileInfo(fileName).suffix().toLower();
    const auto found = cache.constFind(extension);
    if (found != cache.constEnd())
        return found.value();

    QIcon icon;
#ifdef Q_OS_WIN
    // Both sizes, so the icon stays sharp on scaled displays
    const QPixmap small = shellIcon(extension, SHGFI_SMALLICON);
    const QPixmap large = shellIcon(extension, SHGFI_LARGEICON);
    if (!small.isNull())
        icon.addPixmap(small);
    if (!large.isNull())
        icon.addPixmap(large);
#endif
    if (icon.isNull())
        icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

    cache.insert(extension, icon);
    return icon;
}
