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
