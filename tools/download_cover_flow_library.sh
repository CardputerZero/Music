#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIBRARY_DIR="${ROOT_DIR}/test_music/cover_flow_library"
CACHE_DIR="${MUSIC_COVER_FLOW_CACHE_DIR:-${TMPDIR:-/tmp}/music-cover-flow-covers}"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    cat <<EOF
Usage: $0

Create a lightweight album library for testing Cover Flow.

Library: ${LIBRARY_DIR}
Cache:   ${CACHE_DIR}

The script downloads cover art only. Each album gets a one-second silent MP3
with title, artist, album, year, and track metadata.
EOF
    exit 0
fi

if [[ "$#" -ne 0 ]]; then
    echo "Unknown argument: $1" >&2
    exit 1
fi

for command in curl ffmpeg; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "Missing required command: ${command}" >&2
        exit 1
    fi
done

sha256_file()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        echo "Missing required command: sha256sum or shasum" >&2
        return 1
    fi
}

download_cover()
{
    local release_group_id="$1"
    local expected_sha256="$2"
    local destination="$3"
    local cached_cover="${CACHE_DIR}/${release_group_id}.jpg"
    local cover_url="https://coverartarchive.org/release-group/${release_group_id}/front-500"

    if [[ ! -f "${cached_cover}" ||
          "$(sha256_file "${cached_cover}")" != "${expected_sha256}" ]]; then
        curl --fail --location --retry 5 --retry-all-errors --silent --show-error \
            -A "M5CardputerZero-Music/0.1 test-fixture" \
            --output "${cached_cover}" \
            "${cover_url}"
    fi

    if [[ "$(sha256_file "${cached_cover}")" != "${expected_sha256}" ]]; then
        echo "Checksum mismatch for ${cached_cover}" >&2
        return 1
    fi

    cp "${cached_cover}" "${destination}"
}

create_album()
{
    local artist="$1"
    local album="$2"
    local year="$3"
    local release_group_id="$4"
    local cover_sha256="$5"
    local album_dir="${LIBRARY_DIR}/${artist}/${album}"
    local dummy_track="${album_dir}/01 - Cover Flow Preview.mp3"
    local release_group_url="https://musicbrainz.org/release-group/${release_group_id}"
    local cover_url="https://coverartarchive.org/release-group/${release_group_id}/front-500"

    echo "Preparing ${artist} - ${album}"
    mkdir -p "${album_dir}"
    download_cover "${release_group_id}" "${cover_sha256}" "${album_dir}/cover.jpg"

    ffmpeg -nostdin -hide_banner -loglevel error -y \
        -f lavfi -i "anullsrc=channel_layout=mono:sample_rate=22050" \
        -t 1 -codec:a libmp3lame -b:a 32k -id3v2_version 3 \
        -metadata "title=Cover Flow Preview" \
        -metadata "artist=${artist}" \
        -metadata "album=${album}" \
        -metadata "album_artist=${artist}" \
        -metadata "date=${year}" \
        -metadata "track=1/1" \
        -metadata "disc=1/1" \
        -metadata "genre=Test Fixture" \
        "${dummy_track}"

    cat >"${album_dir}/SOURCE.txt" <<EOF
Artist: ${artist}
Album: ${album}
Year: ${year}
Metadata: ${release_group_url}
Cover: ${cover_url}
Audio: one-second silence generated locally for UI testing
EOF
    touch "${album_dir}/.download-complete"
}

mkdir -p "${LIBRARY_DIR}" "${CACHE_DIR}"

create_album \
    "The Beatles" \
    "Abbey Road" \
    "1969" \
    "9162580e-5df4-32de-80cc-f45a8d8a9b1d" \
    "8ad081259802fbc4390a4171016daa11d8db1b555f591f06b6eda2d08d5466a6"
create_album \
    "Pink Floyd" \
    "The Dark Side of the Moon" \
    "1973" \
    "f5093c06-23e3-404f-aeaa-40f72885ee3a" \
    "852e90de98088902e8e0393579c1b0e63a0f3e135e013477f9a4093968be53e0"
create_album \
    "Miles Davis" \
    "Kind of Blue" \
    "1959" \
    "8e8a594f-2175-38c7-a871-abb68ec363e7" \
    "6177e826b0d7650647221e0b2a37ae049a5243c2404c5b2539b26e3a95a8a61f"
create_album \
    "Daft Punk" \
    "Random Access Memories" \
    "2013" \
    "aa997ea0-2936-40bd-884d-3af8a0e064dc" \
    "436f294eb1cef5c6a8fa7af6055296928484c5aa5e6114d738b3717d98acc299"
create_album \
    "Taylor Swift" \
    "1989" \
    "2014" \
    "4d9ec1c2-58ec-48a4-aa0a-916718adead0" \
    "922f3bd7f4741c38dc5a713ac5a5c164317e8825bc4d68334a01b7bc1edf8896"
create_album \
    "Kendrick Lamar" \
    "To Pimp a Butterfly" \
    "2015" \
    "d9103c72-3807-4378-9ce7-b6f3e8fdd547" \
    "975ab57fbe76cd65ebe023805867b8173ed3453780c80501b9a277e2419325ef"
create_album \
    "Radiohead" \
    "In Rainbows" \
    "2007" \
    "6e335887-60ba-38f0-95af-fae7774336bf" \
    "c687cd701425d2c2f6221a275b88f9719bef448bd89491c6d755a5eb29a15ec3"
create_album \
    "Björk" \
    "Homogenic" \
    "1997" \
    "810272e0-aef1-3d85-b2d3-e512e87fc38c" \
    "da00377717650c9e305ac8e770fa0fbe02573cb902117b3fca2039bed295d544"
create_album \
    "宇多田ヒカル" \
    "First Love" \
    "1999" \
    "c60fcd45-9420-30aa-98e5-2bffb894021a" \
    "056f754a6ba8b4c50f67262834b63f7a21d1f23b716341d7d88569e5921ad3a5"
create_album \
    "Gorillaz" \
    "Demon Days" \
    "2005" \
    "f959a46a-a136-3134-9412-6572b23fad95" \
    "c5cd0a884a5857e9a5c1498c3c4fe5f978dcecade2054d0c812fd3d3dc21a908"
create_album \
    "Daniel Caesar" \
    "NEVER ENOUGH" \
    "2023" \
    "65f5ca68-6dcd-4cc9-8f3c-09d9aaa56bfb" \
    "71567d615c92298fc26c5cd8de236654554a252e652726afae4085b83476c5d4"
create_album \
    "Billie Eilish" \
    "HIT ME HARD AND SOFT" \
    "2024" \
    "02a544b3-0459-42c7-bd9c-047162e7b67a" \
    "90ab6ab3ecbaf5d6595cc84bd0173194ad9962e6a71899352f3f22268716c9d8"
create_album \
    "Tyler, The Creator" \
    "IGOR" \
    "2019" \
    "0f1b9e07-b38b-4bba-9794-55e0924d7177" \
    "238f7c458f96f48ca60f227a7fa6782ae071758239d7cee9b737a581155f3906"
create_album \
    "Lorde" \
    "Melodrama" \
    "2017" \
    "668e80e0-b35e-4471-9788-0a3d797fe42c" \
    "5df245f445a1f62a18456d46b4f55cadff689eb3102ae53ff996c80b01a9924c"
create_album \
    "Sufjan Stevens" \
    "Carrie & Lowell" \
    "2015" \
    "21666540-83c8-4c54-9be1-0363a0848f33" \
    "e3f985d65b939c100c9a16322597e85dd06bc2a4fe64db9415e32914b56bb8b3"

echo "Cover Flow test library is ready at ${LIBRARY_DIR}"
