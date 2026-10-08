#pragma once

#include <QIcon>
#include <QString>

// The icon Windows shows for a file name's type (from its associated app), without
// the file having to exist. Cached per extension. Call from the UI thread.
namespace FileTypeIcons {

QIcon forFileName(const QString& fileName);

} // namespace FileTypeIcons
