#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PACKAGE_NAME="${PACKAGE_NAME:-m5cardputerzero-music}"
PACKAGE_SUFFIX="${PACKAGE_SUFFIX:-m5stack1}"
DEB_ARCH="arm64"
MAINTAINER="${MAINTAINER:-m5stack <m5stack@m5stack.com>}"
PARALLEL="${PARALLEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/package}"
STAGE_DIR="${STAGE_DIR:-${ROOT_DIR}/build/deb-root}"
DIST_DIR="${DIST_DIR:-${ROOT_DIR}/dist}"
BIN_NAME="M5CardputerZero-Music"
CMAKE_BIN="${CMAKE:-cmake}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
READELF_BIN="${READELF:-readelf}"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1" >&2
        exit 1
    fi
}

read_cmake_cache_value() {
    local name="$1"
    local cache_file="${BUILD_DIR}/CMakeCache.txt"
    local line=""

    if [[ ! -f "${cache_file}" ]]; then
        echo "CMake cache not found: ${cache_file}" >&2
        return 1
    fi

    line="$(grep -E "^${name}(:[^=]*)?=" "${cache_file}" | tail -n 1 || true)"
    if [[ -z "${line}" ]]; then
        echo "CMake cache value not found: ${name}" >&2
        return 1
    fi
    printf "%s\n" "${line#*=}"
}

CMAKE_ARGS=(
    -S "${ROOT_DIR}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    -DMUSIC_USE_SDL=OFF
    -DMUSIC_USE_PULSEAUDIO=ON
    -DBUILD_TESTING=OFF
    -DMUSIC_BIN_NAME="${BIN_NAME}"
    -DMUSIC_OUTPUT_DIR="${BUILD_DIR}/dist"
)

host_arch="$(uname -m)"
if [[ "${host_arch}" != "aarch64" && "${host_arch}" != "arm64" ]]; then
    for compiler in aarch64-linux-gnu-gcc aarch64-linux-gnu-g++; do
        require_command "${compiler}"
    done
    READELF_BIN="${READELF:-aarch64-linux-gnu-readelf}"
    CMAKE_ARGS+=("-DCMAKE_TOOLCHAIN_FILE=${ROOT_DIR}/cmake/aarch64-linux-gnu.cmake")
fi

for command in "${CMAKE_BIN}" "${READELF_BIN}" dpkg-deb; do
    require_command "${command}"
done

"${CMAKE_BIN}" "${CMAKE_ARGS[@]}"
if [[ "$(read_cmake_cache_value MUSIC_USE_SDL)" != "OFF" ]]; then
    echo "Invalid package build: MUSIC_USE_SDL must be OFF." >&2
    exit 1
fi
if [[ "$(read_cmake_cache_value MUSIC_USE_PULSEAUDIO)" != "ON" ]]; then
    echo "Invalid package build: MUSIC_USE_PULSEAUDIO must be ON." >&2
    exit 1
fi
PACKAGE_VERSION="$(read_cmake_cache_value CMAKE_PROJECT_VERSION)"
"${CMAKE_BIN}" --build "${BUILD_DIR}" -j"${PARALLEL}"

EXECUTABLE="${BUILD_DIR}/dist/${BIN_NAME}"
DESKTOP_TEMPLATE="${SCRIPT_DIR}/music.desktop.in"
ICON_FILE="${SCRIPT_DIR}/images/music.png"
ALL_MUSIC_COVER="${ROOT_DIR}/assets/covers/all-music.jpg"
GUIDE_MUSIC_COVER="${ROOT_DIR}/assets/covers/guide-add-music.jpg"
GUIDE_COVERS_COVER="${ROOT_DIR}/assets/covers/guide-cover-art.jpg"
GUIDE_LYRICS_COVER="${ROOT_DIR}/assets/covers/guide-lyrics.jpg"
EXAMPLE_COVER="${ROOT_DIR}/assets/covers/examples.jpg"
EXAMPLE_MAPLE_LEAF="${ROOT_DIR}/assets/examples/01 - Maple Leaf Rag.mp3"
EXAMPLE_CLAIR_DE_LUNE="${ROOT_DIR}/assets/examples/02 - Clair de Lune.mp3"
EXAMPLE_LIEBESTRAUME="${ROOT_DIR}/assets/examples/03 - Liebesträume No. 3.mp3"
EXAMPLE_DAISY_BELL="${ROOT_DIR}/assets/examples/Daisy Bell (Bicycle Built for Two).mp3"
EXAMPLE_DAISY_BELL_LYRICS="${ROOT_DIR}/assets/examples/Daisy Bell (Bicycle Built for Two).lrc"
EXAMPLE_SOURCES="${ROOT_DIR}/assets/examples/SOURCES.txt"
THIRD_PARTY_NOTICES="${ROOT_DIR}/THIRD_PARTY_NOTICES.md"
CURSOR_HOVER="${ROOT_DIR}/assets/images/cursor_hover.png"
CURSOR_PRESSED="${ROOT_DIR}/assets/images/cursor_pressed.png"
PLAYBACK_FULLSCREEN="${ROOT_DIR}/assets/images/playback_fullscreen.png"
PLAYBACK_PREVIOUS="${ROOT_DIR}/assets/images/playback_previous.png"
PLAYBACK_PLAY="${ROOT_DIR}/assets/images/playback_play.png"
PLAYBACK_PAUSE="${ROOT_DIR}/assets/images/playback_pause.png"
PLAYBACK_NEXT="${ROOT_DIR}/assets/images/playback_next.png"
PLAYBACK_MODE_SEQUENTIAL="${ROOT_DIR}/assets/images/playback_mode_sequential.png"
PLAYBACK_MODE_SHUFFLE="${ROOT_DIR}/assets/images/playback_mode_shuffle.png"
PLAYBACK_MODE_REPEAT_ONE="${ROOT_DIR}/assets/images/playback_mode_repeat_one.png"
MUSIC_MAGIC_NOTE_A="${ROOT_DIR}/assets/images/music_magic_note_a.png"
MUSIC_MAGIC_NOTE_B="${ROOT_DIR}/assets/images/music_magic_note_b.png"
MUSIC_MAGIC_HAZARD="${ROOT_DIR}/assets/images/music_magic_hazard.png"
MUSIC_MAGIC_CATCHER="${ROOT_DIR}/assets/images/music_magic_catcher.png"
for path in \
    "${EXECUTABLE}" \
    "${DESKTOP_TEMPLATE}" \
    "${ICON_FILE}" \
    "${ALL_MUSIC_COVER}" \
    "${GUIDE_MUSIC_COVER}" \
    "${GUIDE_COVERS_COVER}" \
    "${GUIDE_LYRICS_COVER}" \
    "${EXAMPLE_COVER}" \
    "${EXAMPLE_MAPLE_LEAF}" \
    "${EXAMPLE_CLAIR_DE_LUNE}" \
    "${EXAMPLE_LIEBESTRAUME}" \
    "${EXAMPLE_DAISY_BELL}" \
    "${EXAMPLE_DAISY_BELL_LYRICS}" \
    "${EXAMPLE_SOURCES}" \
    "${THIRD_PARTY_NOTICES}" \
    "${CURSOR_HOVER}" \
    "${CURSOR_PRESSED}" \
    "${PLAYBACK_FULLSCREEN}" \
    "${PLAYBACK_PREVIOUS}" \
    "${PLAYBACK_PLAY}" \
    "${PLAYBACK_PAUSE}" \
    "${PLAYBACK_NEXT}" \
    "${PLAYBACK_MODE_SEQUENTIAL}" \
    "${PLAYBACK_MODE_SHUFFLE}" \
    "${PLAYBACK_MODE_REPEAT_ONE}" \
    "${MUSIC_MAGIC_NOTE_A}" \
    "${MUSIC_MAGIC_NOTE_B}" \
    "${MUSIC_MAGIC_HAZARD}" \
    "${MUSIC_MAGIC_CATCHER}"
do
    if [[ ! -f "${path}" ]]; then
        echo "Required file not found: ${path}" >&2
        exit 1
    fi
done

machine="$(${READELF_BIN} -h "${EXECUTABLE}" | awk -F: '/Machine:/ { sub(/^[[:space:]]+/, "", $2); print $2; exit }')"
if [[ "${machine}" != "AArch64" ]]; then
    echo "Invalid package executable architecture: expected AArch64, got ${machine:-unknown}." >&2
    exit 1
fi

dynamic_section="$(${READELF_BIN} -d "${EXECUTABLE}")"
if [[ "${dynamic_section}" == *"libSDL"* || "${dynamic_section}" == *"libGL.so"* ||
      "${dynamic_section}" == *"libGLES"* || "${dynamic_section}" == *"libEGL"* ||
      "${dynamic_section}" == *"libdrm"* || "${dynamic_section}" == *"libgbm"* ||
      "${dynamic_section}" == *"libglfw"* || "${dynamic_section}" == *"libX11"* ||
      "${dynamic_section}" == *"libwayland"* ]]; then
    echo "Invalid package executable: desktop or GPU graphics dependencies are not allowed." >&2
    exit 1
fi

rm -rf "${STAGE_DIR}"
mkdir -p \
    "${STAGE_DIR}/DEBIAN" \
    "${STAGE_DIR}/usr/share/APPLaunch/bin" \
    "${STAGE_DIR}/usr/share/APPLaunch/applications" \
    "${STAGE_DIR}/usr/share/APPLaunch/share/images" \
    "${STAGE_DIR}/usr/share/Music/covers" \
    "${STAGE_DIR}/usr/share/Music/examples" \
    "${STAGE_DIR}/usr/share/Music/images" \
    "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}" \
    "${DIST_DIR}"

install -m 755 "${EXECUTABLE}" "${DIST_DIR}/${BIN_NAME}"
install -m 755 "${EXECUTABLE}" "${STAGE_DIR}/usr/share/APPLaunch/bin/${BIN_NAME}"
install -m 644 "${DESKTOP_TEMPLATE}" "${STAGE_DIR}/usr/share/APPLaunch/applications/music.desktop"
install -m 644 "${ICON_FILE}" "${STAGE_DIR}/usr/share/APPLaunch/share/images/music.png"
install -m 644 "${ALL_MUSIC_COVER}" "${STAGE_DIR}/usr/share/Music/covers/all-music.jpg"
install -m 644 "${GUIDE_MUSIC_COVER}" "${STAGE_DIR}/usr/share/Music/covers/guide-add-music.jpg"
install -m 644 "${GUIDE_COVERS_COVER}" "${STAGE_DIR}/usr/share/Music/covers/guide-cover-art.jpg"
install -m 644 "${GUIDE_LYRICS_COVER}" "${STAGE_DIR}/usr/share/Music/covers/guide-lyrics.jpg"
install -m 644 "${EXAMPLE_COVER}" "${STAGE_DIR}/usr/share/Music/covers/examples.jpg"
install -m 644 "${EXAMPLE_MAPLE_LEAF}" "${STAGE_DIR}/usr/share/Music/examples/01 - Maple Leaf Rag.mp3"
install -m 644 "${EXAMPLE_CLAIR_DE_LUNE}" "${STAGE_DIR}/usr/share/Music/examples/02 - Clair de Lune.mp3"
install -m 644 "${EXAMPLE_LIEBESTRAUME}" "${STAGE_DIR}/usr/share/Music/examples/03 - Liebesträume No. 3.mp3"
install -m 644 "${EXAMPLE_DAISY_BELL}" \
    "${STAGE_DIR}/usr/share/Music/examples/Daisy Bell (Bicycle Built for Two).mp3"
install -m 644 "${EXAMPLE_DAISY_BELL_LYRICS}" \
    "${STAGE_DIR}/usr/share/Music/examples/Daisy Bell (Bicycle Built for Two).lrc"
install -m 644 "${EXAMPLE_SOURCES}" "${STAGE_DIR}/usr/share/Music/examples/SOURCES.txt"
install -m 644 "${THIRD_PARTY_NOTICES}" "${STAGE_DIR}/usr/share/doc/${PACKAGE_NAME}/THIRD_PARTY_NOTICES.md"
install -m 644 "${CURSOR_HOVER}" "${STAGE_DIR}/usr/share/Music/images/cursor_hover.png"
install -m 644 "${CURSOR_PRESSED}" "${STAGE_DIR}/usr/share/Music/images/cursor_pressed.png"
install -m 644 "${PLAYBACK_FULLSCREEN}" "${STAGE_DIR}/usr/share/Music/images/playback_fullscreen.png"
install -m 644 "${PLAYBACK_PREVIOUS}" "${STAGE_DIR}/usr/share/Music/images/playback_previous.png"
install -m 644 "${PLAYBACK_PLAY}" "${STAGE_DIR}/usr/share/Music/images/playback_play.png"
install -m 644 "${PLAYBACK_PAUSE}" "${STAGE_DIR}/usr/share/Music/images/playback_pause.png"
install -m 644 "${PLAYBACK_NEXT}" "${STAGE_DIR}/usr/share/Music/images/playback_next.png"
install -m 644 "${PLAYBACK_MODE_SEQUENTIAL}" \
    "${STAGE_DIR}/usr/share/Music/images/playback_mode_sequential.png"
install -m 644 "${PLAYBACK_MODE_SHUFFLE}" "${STAGE_DIR}/usr/share/Music/images/playback_mode_shuffle.png"
install -m 644 "${PLAYBACK_MODE_REPEAT_ONE}" \
    "${STAGE_DIR}/usr/share/Music/images/playback_mode_repeat_one.png"
install -m 644 "${MUSIC_MAGIC_NOTE_A}" "${STAGE_DIR}/usr/share/Music/images/music_magic_note_a.png"
install -m 644 "${MUSIC_MAGIC_NOTE_B}" "${STAGE_DIR}/usr/share/Music/images/music_magic_note_b.png"
install -m 644 "${MUSIC_MAGIC_HAZARD}" "${STAGE_DIR}/usr/share/Music/images/music_magic_hazard.png"
install -m 644 "${MUSIC_MAGIC_CATCHER}" "${STAGE_DIR}/usr/share/Music/images/music_magic_catcher.png"

INSTALLED_SIZE="$(du -sk "${STAGE_DIR}/usr" | awk '{print $1}')"
cat >"${STAGE_DIR}/DEBIAN/control" <<EOF
Package: ${PACKAGE_NAME}
Version: ${PACKAGE_VERSION}
Section: sound
Priority: optional
Architecture: ${DEB_ARCH}
Maintainer: ${MAINTAINER}
Depends: libc6, libstdc++6, libgcc-s1, zlib1g, libpulse0, pulseaudio-utils
Installed-Size: ${INSTALLED_SIZE}
Description: Local music library for M5CardputerZero APPLaunch
 Local audio library index and 3D Cover Flow browser.
EOF

DEB_PATH="${DIST_DIR}/${PACKAGE_NAME}_${PACKAGE_VERSION}_${PACKAGE_SUFFIX}_${DEB_ARCH}.deb"
dpkg-deb --build --root-owner-group "${STAGE_DIR}" "${DEB_PATH}"
if [[ "$(dpkg-deb -f "${DEB_PATH}" Architecture)" != "${DEB_ARCH}" ]]; then
    echo "Generated package has an invalid architecture field." >&2
    exit 1
fi

echo "Generated Debian package: ${DEB_PATH}"
