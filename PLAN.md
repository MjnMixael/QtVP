# QtVP Plan

A standalone Qt replacement for VPView32: open VP archives, browse them, extract files, and make or edit VPs. Polish over breadth. The most common job is "open a VP, grab a few files, put them somewhere," so that path has to be fast and pleasant.

## Ground rules

- Keep the VPView32 layout: folder tree with file counts and an `<All files>` node, a sortable file list, a resizable preview pane under the tree, and a toolbar with Load / Extract.
- Preview support targets parity with what the FSO engine loads, not every format in existence.
- No feature creep. Things other community VP tools bolt on (mod managers, model editors, etc.) are out of scope.
- Setup follows AnimStudio and Etemenanki: Qt VS Tools project, Visual Studio 2026 (.slnx, v145 toolset), Qt 6.8.3 msvc2022_64 (ABI-compatible with v145), C++17, GPLv3, tag-triggered release workflow.

## Where things are

| What | Where |
|---|---|
| This repo | `E:\QtVP`, branch `main`, public at https://github.com/MjnMixael/QtVP |
| Solution / project | `QtVP.slnx`, `QtVP\QtVP.vcxproj` (+ `.filters`; keep both in sync when adding files) |
| Qt | `C:\Qt6\6.8.3\msvc2022_64`, registered in Qt VS Tools as `6.8.3_msvc2022_64`. Qt Multimedia and its `ffmpegmediaplugin` are installed there. |
| AnimStudio (code to copy) | `E:\AnimStudio\AnimStudio` (GitHub MjnMixael/AnimStudio) |
| Etemenanki (sibling app, same setup) | `E:\Etemenanki` |
| FSO engine source (format reference only, never a dependency, never copy code from it: the Volition source license is not GPL-compatible) | `E:\FSO-Code\code` |
| VPView32 (the app being replaced) | Windows-only, closed. Layout described under Ground rules. |

## Current state

- Phase 1 is done (`658fb96`, `1299112`). Phase 2 is done (`644f2e3`); the user confirmed VPs open and report their file count.
- Phase 3 is done; the user confirmed browsing, filtering, extraction, and drag-out work.
- Files: `main.cpp` (opens a `.vp` passed on the command line), `Core\VpArchive.{h,cpp}`, `Models\FolderTreeModel.{h,cpp}`, `Models\FileListModel.{h,cpp}`, `Windows\MainWindow.{h,cpp}`, `Forms\MainWindow.ui`, `Dependencies\lz4\` (upstream LZ4 1.10.0, `lz4.c`/`lz4.h`/`LICENSE` only).
- `MainWindow` owns a `std::unique_ptr<VpArchive>`. `openVp()` parses into a fresh archive and only replaces the current one on success; failures show a message box with `errorString()`, and `warnings()` go to the status bar. The models hold a raw archive pointer, so they are always set to nullptr before an archive is destroyed.
- `FolderTreeModel`: `<All files> (N)` first, then the folders alphabetized, each showing its direct file count (the tooltip has the recursive count). Internal id is folder index + 1; 0 is `<All files>`.
- `FileListModel` sits behind a `QSortFilterProxyModel`: Name, Folder (only shown for `<All files>`), Type, Size (stored bytes), Date & Time. `SortRole` gives raw numbers. `mimeData()` calls a drag provider in `MainWindow` that extracts to a temp folder.
- `VpCheck\` is a second project in the solution: a console harness over the same `VpArchive.cpp` and `lz4.c` with `list`, `verify` (reads and decompresses every entry), and `extract`. Not shipped by the release workflow.
- Widgets in the .ui: `mainSplitter` (horizontal) holding `leftSplitter` (vertical: `folderTree` QTreeView, `previewPane` with `playButton`/`stopButton`/`popOutButton` and the `previewArea` QLabel) and `filePane` (`filterEdit` QLineEdit above `fileList` QTreeView: multi-select, sortable, drag-only). Actions: `actionLoadVp`, `actionNewVp`, `actionCloseVp`, `actionExit`, `actionExtractToDir`, `actionFind` (Ctrl+F, focuses the filter), `actionAbout`, `actionAboutQt`. Menus: File, Action, Help. The Recent VPs submenu is built in code.
- `updateActions()` force-disables everything that is not wired up yet (New VP, preview buttons); enable each piece as it lands.
- `QSettings` (org and app name both `QtVP`): `window/*` for geometry, splitters, and the file list header; `paths/lastVpDir`, `paths/lastExtractDir`; `recentFiles` (up to 10, native paths).
- Icons are Qt standard icons (`QStyle::standardIcon`). No app icon or `.rc` file yet.
- The release workflow has not run yet. No version tag exists.
- **Next: Phase 4 (previews).**

## Working conventions

- **The user builds.** Do not run msbuild or other builds to verify; check changes statically and ask the user to build.
- Commit and push only when asked. Commit messages: short imperative subject, wrapped body explaining why. No attribution lines.
- Never write em or en dashes anywhere (code, comments, commits, docs, chat). Use a plain hyphen.
- American spelling (color, center, behavior, gray, organize).
- Comments are terse and match the surrounding code. No doxygen `/** @brief */` blocks.
- Qt keywords (`signals:`, `emit`) are fine here, as in AnimStudio. (QtFRED's no-keywords rule does not apply to this project.)
- 4-space indentation, `m_` member prefix, `ui->` pointer to the generated form, connections made in code with function-pointer syntax.
- When a phase or decision changes, update this file in the same commit.

## Notes on reusing AnimStudio code

The pieces are in `E:\AnimStudio\AnimStudio`. AnimStudio is the user's own GPL project, so its code can be copied directly. They all work on files on disk; QtVP needs them to work on bytes read out of a VP.

- `Formats\Custom Handlers\PcxHandler` and `TgaHandler` take a `QIODevice*`. Feed them a `QBuffer` over the entry's bytes; little or no change needed.
- `Formats\Custom Handlers\DdsHandler` takes a file path and uses compressonator for both read and write. QtVP only reads, so replace it with a decoder over bytes using `bcdec` (single header, MIT). Do not bring compressonator over.
- `Formats\Import\AniImporter`, `EffImporter`, `ApngImporter` expose `importFromFile(const QString& path)` returning `std::optional<AnimationData>`. Add a bytes-based entry point. `EffImporter` must resolve its frame files through a callback that looks them up in the same VP folder, not the file system.
- `ApngImporter` depends on `Dependencies\apngdisassembler` and `Dependencies\libpng`; `AniImporter` uses `Animation\Palette`. Copy only what the importers and playback need: no exporters, quantizer, libimagequant, apngasm, or compressonator.
- `Animation\AnimationData.h` (frames, fps, loop point, keyframes) and the play/pause timer logic in `Animation\AnimationController` are the model for preview playback. Strip the editing and export parts.

## Decisions and open items

- Name: QtVP for now. Possible concern: The Qt Company's trademark guidance may object to "Qt" at the start of a product name. Revisit before a 1.0 release if it matters.
- Release tags are plain `vX.Y.Z`. The user's mobile-app versioning rule (patch only for pre-releases) does not apply to QtVP.
- Knossos: AnimStudio's release workflow also opens a PR against KnossosNET/Knet-Tool-Repo. Left out of QtVP's workflow until QtVP is listed there; copy that job from `E:\AnimStudio\.github\workflows\release.yml` when it is.
- Drag-out to Explorer: extract the dragged entries to a temp folder when the drag starts and hand Explorer file URLs. Simple and fine for the "a few files" case. Each drag gets its own numbered subfolder of one `QTemporaryDir` (removed on exit); same-named files from different folders go in `~N` subfolders. Drags back onto QtVP itself are ignored.
- What the extract actions act on: the selected files, or the whole current folder (with subfolders) when nothing is selected or the folder tree has focus (so right-clicking a folder extracts the folder). Selected files land directly in the target folder, except from `<All files>`, where they keep their full VP paths. An extracted folder is recreated under the target.
- No "Extract to FS Data Folder". VPView32 had it, but with how mods are laid out now it is niche and full of edge cases, so it was dropped (user decision, 2026-10-08) along with the Options dialog and Tools menu that only existed for it. Do not bring it back. Add an Options dialog again only when there is a real setting for it.
- Existing files: one prompt per extraction (Overwrite / Skip Existing / Cancel). Errors are collected and shown together with details; cancel stops between files. Extraction runs on the UI thread with a modal `QProgressDialog`; move it to a worker thread if very large single entries make the UI stall.
- Filter box: plain text matches anywhere in the name, `*`/`?` wildcards match whole names, `;` separates patterns. Filters the current list only; pick `<All files>` to search the whole VP.
- The preview pane must stay resizable (VPView32's is). The pop-out window is an addition, not a replacement.
- Extraction keeps the VP's folder structure relative to the chosen target and restores the entry timestamps.
- When a VP fails validation, open nothing and say what is wrong (bad magic, directory offset outside the file, entry overruns file). Never crash on bad input.
- Be as tolerant as the engine, though. Several old campaign VPs (Storm Front, What If, Rain on Ribos 4, Light of Antares) have a header entry count one higher than the directory holds. The engine stops at end of file and keeps what it read, so `VpArchive` does the same and adds a warning instead of failing.
- A scan of 214 local VPs (63k entries) with the same rules found no other problems. Some VPs open the same folder twice; folders are merged case-insensitively, as the engine does. Unclosed folders at the end of the directory are fine.
- None of the local VPs have LZ41 entries, so the LZ41 reader is written from the engine source but untested on real data. Find a `.vpc` to test with.
- Extraction refuses entry or folder names that are empty, `.`/`..`, or contain `<>:"/\|?*` or control characters, so a hostile VP cannot write outside the target folder. Files are written with `QSaveFile` and timestamps are set afterward (skipped when the timestamp is 0).
- Large VPs (retail `sparky_fs2.vp` has thousands of entries; mod VPs can be several GB): read the directory only on open, read entry bytes on demand, never load the whole file.

## VP format reference

From the engine (`code/cfile/cfilesystem.cpp`, `code/cfile/cfilecompression.*`, `code/cfilearchiver`, `code/cfileextractor`):

- Header (16 bytes): `"VPVP"`, int version (2), int directory offset, int entry count.
- Directory entries (44 bytes each): int offset, int size, char[32] name, int timestamp (Unix time).
- A directory entry with size 0 opens a folder; an entry named `..` closes the current one. Files belong to the folder that is open when they appear. This means a VP cannot hold a zero-byte file (the engine would read it as a folder); phase 5 must refuse or warn on empty files.
- The engine loads both `*.vp` and `*.vpc`. A `.vpc` is an ordinary VP whose entries are LZ41-compressed. Offsets, sizes, and timestamps are read as unsigned 32-bit, so archives up to 4 GB work.
- Names are limited to 31 characters plus the terminator. The engine matches names case-insensitively.
- LZ41 compression is per file, not per archive: a compressed entry starts with the `LZ41` magic, followed by LZ4 blocks and a block offset table. The last 12 bytes are three ints: offset count, decompressed size, block size (see `lz41_create_ci` / `lz41_load_offsets`). Uncompressed and compressed entries can be mixed in one VP.
- LZ41 offsets are relative to the start of the entry, and there is one more offset than there are blocks (the last marks the end of the final block). Each block decodes independently with `LZ4_decompress_safe`, which is what lets the engine seek within a file.

## Engine format support (preview targets)

| Kind | Formats | Source in QtVP |
|---|---|---|
| Images | PCX, TGA (16/24/32-bit, RLE), PNG, JPG | AnimStudio handlers; Qt for PNG/JPG |
| DDS | Uncompressed, DXT1/3/5, BC7, mipmaps, cubemaps | Small decoder (`bcdec`) instead of AnimStudio's compressonator, since we only decode |
| Animations | ANI, EFF (frames resolved inside the same VP), APNG | AnimStudio importers and playback |
| Sound | WAV (PCM and ADPCM), OGG | Qt Multimedia, FFmpeg backend, playing from memory |
| Text | `.tbl .tbm .fs2 .fc2 .lua .eff .sdr .vert .frag .rml .rcss .txt .html` | Maybe: read-only monospace view |
| Movies | MP4, WebM, OGG (Theora), MVE | Later |
| KTX | ETC2 variants | Probably never |

## Phases

### 1. Scaffold (done)
- Repo, solution, project, GPLv3, README, release workflow.
- Main window with the VPView32 layout: toolbar, folder tree, file list, resizable preview pane with play/stop/pop-out buttons. Window and splitter layout persist across runs.

### 2. VP core (done)
- `VpArchive`: parse header and directory, build the folder tree, validate offsets and sizes against the file length, and report damaged archives cleanly instead of crashing.
- Read an entry's bytes, transparently decompressing LZ41 entries (vendor `lz4.c`/`lz4.h`).
- Extract an entry, a folder, or everything to a target folder, keeping relative paths and original timestamps.
- No GUI dependencies, so it can be exercised from a small test harness.

### 3. Browsing and extraction (done)
- Folder tree model with counts and `<All files>`; file list model with Name, Type, Size, Date & Time.
- Filter box above the file list.
- Open by File menu, drop a VP on the window, command-line argument, or recent-files list. Accept `.vpc` everywhere `.vp` is accepted.
- Extract selected (or the selected folder) to a chosen folder.
- Drag files out of the list straight into Explorer (extract to a temp folder on drag start).

### 4. Previews
- Preview pane stays resizable like VPView32's. Double-click or the pop-out button opens a larger separate preview window.
- Images: PCX, TGA, PNG, JPG, DDS (show compression type, size, mip count; cubemap faces).
- Animations: ANI, EFF, APNG with play/stop, copied from AnimStudio and adapted to read from memory instead of file paths.
- Sound: WAV and OGG via Qt Multimedia. Adds `multimedia` to `QtModules` and `qtmultimedia` to the release workflow.
- Optional: text and table viewer.

### 5. Creating and editing VPs
- New VP, add files or folders (including drag-in from Explorer), delete, rename, new folder, save, save as.
- Saving always writes a new file and replaces the old one only on success (`QSaveFile`), so a failed save cannot corrupt the VP.
- Warn on names over 31 characters and on names that differ only by case within a folder.
- Track unsaved changes; prompt on close.

### 6. Later
- Movie previews (definitely wanted).
- Text/table preview, if not done in phase 4.
- Writing LZ41-compressed entries.
- KTX/ETC2 previews (low priority).
- App icon and custom toolbar icons (currently Qt's standard icons).
