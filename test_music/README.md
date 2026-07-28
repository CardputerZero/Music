# Test music library

Run the downloader from the repository root:

```bash
./tools/download_test_music.sh
```

Music is stored under `test_music/library/<artist>/<album>/`. The downloaded files
are intentionally ignored by Git.

The library contains seven real Creative Commons albums:

- FLAC: Me As In You — *Neon*
- FLAC: Vik44 — *Robot Overlords*
- FLAC: Vietnam II — *Still Empty Like Before*
- MP3: Louis Lingg And The Bombs — *Can You Hear The Uproar*
- MP3: Headsnack — *The Comet*
- MP3 with embedded lyrics: Nine Inch Nails — *The Slip*
- MP3 with official lyric sidecars: Josh Woodward — *Breadcrumbs*

Every download is checked with SHA-256. Archives are cached under the system
temporary directory; set `MUSIC_TEST_ALBUM_CACHE_DIR` to keep them elsewhere.

## Cover Flow library

Create a lightweight visual library with 15 well-known albums:

```bash
./tools/download_cover_flow_library.sh
MUSIC_LIBRARY_DIRS="$PWD/test_music/cover_flow_library" ./dist/desktop/M5CardputerZero-Music
```

Each album contains its real 500 px cover and a one-second silent MP3 carrying
the album and artist metadata. No copyrighted music is downloaded. Generated
files are stored under `test_music/cover_flow_library/` and ignored by Git.
