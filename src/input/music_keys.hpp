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
constexpr std::uint32_t NowPlaying = Key8;

}  // namespace music::music_key
