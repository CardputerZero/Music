#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace music {

enum class MusicMagicPhase : std::uint8_t {
    Idle = 0,
    Playing,
    Failing,
    Completed,
    Finished,
};

enum class MusicMagicItemKind : std::uint8_t {
    Note = 0,
    Hazard,
};

struct MusicMagicItemSnapshot {
    MusicMagicItemKind kind = MusicMagicItemKind::Note;
    float x = 0.0f;
    float y = 0.0f;
    std::uint8_t appearance = 0;
    bool active = false;
};

struct MusicMagicCompletionSpriteSnapshot {
    float x = 0.0f;
    float y = 0.0f;
};

struct MusicMagicCompletionScoreSnapshot {
    float x = 0.0f;
    float y = 0.0f;
};

struct MusicMagicSnapshot {
    static constexpr std::size_t kMaxItems = 6;
    static constexpr std::size_t kCompletionSpriteCount = 4;
    static constexpr std::size_t kNoFailedItem = kMaxItems;

    MusicMagicPhase phase = MusicMagicPhase::Idle;
    std::array<MusicMagicItemSnapshot, kMaxItems> items{};
    std::array<MusicMagicCompletionSpriteSnapshot, kCompletionSpriteCount> completion_sprites{};
    MusicMagicCompletionScoreSnapshot completion_score{};
    std::uint64_t revision = 0;
    std::size_t failed_item = kNoFailedItem;
    int score = 0;
    int target_score = 0;
    float player_x = 160.0f;
    float player_y = 148.0f;
    bool failed_item_visible = true;
};

class MusicMagicModel {
public:
    MusicMagicModel();
    ~MusicMagicModel();

    MusicMagicModel(const MusicMagicModel&) = delete;
    MusicMagicModel& operator=(const MusicMagicModel&) = delete;

    void start(std::uint32_t seed);
    void stop();
    void update(float delta_seconds);
    void moveLeft();
    void moveRight();

    MusicMagicSnapshot snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace music
