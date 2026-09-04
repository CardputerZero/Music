#pragma once

#include <cstdint>

namespace music::music_key {

constexpr std::uint32_t Up = 0x10001;
constexpr std::uint32_t Down = 0x10002;
constexpr std::uint32_t Left = 0x10003;
constexpr std::uint32_t Right = 0x10004;
constexpr std::uint32_t Enter = 0x10005;
constexpr std::uint32_t Escape = 0x10006;
constexpr std::uint32_t Key4 = 0x10007;
constexpr std::uint32_t Key5 = 0x10008;
constexpr std::uint32_t Key6 = 0x10009;
constexpr std::uint32_t Key7 = 0x1000a;
constexpr std::uint32_t Key8 = 0x1000b;
constexpr std::uint32_t Space = 0x1000c;
// The keyboard firmware emits these as Linux consumer/media key events for
// the Fn + Q/W/E shortcuts.
constexpr std::uint32_t PlayPause = 0x1000d;
constexpr std::uint32_t Previous = 0x1000e;
constexpr std::uint32_t Next = 0x1000f;
// The keyboard firmware emits KEY_HELP for the Fn + H shortcut.
constexpr std::uint32_t Help = 0x10010;
// The keyboard firmware emits these as Linux consumer/media key events for
// the Fn + S/D volume shortcuts.
constexpr std::uint32_t VolumeDown = 0x10011;
constexpr std::uint32_t VolumeUp = 0x10012;
constexpr std::uint32_t NowPlaying = Key8;

}  // namespace music::music_key
