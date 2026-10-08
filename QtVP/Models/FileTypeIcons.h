#pragma once

#include <QIcon>
#include <QString>

#include "Icons.h"

// Which kind of FreeSpace file a name is, by extension, and its icon. Cached per kind.
namespace FileTypeIcons {

Icons::FileKind kindOf(const QString& fileName);
QIcon forFileName(const QString& fileName);

} // namespace FileTypeIcons
