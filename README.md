# QtVP

A Qt-based VP archive viewer and extractor for FreeSpace Open.

QtVP keeps the familiar VPView32 layout (folder tree, file list, resizable preview pane) and brings it up to date with the formats the modern engine uses.

> Early development. See [PLAN.md](PLAN.md) for the roadmap.

## Planned features

- Open VP archives and browse their folders
- Extract selected files, folders, or the whole archive; drag files straight out to Explorer
- Extract to a configured FreeSpace data folder
- Preview images (PCX, TGA, DDS, PNG, JPG), animations (ANI, EFF, APNG), and sounds (WAV, OGG)
- Create and edit VPs: add, remove, rename, and save
- Read compressed (LZ41) VP entries

## Building

Requirements:

- Visual Studio 2026 (v145 toolset)
- Qt 6.8.3 (msvc2022_64), registered in Qt VS Tools as `6.8.3_msvc2022_64`
- Qt VS Tools extension

Open `QtVP.slnx` and build `Release|x64`.

The solution also builds `VpCheck`, a small console tool for testing the VP core without the GUI:

```
VpCheck list <file.vp>
VpCheck verify <file.vp> [more.vp ...]
VpCheck extract <file.vp> <target folder>
```

## Third-party code

- [LZ4](https://github.com/lz4/lz4) 1.10.0 (`lz4.c`, `lz4.h`), BSD 2-Clause. See `QtVP/Dependencies/lz4/LICENSE`.

## License

GNU GPL v3. See [LICENSE](LICENSE).
