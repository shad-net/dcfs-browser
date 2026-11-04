# dcfs-browser

A Qt 6 browser for Driveclub's file system archives (`game.ndx` + `gameNNN.dat`). Open an
archive, look through its folders, preview any file straight out of the compressed data, then
extract just what you need, or everything.

The archive format is a C++ port of [DriveClubFS](https://github.com/Nenkai/DriveClubFS) by
Nenkai (MIT). Supported builds are the same: Driveclub 1.00, 1.28 and the NPXX51272 alpha.

## What it does

- **Open** a game folder, `game.ndx`, the alpha's `game.dat`, or any single `gameNNN.dat`.
  Opening a `gameNNN.dat` reads the names from the `game.ndx` beside it and shows only the
  files stored in that data file. The *Data file* box switches between data files (or shows
  all of them), with file counts; missing data files are flagged.
- **Scan a folder** (File > Scan Folder, Ctrl+Shift+F) to find every archive inside it and
  all its subfolders: a game dump, a drive, a folder of several builds. The *Archives* panel
  lists each one with its file count and size, and below it each `gameNNN.dat` with the files
  it holds; missing data files are marked. Click an archive or a data file to browse it,
  right-click to extract the whole archive or just that data file. Opening a folder that is
  not itself an archive folder scans it automatically. Data files without a `game.ndx` next
  to them are counted but cannot be listed, since the names live in `game.ndx`.
- **Browse** the paths as a folder tree with sizes, data file, offset and name hash. Folders
  show the total size and file count below them. Sort by any column.
- **Filter** by path (`cars`, `*.xml`, `data/tracks/*india*`).
- **Preview** without extracting: hex view of the first 64 KB, text view for XML and other text
  files (UTF-8 or UTF-16, detected), and an image view for formats Qt reads (PNG, JPEG, ...).
  **CSV files** (such as the `gui/lams/*.csv` text tables) open in a sortable, filterable
  table; the delimiter (`,` `;` tab `|`) and encoding are detected, quoted fields work, and
  Ctrl+C copies cells tab-separated, ready to paste into a spreadsheet. Details show the chunk range
  and MD5.
- **Extract** the selection (Ctrl+E), everything the filter shows, or the whole archive
  (Ctrl+Shift+E). Extraction keeps the folder structure, runs in the background with progress
  and cancel, reads each data file in order, and checks MD5s against the index (toggle
  *Verify MD5*). A file that fails its check is not written.
- **See textures right away**: selecting a `.rpk` in the tree shows its texture in the
  Image tab (the first one, for packs with several), decoded on a background thread, so you
  can hold the arrow keys to flick through a folder such as `liveryeditor/assets/stickers/`.
- **Gallery** (View > Show as Gallery, Ctrl+G, the *Show as Gallery* button on a folder, or
  right-click): a thumbnail grid of every texture in the selected folder, or in whatever the
  filter shows. Thumbnails load in the background, the ones on screen first, so even
  `liveryeditor/` (about 2,400 packs) opens at once. Resize thumbnails with the slider,
  filter by name, double-click to open the pack, and export the selected (or all shown)
  textures.
- **Export textures in bulk** (Extract > Export Textures, Ctrl+T, or right-click a folder,
  an archive or a data file): every texture inside the resource packs of the selection, or of
  everything shown, as DDS, PNG or both. Single-texture packs are written where the pack was
  (`stickers/x.tif_pixel_buffer.rpk` becomes `stickers/x.dds`); other packs get a folder of
  their own (`vehicles/car.rpk` becomes `vehicles/car/...`). Optionally includes XML, BIN and
  the other resources too. Runs in the background with progress and cancel.
- **Open resource packs** (`.rpk`): double-click one in the tree (or use *Open Resource
  Pack* in the preview or the right-click menu), or open a `.rpk` from disk with File > Open.
  A pack window lists every resource with its type, size and uid, filterable by type and
  name.
  - **Textures** (PIXEL_BUFFER) are untiled from the PS4 layout and shown in a viewer with
    channel views (RGBA, RGB, R, G, B, A) and fit or 1:1 zoom. Textures whose full-size mip
    lives in a separate top mips pack (`name_topmips.rpk`, `name_top_mips.rpk` or
    `prefix_common_topmips.rpk`) find it automatically, inside the archive or next to the
    file on disk.
  - **XML and BIN** resources show as text or hex; anything else shows as hex.
  - **Export** the selection or everything shown: textures as `.dds` (DX10 header, as
    DriveClubFS writes them) and optionally `.png`, XML and BIN as they are, and, if you
    tick *Other types as raw*, meshes, materials and the rest as `name.TYPE.bin`.

  Preview decoding supports BC1, BC3, BC4, BC5 (with the normal map's Z rebuilt), BC6H, BC7,
  RGBA8, BGRA8, R8, A8, R8G8, half and float formats and R10G10B10A2. The pixel format to DXGI
  mapping is DriveClubFS's, kept as-is (it maps the game's DXT3 to BC3 and DXT5 to BC5). PS3
  textures (older pixel buffer versions) are listed but not converted.
- **Export file names** of what you are looking at (File > Export File Names, Ctrl+L): the
  current data file and filter, the selection (right-click in the tree), one `gameNNN.dat`
  or a whole archive (right-click in the Archives panel), or one list per data file into a
  folder (`game000.txt`, `game001.txt`, ...). Three formats: names only (one path per
  line), detailed (`name (size bytes, Dat Index: n, Offset: 0x...)` like DriveClubFS's
  `list-files`), or CSV with size, data file, offset, name hash and MD5. Names come from
  `game.ndx`, so this also works for data files that are missing.

Supported chunk compression: none, zlib, LZ4 and LZ4HC (what Driveclub uses). Other formats
the index can declare (RaPack, LZRP3, LZMA, LZlib) are reported as unsupported.

## Command line

```
dcfs-browser [path]                       open the window, optionally with an archive
dcfs-browser --list --extract <dir> [--png] [--raw-other] <pack.rpk>
                                          list or export a resource pack on disk
dcfs-browser --list --extract <dir> --rpk <name> <path>
                                          the same for a pack inside an archive
dcfs-browser --export-textures <dir> <path>
             [--texture-format dds|png|both] [--all-resources] [--filter <text>]
                                          every texture inside the archive's resource packs
dcfs-browser --scan <dir>                 list every archive in <dir> and its subfolders
dcfs-browser --list <path>                print the files
dcfs-browser --export <file> <path>       write the file names to <file>
             [--format names|detailed|csv] [--filter <text>]
dcfs-browser --export <folder> --per-data-file <path>
                                          one list per gameNNN.dat
dcfs-browser --extract <dir> <path>       extract everything to <dir>
             [--filter <text>] [--no-verify]
```

With a `gameNNN.dat` as the path, `--list`, `--export` and `--extract` only cover that data
file. On
Windows the program is a GUI application, so run it from `cmd` with
`start /wait dcfs-browser.exe --list ... > files.txt` to capture the output.

## Building

Needs CMake 3.19+, a C++17 compiler and Qt 6.4+ (Core, Gui, Widgets). LZ4, zlib and bcdec
(BC1-BC7 decoding) are built from the `externals/` submodules.

```
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

On Windows, `windeployqt` runs after the build and copies the Qt DLLs next to
`dcfs-browser.exe` (turn off with `-DDCFS_DEPLOY_QT=OFF`).

## Testing without the game

`tools/make_test_archive.py` writes a small synthetic archive, built from DriveClubFS's reader
rather than this one, so the two check each other (`pip install lz4` first):

```
python3 tools/make_test_archive.py test/lz4 --compression lz4 --missing-dat
python3 tools/make_test_archive.py test/alpha --alpha --compression zlib
dcfs-browser test/lz4
```

The car pack in the test archive (`data/cars/abarth/abarth_695.rpk`) holds RGBA8 and BC1
textures, one whose full-size mip is in `abarth_695_topmips.rpk`, plus XML, BIN and a mesh;
`art/textures/hueramp.tif_pixel_buffer.rpk` is a single-texture pack with its top mips in
`hueramp.tif_top_mips.rpk`.

Options: `--compression none|zlib|lz4|lz4hc`, `--alpha` (3100 layout), `--v100` (1.00's 8-bit
data file index), `--missing-dat` (an entry in a data file that does not exist),
`--buffer-size`.

## Layout

| Path | |
|---|---|
| `src/evo_index.*` | `game.ndx` parser, including the packed name table and the name hash |
| `src/evo_data_file.*` | `gameNNN.dat` header and chunk table (and the alpha's embedded index) |
| `src/evo_file_system.*` | opens a folder or file and ties the index to its data files |
| `src/evo_reader.*` | chunk unpacking, ranged reads for preview, extraction with MD5 check |
| `src/resource_pack.*` | `.rpk` parser: resource table, names, BIN/XML/texture/top mips payloads |
| `src/texture.*` | PS4 untiling, pixel format table, DDS writer, decoding for preview |
| `src/resource_export.*` | a pack plus its top mips packs; exporting resources |
| `src/text_table.*` | text encoding detection (UTF-8/UTF-16) and CSV parsing |
| `src/texture_batch.*` | texture export across many packs, with the output layout |
| `src/file_list.*` | file name lists (names, detailed, CSV), whole archive or per data file |
| `gui/` | main window, folder tree model, resource pack window, folder scan, extraction |

## Licence

GPL-2.0-or-later. Format code derived from DriveClubFS, MIT, see `LICENSES/MIT.txt`.
