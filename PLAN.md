# QtVP Plan

A standalone Qt replacement for VPView32: open VP archives, browse them, extract files, and make or edit VPs. Polish over breadth. The most common job is "open a VP, grab a few files, put them somewhere," so that path has to be fast and pleasant.

## Ground rules

- Keep the VPView32 layout: folder tree with file counts and an `<All files>` node, a sortable file list, a resizable preview pane under the tree, and a toolbar with Load / Extract / Options.
- Preview support targets parity with what the FSO engine loads, not every format in existence.
- No feature creep. Things other community VP tools bolt on (mod managers, model editors, etc.) are out of scope.
- Setup follows AnimStudio and Etemenanki: Qt VS Tools project, Visual Studio 2026 (.slnx, v145 toolset), Qt 6.8.3 msvc2022_64 (ABI-compatible with v145), C++17, GPLv3, tag-triggered release workflow.

## VP format reference

From the engine (`code/cfile/cfilesystem.cpp`, `code/cfile/cfilecompression.*`, `code/cfilearchiver`, `code/cfileextractor`):

- Header (16 bytes): `"VPVP"`, int version (2), int directory offset, int entry count.
- Directory entries (44 bytes each): int offset, int size, char[32] name, int timestamp (Unix time).
- A directory entry with size 0 opens a folder; an entry named `..` closes the current one. Files belong to the folder that is open when they appear.
- Names are limited to 31 characters plus the terminator. The engine matches names case-insensitively.
- LZ41 compression is per file, not per archive: a compressed entry starts with the `LZ41` magic, followed by LZ4 blocks and a block offset table. The last 12 bytes are three ints: offset count, decompressed size, block size (see `lz41_create_ci` / `lz41_load_offsets`). Uncompressed and compressed entries can be mixed in one VP.

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

### 2. VP core
- `VpArchive`: parse header and directory, build the folder tree, validate offsets and sizes against the file length, and report damaged archives cleanly instead of crashing.
- Read an entry's bytes, transparently decompressing LZ41 entries (vendor `lz4.c`/`lz4.h`).
- Extract an entry, a folder, or everything to a target folder, keeping relative paths and original timestamps.
- No GUI dependencies, so it can be exercised from a small test harness.

### 3. Browsing and extraction
- Folder tree model with counts and `<All files>`; file list model with Name, Type, Size, Date & Time.
- Filter box above the file list.
- Open by File menu, drop a VP on the window, command-line argument, or recent-files list.
- Extract selected (or the selected folder) to a chosen folder.
- Drag files out of the list straight into Explorer (extract to a temp folder on drag start).
- Options dialog: FS data folder, then enable "Extract to FS Data Folder".

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
