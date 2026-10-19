# QtVP

A Qt-based VP archive viewer and extractor for FreeSpace Open.

QtVP keeps the familiar VPView32 layout (folder tree, file list, resizable preview pane) and brings it up to date with the formats the modern engine uses.

> The app is feature-complete; custom icons and branding are still placeholders. See [PLAN.md](PLAN.md) for background.

## Features

- Open VP archives and browse their folders
- Extract selected files, folders, or the whole archive; drag files straight out to the file manager
- Open files in their usual app: ANI/EFF in AnimStudio, POF in POF Tools, everything else in the system default
- Preview images (PCX, TGA, DDS, PNG, JPG), animations (ANI, EFF, APNG), and sounds (WAV, OGG)
- Create and edit VPs: add, remove, rename, and save
- Read compressed (LZ41) VP entries
- Light and dark themes, or follow the system
- Cross-platform: Windows and Linux

## Building

QtVP builds with CMake and Qt 6.8.3 (Core, Gui, Widgets, Multimedia, MultimediaWidgets, Concurrent). It builds on Windows (MSVC) and Linux (GCC/Clang); the GitHub Actions workflow builds and packages both.

### Requirements

- CMake 3.21+
- Qt 6.8.3, including the `qtmultimedia` module
- A C++17 compiler — MSVC (Visual Studio 2026) on Windows, GCC or Clang on Linux

### Visual Studio 2026

Generate the solution against your Qt install, then open it:

```bash
cmake -S . -B build/vs2026 -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt6/6.8.3/msvc2022_64
```

Open `build\vs2026\QtVP.slnx` and build/run with F5 (QtVP is the default startup project). Alternatively, use **File → Open → Folder** on the repo root — VS reads `CMakePresets.json` directly.

### Command line (Linux or Windows)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build
```

## Third-party code

- [LZ4](https://github.com/lz4/lz4) 1.10.0 (`lz4.c`, `lz4.h`), BSD 2-Clause. See `QtVP/Dependencies/lz4/LICENSE`.
- [bcdec](https://github.com/iOrange/bcdec) 0.985 (`bcdec.h`), MIT or Unlicense. The license is at the end of the header.
- [etcdec](https://github.com/iOrange/etcdec) 0.91 (`etcdec.h`), MIT or Unlicense. The license is at the end of the header.

## License

GNU GPL v3. See [LICENSE](LICENSE).
