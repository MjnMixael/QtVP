#pragma once

#include <QIcon>

// Simple line icons drawn in the current palette's text color when painted,
// so they follow the light/dark theme without being rebuilt
namespace Icons {

enum class Shape { Play, Pause, Stop, PopOut, Sound };

QIcon icon(Shape shape);

} // namespace Icons
