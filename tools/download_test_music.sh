#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIBRARY_DIR="${ROOT_DIR}/test_music/library"
CACHE_DIR="${MUSIC_TEST_ALBUM_CACHE_DIR:-${TMPDIR:-/tmp}/music-test-albums}"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    cat <<EOF
Usage: $0

Download the real Creative Commons albums used to test Music.

Library: ${LIBRARY_DIR}
Cache:   ${CACHE_DIR}

Override the cache with MUSIC_TEST_ALBUM_CACHE_DIR.
EOF
    exit 0
fi

if [[ "$#" -ne 0 ]]; then
    echo "Unknown argument: $1" >&2
    exit 1
fi

for command in curl python3 unzip; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "Missing required command: ${command}" >&2
        exit 1
    fi
done

mkdir -p "${LIBRARY_DIR}" "${CACHE_DIR}"

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

download_checked_file()
{
    local url="$1"
    local destination="$2"
    local expected_sha256="$3"

    mkdir -p "$(dirname "${destination}")"
    if [[ ! -f "${destination}" ||
          "$(sha256_file "${destination}")" != "${expected_sha256}" ]]; then
        curl --fail --location --retry 5 --retry-all-errors --silent --show-error \
            -A "Mozilla/5.0" --output "${destination}" "${url}"
    fi

    if [[ "$(sha256_file "${destination}")" != "${expected_sha256}" ]]; then
        echo "Checksum mismatch for ${destination}" >&2
        return 1
    fi
}

download_blocsonic_album()
{
    local artist="$1"
    local album="$2"
    local catalog_number="$3"
    local format="$4"
    local archive_url="$5"
    local expected_sha256="$6"
    local release_url="$7"
    local album_dir="${LIBRARY_DIR}/${artist}/${album}"
    local format_label
    local extension

    case "${format}" in
        flac)
            format_label="FLAC"
            extension="flac"
            ;;
        mp3)
            format_label="MP3"
            extension="mp3"
            ;;
        *)
            echo "Unsupported audio format: ${format}" >&2
            return 1
            ;;
    esac

    local cache_path="${CACHE_DIR}/${catalog_number}-${format_label}.zip"

    if [[ -f "${album_dir}/.download-complete" ]]; then
        echo "Album already available: ${artist} - ${album}"
        return
    fi

    echo "Downloading ${format_label}: ${artist} - ${album}"
    download_checked_file "${archive_url}" "${cache_path}" "${expected_sha256}"
    mkdir -p "${album_dir}"
    unzip -j -o -q "${cache_path}" "*.${extension}" "*Cover.jpg" \
        -x "__MACOSX/*" -d "${album_dir}"

    if [[ -f "${album_dir}/00 - Cover.jpg" ]]; then
        mv "${album_dir}/00 - Cover.jpg" "${album_dir}/cover.jpg"
    fi

    {
        printf 'Artist: %s\n' "${artist}"
        printf 'Album: %s\n' "${album}"
        printf 'Source: %s\n' "${release_url}"
        printf 'License: Creative Commons Attribution-NonCommercial-ShareAlike 4.0\n'
        printf 'Downloaded format: %s\n' "${format_label}"
    } >"${album_dir}/SOURCE.txt"
    touch "${album_dir}/.download-complete"
}

download_archive_album()
{
    local artist="$1"
    local album="$2"
    local identifier="$3"
    local license="$4"
    local license_url="$5"
    local artwork_credit="$6"
    local format="$7"
    shift 7

    local album_dir="${LIBRARY_DIR}/${artist}/${album}"
    local cache_dir="${CACHE_DIR}/${identifier}"
    local records=("$@")
    local album_complete=false

    if [[ -f "${album_dir}/.download-complete" ]]; then
        album_complete=true
        local existing_record
        for existing_record in "${records[@]}"; do
            local existing_remainder="${existing_record#*|}"
            local existing_name="${existing_remainder%%|*}"
            local existing_sha256="${existing_record##*|}"
            local existing_path="${album_dir}/${existing_name}"
            if [[ ! -f "${existing_path}" ||
                  "$(sha256_file "${existing_path}")" != "${existing_sha256}" ]]; then
                album_complete=false
                break
            fi
        done
        if [[ "${album_complete}" == true ]]; then
            echo "Album already available: ${artist} - ${album}"
        else
            echo "Repairing incomplete album: ${artist} - ${album}"
            rm -f "${album_dir}/.download-complete"
        fi
    fi

    if [[ "${album_complete}" != true ]]; then
        echo "Downloading ${format}: ${artist} - ${album}"
        mkdir -p "${album_dir}" "${cache_dir}"

        local record
        for record in "${records[@]}"; do
            local remainder="${record#*|}"
            local remote_name="${record%%|*}"
            local local_name="${remainder%%|*}"
            local expected_sha256="${record##*|}"

            download_checked_file \
                "https://archive.org/download/${identifier}/${remote_name}" \
                "${cache_dir}/${local_name}" \
                "${expected_sha256}"
            cp "${cache_dir}/${local_name}" "${album_dir}/${local_name}"
        done
    fi

    {
        printf 'Artist: %s\n' "${artist}"
        printf 'Album: %s\n' "${album}"
        printf 'Source: https://archive.org/details/%s\n' "${identifier}"
        printf 'License: %s\n' "${license}"
        printf 'License URL: %s\n' "${license_url}"
        printf 'Artwork credit: %s\n' "${artwork_credit}"
        printf 'Downloaded format: %s\n' "${format}"
    } >"${album_dir}/SOURCE.txt"
    touch "${album_dir}/.download-complete"
}

download_the_slip()
{
    local artist="Nine Inch Nails"
    local album="The Slip"
    local album_dir="${LIBRARY_DIR}/${artist}/${album}"
    local cache_dir="${CACHE_DIR}/NIN-The-Slip"
    local base_url="https://files.freemusicarchive.org/storage-freemusicarchive-org/music/Creative_Commons/Nine_Inch_Nails/The_Slip"
    local tracks=(
        "Nine_Inch_Nails_-_01_-_999999.mp3|94570cf8c4d3cc0f82e85b0f4fde7f810c728a0ce5edec77483d70ac2bba6eaf"
        "Nine_Inch_Nails_-_02_-_1000000.mp3|386fd2702ab6e774138bc08f4d8677256e847f022767a14d3c999d16d2c69094"
        "Nine_Inch_Nails_-_03_-_Letting_You.mp3|2958b9e39e024e8c911b1c8f30bf9b238cb66552229fb1451cd2bbe3e34c8648"
        "Nine_Inch_Nails_-_04_-_Discipline.mp3|c179a0d90c360f877f7de907bd63455d39888f40862698c3390a3d40abd60a4e"
        "Nine_Inch_Nails_-_05_-_Echoplex.mp3|162465c71e72ad67a41bd50e1c33986156b1342e52c9ec763dd4dec760dc4565"
        "Nine_Inch_Nails_-_06_-_Head_Down.mp3|e3ece638bebf1704ad1c8df5359e9badd8b1f1b41e1b72a29986796c8201740e"
        "Nine_Inch_Nails_-_07_-_Lights_in_the_Sky.mp3|444da698ff0e321c5b66879c6e4fdc4a64e73459b4aaae87c5531a0ce78e66af"
        "Nine_Inch_Nails_-_08_-_Corona_Radiata.mp3|636f6fa89c0d093a56dfabf44878ea41a080f348eae799bfa947024add82015b"
        "Nine_Inch_Nails_-_09_-_The_Four_of_Us_are_Dying.mp3|9e77af465967a438a419c2e5b574f6c19373f2a6e0224372fba56f2277a1188d"
        "Nine_Inch_Nails_-_10_-_Demon_Seed.mp3|88191ac6fafc4f57aaaaa3b2e175ee8940c03dd2278937f3c30140dfafa73db7"
    )

    if [[ -f "${album_dir}/.download-complete" ]]; then
        echo "Album already available: ${artist} - ${album}"
        return
    fi

    echo "Downloading MP3: ${artist} - ${album}"
    mkdir -p "${album_dir}"
    local record
    for record in "${tracks[@]}"; do
        local filename="${record%%|*}"
        local expected_sha256="${record#*|}"
        download_checked_file \
            "${base_url}/${filename}" \
            "${cache_dir}/${filename}" \
            "${expected_sha256}"
        cp "${cache_dir}/${filename}" "${album_dir}/${filename}"
    done

    {
        printf 'Artist: %s\n' "${artist}"
        printf 'Album: %s\n' "${album}"
        printf 'Source: https://freemusicarchive.org/music/Nine_Inch_Nails/The_Slip\n'
        printf 'Original release: https://www.nin.com/music/the-slip/\n'
        printf 'License: Creative Commons Attribution-NonCommercial-ShareAlike 3.0 US\n'
        printf 'Downloaded format: MP3\n'
        printf 'Lyrics: embedded in the vocal tracks\n'
    } >"${album_dir}/SOURCE.txt"
    touch "${album_dir}/.download-complete"
}

extract_josh_woodward_lyrics()
{
    python3 -c '
import html
import json
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
match = re.search(r"<script type=\"application/ld\+json\">(.*?)</script>", source, re.S)
if not match:
    raise SystemExit("official lyrics metadata was not found")
document = json.loads(html.unescape(match.group(1)))
lyrics = document["recordingOf"]["lyrics"]["text"].strip()
pathlib.Path(sys.argv[2]).write_text(lyrics + "\n", encoding="utf-8")
' "$1" "$2"
}

download_breadcrumbs()
{
    local artist="Josh Woodward"
    local album="Breadcrumbs"
    local album_dir="${LIBRARY_DIR}/${artist}/${album}"
    local cache_dir="${CACHE_DIR}/Josh-Breadcrumbs"
    local audio_base_url="https://www.joshwoodward.com/mp3/Breadcrumbs"
    local tracks=(
        "01-Swansong|Swansong|321174573eea651b267fe217a9b56501ee7a335c3de6ced8cf89b4c7811fb811"
        "02-2020|2020|4336a2199c9f06322e6c885b7042d2286456bb07d95049a87bacbd6679ccd863"
        "03-BorderBlaster|BorderBlaster|2dfa4ef14ace13e7490b276de4bd37f7eb3b157db2985a21b7f45532137d3c33"
        "04-PrivateHurricane|PrivateHurricane|3842f24bb37d7dae666ce141139a291f7a72648f5571f6e77f29cea35382c223"
        "05-UnderTheStairs|UndertheStairs|afb467c3696d88898d4851f6692d721d80437679099458cb0c079d55a3e35f6d"
        "06-StarsCollide|StarsCollide|ce4f8550643b6686835d7acfbd5ff3c84e77b95f82fc432e2509c8ff82eb2428"
        "07-GreySnow|GreySnow|1c3e874f50b63cc4327f8bf69bfbcf1075023b02279b91fd6c2451631bbd9977"
        "08-Overthrown|Overthrown|45c7bc8a50982ec0d15967f6402c40d36412138699474cbad53a954c5c01bba7"
        "09-OnceTomorrow|OnceTomorrow|04d25fe9328ea17a47ed6640ee99fa4eb3bf835204507302ade45853949f677a"
        "10-TheVoices|TheVoices|a56d46b096be658e893a59566cd8503f5fad79d1e66f2becb4a50d728c2c1cbe"
        "11-ImNotDreaming|ImNotDreaming|877b7c1b728c454a0f905baa11fb720b042ef9c90d95691e41bbfba44e3036c4"
    )

    if [[ -f "${album_dir}/.download-complete" ]]; then
        echo "Album already available: ${artist} - ${album}"
        return
    fi

    echo "Downloading MP3 and lyrics: ${artist} - ${album}"
    mkdir -p "${album_dir}" "${cache_dir}/pages"
    download_checked_file \
        "https://www.joshwoodward.com/nextImages/albums/Breadcrumbs-150.jpg" \
        "${cache_dir}/cover.jpg" \
        "87961131e9312875b88efc603452becd19b6400383842f736ed9250feaec00dd"
    cp "${cache_dir}/cover.jpg" "${album_dir}/cover.jpg"

    local record
    for record in "${tracks[@]}"; do
        local remainder="${record#*|}"
        local stem="${record%%|*}"
        local slug="${remainder%%|*}"
        local expected_sha256="${record##*|}"
        local filename="JoshWoodward-Breadcrumbs-${stem}.mp3"
        local lyrics_page="${cache_dir}/pages/${slug}.html"

        download_checked_file \
            "${audio_base_url}/${filename}" \
            "${cache_dir}/${filename}" \
            "${expected_sha256}"
        if [[ ! -f "${lyrics_page}" ]]; then
            curl --fail --location --retry 3 --silent --show-error \
                -A "Mozilla/5.0" --output "${lyrics_page}" \
                "https://www.joshwoodward.com/song/${slug}"
        fi

        cp "${cache_dir}/${filename}" "${album_dir}/${filename}"
        extract_josh_woodward_lyrics \
            "${lyrics_page}" \
            "${album_dir}/${filename%.mp3}.lrc"
    done

    {
        printf 'Artist: %s\n' "${artist}"
        printf 'Album: %s\n' "${album}"
        printf 'Source: https://www.joshwoodward.com/album/Breadcrumbs\n'
        printf 'License: Creative Commons Attribution 4.0\n'
        printf 'Downloaded format: MP3\n'
        printf 'Lyrics: official plain-text sidecars from each song page\n'
    } >"${album_dir}/SOURCE.txt"
    touch "${album_dir}/.download-complete"
}

download_blocsonic_album \
    "Me As In You" \
    "Neon" \
    "BSMX0226" \
    "flac" \
    "https://assets.blocsonic.com/releases/maxblocs/bsmx0226/00-BSMX0226_FLAC.zip" \
    "e9d4ff97b615bc4b2b600dd278f3bea385b78e309184ce6f7ae5693aaeff5446" \
    "https://blocsonic.com/releases/me-as-in-you/neon/"
download_blocsonic_album \
    "Vik44" \
    "Robot Overlords" \
    "BSMX0221" \
    "flac" \
    "https://assets.blocsonic.com/releases/maxblocs/bsmx0221/00-BSMX0221_FLAC.zip" \
    "8ac5823b18b125f66d2f5265a47e1277062dd0f4da25afab4bb0bfad6b3071ed" \
    "https://blocsonic.com/releases/vik44/robot-overlords/"
download_blocsonic_album \
    "Vietnam II" \
    "Still Empty Like Before" \
    "BSMX0227" \
    "flac" \
    "https://assets.blocsonic.com/releases/maxblocs/bsmx0227/00-BSMX0227_FLAC.zip" \
    "3857cad47c1961e8034dfdadd4f2401924080d667bb190645339b7b835669536" \
    "https://blocsonic.com/releases/vietnam-ii/still-empty-like-before/"
download_blocsonic_album \
    "Louis Lingg And The Bombs" \
    "Can You Hear The Uproar" \
    "BSMX0229" \
    "mp3" \
    "https://assets.blocsonic.com/releases/maxblocs/bsmx0229/00-BSMX0229_192Kbs_MP3.zip" \
    "5af8264761c696a0e801614f78ba73d07b07315b8bbaefcdb7441d0b1fdfbcc3" \
    "https://blocsonic.com/releases/louis-lingg-and-the-bombs/can-you-hear-the-uproar/"
download_blocsonic_album \
    "Headsnack" \
    "The Comet" \
    "BSMX0225" \
    "mp3" \
    "https://assets.blocsonic.com/releases/maxblocs/bsmx0225/00-BSMX0225_192Kbs_MP3.zip" \
    "c83b6ad34357a81b257f658dd071dae3a4df48e06ccf67524329704839fdcc80" \
    "https://blocsonic.com/releases/headsnack/the-comet/"
download_the_slip
download_breadcrumbs
download_archive_album \
    "MDK" \
    "008: ロボットの夢" \
    "mdk008-untitled" \
    "Creative Commons Attribution 3.0" \
    "https://creativecommons.org/licenses/by/3.0/" \
    "D." \
    "MP3" \
    "cover_thumb.jpg|cover.jpg|0dfe14ce565711eba8afa3411dbc4665e100b1508a69e85b0f06860024d4dfda" \
    "01%20-%20%E6%A1%9C%E3%81%AE%E9%A2%A8.mp3|01 - 桜の風.mp3|d291827a6f5f536686ee964b40a0f5836fe94e6c0a44dacda330a18b6eb714e6" \
    "02%20-%20%E6%9C%AA%E6%9D%A5%E3%81%AE%E9%83%BD%E5%B8%82.mp3|02 - 未来の都市.mp3|12dfc066a09ecdf086741f50b818336283916df339ee7e20896451ca5bcb7147" \
    "03%20-%20%E4%B8%83%E4%BA%BA%E3%81%AE%E6%98%9F.mp3|03 - 七人の星.mp3|cc440f14901b6ce23b51352980ace60a71c96f8db34a8e4f33ec988bcd8e7d2c" \
    "04%20-%20%E5%AE%87%E5%AE%99%E3%81%AE%E7%A7%98%E5%AF%86.mp3|04 - 宇宙の秘密.mp3|317df05bb1a582cf9c7cb02a967c5e4d8768e1210ebc8290d135a5b1ccb2ea87" \
    "05%20-%20%E9%BB%84%E9%87%91%E3%81%AE%E5%85%89.mp3|05 - 黄金の光.mp3|78241a0bfd7e8f1f601663543c08932c5a15e756c6d22f4d0fa01bc514f1f06a" \
    "06%20-%20%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%81%AE%E5%A4%A2.mp3|06 - ロボットの夢.mp3|b692bb1736e1cc3ce0512889a11bf9596e1bfbe2fdd35f18c8367f5db745b971"
download_archive_album \
    "krai" \
    "время-вечность" \
    "teambientless" \
    "Creative Commons Attribution 4.0" \
    "https://creativecommons.org/licenses/by/4.0/" \
    "Photo by Лола; design by krai / Kfor" \
    "MP3" \
    "cover_thumb.jpg|cover.jpg|8d0e740e53928b8280ec3833b52301e9b7107f2fafea12aca722959f8b98586e" \
    "1%20-%20%D0%B7%D0%B5%D0%BB%D1%8C%D0%B5%20%D0%BF%D0%B0%D0%BC%D1%8F%D1%82%D0%B8.mp3|1 - зелье памяти.mp3|3c54281bc4386e9c13919cef76e38958fd7082e5eec5d4b4d4fe42994ce4ea90" \
    "2%20-%20%D0%B3%D0%BE%D1%80%D0%B0.mp3|2 - гора.mp3|66881c7c0dd42f2ed95e2093cd01a310d3c25dae90271ec2c6ff933d89e24d4a" \
    "3%20-%20%D0%B0%D1%80%D1%85%D0%B0%D0%BD%D0%B3%D0%B5%D0%BB%2C%20%D1%87.1.mp3|3 - архангел, ч.1.mp3|939f314dc84d07073d0945042f848972c9d2e3a3e44c0d67f086be475d3af46e" \
    "4%20-%20%D0%B0%D1%80%D1%85%D0%B0%D0%BD%D0%B3%D0%B5%D0%BB%2C%20%D1%87.2.mp3|4 - архангел, ч.2.mp3|c4964dec9f574c10c74854f38c124749c4525925b62d0019a709f150a648e38b" \
    "5%20-%20%D0%B0%D1%80%D1%85%D0%B0%D0%BD%D0%B3%D0%B5%D0%BB%2C%20%D1%87.3.mp3|5 - архангел, ч.3.mp3|62e0035364574550a3d5677d5bcd3410d450e5bc2faa1adf47bc7d7ad9de6eae" \
    "6%20-%20pun-pun%20hop.mp3|6 - pun-pun hop.mp3|dbc97012ee8e0bf531e00734146de02aada000d3b2ab7f8a6fccba2d618b1d00"

echo "Test music library is ready at ${LIBRARY_DIR}"
