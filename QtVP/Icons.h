#pragma once

#include <QIcon>

// Simple line icons drawn in the current palette's text color when painted,
// so they follow the light/dark theme without being rebuilt
namespace Icons {

enum class Shape { Play, Pause, Stop, PopOut, Sound };

QIcon icon(Shape shape);

// A page with a colored mark for what kind of FreeSpace file it is. The same on
// every platform, and unlike system icons it knows tables, missions, and models.
enum class FileKind { Image, Animation, Sound, Movie, Text, Mission, Model, Script, Font, Other };

QIcon fileIcon(FileKind kind);

} // namespace Icons
