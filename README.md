# CardputerZero Music

Local music player and indexed album browser for M5Stack CardputerZero.

<img width="320" height="170" alt="cover flow" src="https://github.com/user-attachments/assets/d5d06902-9dd6-427e-acd2-5c6a81866360" />

## Features

- Scan local libraries for MP3, FLAC, M4A/MP4, WAV, OGG, and Opus files in the background
- Keep an incremental SQLite index with title, artist, album, genre, year, track/disc number, and duration metadata
- Browse `All Music` and detected albums in an animated Cover Flow interface
- Use embedded or external album artwork and derive each album's UI colors from its cover
- Play album queues with sequential, shuffle, and repeat-one modes
- Display embedded lyrics or matching `.lrc` files with synchronized scrolling
- Render multilingual metadata with bundled Noto Sans SC and JP fonts
- Show bundled example music, synchronized sample lyrics, and setup guides when the user library is empty

## Dependencies

Run the bootstrap script once after cloning:

```bash
./bootstrap.sh
```

It fetches the pinned repositories from `repos.json` into `dependencies/`.

System build requirements:

- CMake 3.16 or newer, a C/C++ compiler, Git, and Python 3
- SDL2 development files for the desktop build
- zlib development files
- The GNU AArch64 toolchain, `dpkg-deb`, and an ARM64 zlib library when cross-packaging from x86 Linux

Project dependencies include LVGL, libjpeg-turbo, spdlog, Smooth UI Toolkit, miniaudio, TagLib, SQLite, and
SQLiteCpp.

## Build

Build the SDL desktop version and run the tests:

```bash
cmake -S . -B build/desktop -DMUSIC_USE_SDL=ON -DBUILD_TESTING=ON
cmake --build build/desktop -j8
ctest --test-dir build/desktop --output-on-failure
```

Run it with the default music library:

```bash
./dist/desktop/M5CardputerZero-Music
```

Set `MUSIC_SDL_ZOOM=2` or another positive scale for a larger desktop window.

## Usage

Music scans `$XDG_MUSIC_DIR` when set, otherwise `$HOME/Music`. Override the library with one or more
colon-separated paths:

```bash
MUSIC_LIBRARY_DIRS=/path/to/music:/another/library ./dist/desktop/M5CardputerZero-Music
```

The SQLite index is stored in `$XDG_DATA_HOME/Music/library.db`, falling back to
`$HOME/.local/share/Music/library.db`. Extracted artwork and lyrics use the matching XDG cache directory.

Download the Creative Commons test library with:

```bash
./tools/download_test_music.sh
MUSIC_LIBRARY_DIRS="$PWD/test_music/library" ./dist/desktop/M5CardputerZero-Music
```

Key controls:

- Cover Flow: Left/Right select, Enter opens an album or guide, Esc exits
- Album List: Up/Down select, Enter plays/pauses a track or opens album info from the header, `8` opens Now Playing,
  Esc returns
- Playback: `4` fullscreen, `5` previous, `6` play/pause, `7` next, `8` changes playback mode, Up/Down scroll
  lyrics, Esc returns
- Info pages: Up/Down scroll, Esc returns

On CardputerZero, `F`/`X`/`Z`/`C` are accepted as Up/Down/Left/Right. The device build reads
`MUSIC_KEYBOARD_DEVICE`, `APPLAUNCH_LINUX_KEYBOARD_DEVICE`, or the default CardputerZero keypad event node without
grabbing it from APPLaunch.

## Package

Build the CardputerZero ARM64 APPLaunch package:

```bash
./packaging/deb/package_deb.sh
```

The script cross-compiles on x86 Linux and builds natively on ARM64. It always produces the framebuffer build and
writes the binary and Debian package to `dist/`:

```text
dist/M5CardputerZero-Music
dist/m5cardputerzero-music_<version>_m5stack1_arm64.deb
```

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency, font, and bundled example-music attribution.
