#pragma once

// Light/dark theme, saved in QSettings and applied app-wide
namespace Theme {

enum class Mode { System, Light, Dark };

Mode saved();
void save(Mode mode);

// Call once QApplication exists, and again whenever the setting changes
void apply(Mode mode);

} // namespace Theme
