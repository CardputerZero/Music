#include "models/music_magic_model.hpp"

#include <games/core/components/area.hpp>
#include <games/core/components/shape.hpp>
#include <games/core/components/transform.hpp>
#include <games/core/world.hpp>
#include <games/dvd_screensaver/dvd_screensaver.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <random>

namespace music {
namespace {

namespace magic_core = smooth_ui_toolkit::games;
namespace dvd_magic = smooth_ui_toolkit::games::dvd_screensaver;
using Vector2 = smooth_ui_toolkit::Vector2;

constexpr int kPlayerGroup = 1;
constexpr int kItemGroup = 2;
constexpr int kCompletionSpriteGroup = 100;
constexpr int kCompletionScoreGroup = 110;
constexpr int kCompletionScore = 66;
constexpr std::array<float, 3> kLaneCenters{64.0f, 160.0f, 256.0f};
constexpr float kPlayerY = 148.0f;
constexpr float kSpawnY = -12.0f;
constexpr float kMissY = 174.0f;
constexpr float kMissDisplayY = 158.0f;
constexpr float kFailureHoldSeconds = 0.2f;
constexpr float kFailureBlinkStepSeconds = 0.12f;
constexpr int kFailureBlinkSteps = 7;
constexpr float kCompletionFieldWidth = 320.0f;
constexpr float kCompletionFieldHeight = 170.0f;
constexpr float kCompletionSpriteSize = 30.0f;
constexpr float kCompletionScoreWidth = 60.0f;
constexpr float kCompletionScoreHeight = 16.0f;

class CatcherObject final : public magic_core::GameObject {
public:
    CatcherObject()
    {
        groupId = kPlayerGroup;
        add(std::make_unique<magic_core::Transform>(Vector2{kLaneCenters[1], kPlayerY}));
        add(std::make_unique<magic_core::RectShape>(Vector2{26.0f, 26.0f}));
        add(std::make_unique<magic_core::Area>());
    }

    void reset()
    {
        _lane = 1;
        _target_x = kLaneCenters[1];
        _velocity_x = 0.0f;
        get<magic_core::Transform>()->position = {_target_x, kPlayerY};
    }

    void move(int offset)
    {
        _lane = std::clamp(_lane + offset, 0, static_cast<int>(kLaneCenters.size()) - 1);
        _target_x = kLaneCenters[static_cast<std::size_t>(_lane)];
    }

    void onUpdate(float delta_seconds) override
    {
        auto* transform = get<magic_core::Transform>();
        const float displacement = _target_x - transform->position.x;
        const float acceleration = displacement * 760.0f - _velocity_x * 44.0f;
        _velocity_x += acceleration * delta_seconds;
        transform->position.x += _velocity_x * delta_seconds;

        if (std::abs(displacement) < 0.02f && std::abs(_velocity_x) < 0.1f) {
            transform->position.x = _target_x;
            _velocity_x = 0.0f;
        }
    }

private:
    int _lane = 1;
    float _target_x = kLaneCenters[1];
    float _velocity_x = 0.0f;
};

class FallingItemObject final : public magic_core::GameObject {
public:
    using CatchCallback = std::function<void(FallingItemObject&)>;

    FallingItemObject(std::size_t slot, CatchCallback on_caught) : _slot(slot), _on_caught(std::move(on_caught))
    {
        groupId = kItemGroup;
        add(std::make_unique<magic_core::Transform>());
        add(std::make_unique<magic_core::RectShape>(Vector2{18.0f, 20.0f}));
        add(std::make_unique<magic_core::Area>());
        deactivate();
    }

    void onReady() override
    {
        get<magic_core::Area>()->onEntered.connect([this](magic_core::GameObject& other) {
            if (_active && other.groupId == kPlayerGroup) {
                _on_caught(*this);
            }
        });
    }

    void onUpdate(float delta_seconds) override
    {
        if (_active) {
            get<magic_core::Transform>()->position.y += _speed * delta_seconds;
        }
    }

    void activate(MusicMagicItemKind kind, int lane, float speed, std::uint8_t appearance)
    {
        _kind = kind;
        _speed = speed;
        _appearance = appearance;
        _active = true;
        get<magic_core::Transform>()->position = {kLaneCenters[static_cast<std::size_t>(lane)], kSpawnY};
        get<magic_core::RectShape>()->size =
            kind == MusicMagicItemKind::Note ? Vector2{18.0f, 20.0f} : Vector2{18.0f, 18.0f};
    }

    void deactivate()
    {
        _active = false;
        _speed = 0.0f;
        get<magic_core::Transform>()->position = {-1000.0f - static_cast<float>(_slot) * 40.0f, -1000.0f};
    }

    void showMissedAtBottom()
    {
        _speed = 0.0f;
        get<magic_core::Transform>()->position.y = kMissDisplayY;
    }

    std::size_t slot() const noexcept { return _slot; }
    MusicMagicItemKind kind() const noexcept { return _kind; }
    std::uint8_t appearance() const noexcept { return _appearance; }
    bool active() const noexcept { return _active; }
    Vector2 position() { return get<magic_core::Transform>()->position; }

private:
    std::size_t _slot = 0;
    CatchCallback _on_caught;
    MusicMagicItemKind _kind = MusicMagicItemKind::Note;
    std::uint8_t _appearance = 0;
    float _speed = 0.0f;
    bool _active = false;
};

}  // namespace

struct MusicMagicModel::Impl {
    Impl()
    {
        _world.init();
        _completion_world.init();
        _player = static_cast<CatcherObject*>(_world.createObject(std::make_unique<CatcherObject>()));
        for (std::size_t slot = 0; slot < _items.size(); ++slot) {
            _items[slot] = static_cast<FallingItemObject*>(_world.createObject(
                std::make_unique<FallingItemObject>(slot, [this](FallingItemObject& item) { onCaught(item); })));
        }
        addCompletionFrame();
        for (std::size_t slot = 0; slot < _completion_sprites.size(); ++slot) {
            _completion_sprites[slot] =
                static_cast<dvd_magic::Logo*>(_completion_world.createObject(std::make_unique<dvd_magic::Logo>(
                    kCompletionSpriteGroup + static_cast<int>(slot), Vector2{},
                    Vector2{kCompletionSpriteSize, kCompletionSpriteSize}, Vector2{}, 0.0f)));
        }
        _completion_score =
            static_cast<dvd_magic::Logo*>(_completion_world.createObject(std::make_unique<dvd_magic::Logo>(
                kCompletionScoreGroup, Vector2{}, Vector2{kCompletionScoreWidth, kCompletionScoreHeight}, Vector2{},
                0.0f)));
        _world.update(1.0f / 120.0f);
        _completion_world.update(1.0f / 120.0f);
        stop();
    }

    void start(std::uint32_t seed)
    {
        _rng.seed(seed);
        _phase = MusicMagicPhase::Playing;
        _score = 0;
        _spawn_count = 0;
        _last_spawn_lane = -1;
        _spawn_timer = 0.35f;
        resetFailure();
        _player->reset();
        for (auto* item : _items) {
            item->deactivate();
        }
        hideCompletionSprites();
        _world.update(1.0f / 120.0f);
        _completion_world.update(1.0f / 120.0f);
        ++_revision;
    }

    void stop()
    {
        _phase = MusicMagicPhase::Idle;
        _score = 0;
        _spawn_timer = 0.0f;
        resetFailure();
        _player->reset();
        for (auto* item : _items) {
            item->deactivate();
        }
        hideCompletionSprites();
        _world.update(1.0f / 120.0f);
        _completion_world.update(1.0f / 120.0f);
        ++_revision;
    }

    void update(float delta_seconds)
    {
        const float dt = std::clamp(delta_seconds, 0.0f, 0.1f);
        if (_phase == MusicMagicPhase::Completed) {
            _completion_world.update(dt);
            ++_revision;
            return;
        }
        if (_phase == MusicMagicPhase::Failing) {
            updateFailure(dt);
            return;
        }
        if (_phase != MusicMagicPhase::Playing) {
            return;
        }

        _spawn_timer -= dt;
        while (_spawn_timer <= 0.0f) {
            if (!spawnItem()) {
                _spawn_timer = 0.1f;
                break;
            }
            _spawn_timer += nextSpawnInterval();
        }

        _world.update(dt);
        if (_phase != MusicMagicPhase::Playing) {
            ++_revision;
            return;
        }

        for (auto* item : _items) {
            if (!item->active() || item->position().y <= kMissY) {
                continue;
            }
            if (item->kind() == MusicMagicItemKind::Note) {
                item->showMissedAtBottom();
                beginFailure(*item);
                break;
            }
            item->deactivate();
        }
        ++_revision;
    }

    void move(int offset)
    {
        if (_phase == MusicMagicPhase::Playing) {
            _player->move(offset);
            ++_revision;
        }
    }

    MusicMagicSnapshot snapshot() const
    {
        MusicMagicSnapshot result;
        result.phase = _phase;
        result.score = _score;
        result.target_score = kCompletionScore;
        result.revision = _revision;
        result.failed_item = _failed_item;
        result.failed_item_visible = _failed_item_visible;
        result.player_x = _player->get<magic_core::Transform>()->position.x;
        result.player_y = _player->get<magic_core::Transform>()->position.y;
        for (std::size_t slot = 0; slot < _items.size(); ++slot) {
            auto* item = _items[slot];
            const Vector2 position = item->position();
            result.items[slot] =
                MusicMagicItemSnapshot{item->kind(), position.x, position.y, item->appearance(), item->active()};
        }
        for (std::size_t slot = 0; slot < _completion_sprites.size(); ++slot) {
            const Vector2 position = _completion_sprites[slot]->get<magic_core::Transform>()->position;
            result.completion_sprites[slot] = MusicMagicCompletionSpriteSnapshot{position.x, position.y};
        }
        const Vector2 completion_score_position = _completion_score->get<magic_core::Transform>()->position;
        result.completion_score =
            MusicMagicCompletionScoreSnapshot{completion_score_position.x, completion_score_position.y};
        return result;
    }

private:
    magic_core::World _world;
    magic_core::World _completion_world;
    CatcherObject* _player = nullptr;
    std::array<FallingItemObject*, MusicMagicSnapshot::kMaxItems> _items{};
    std::array<dvd_magic::Logo*, MusicMagicSnapshot::kCompletionSpriteCount> _completion_sprites{};
    dvd_magic::Logo* _completion_score = nullptr;
    std::mt19937 _rng;
    MusicMagicPhase _phase = MusicMagicPhase::Idle;
    std::uint64_t _revision = 0;
    std::size_t _failed_item = MusicMagicSnapshot::kNoFailedItem;
    int _score = 0;
    int _spawn_count = 0;
    int _last_spawn_lane = -1;
    float _spawn_timer = 0.0f;
    float _failure_elapsed = 0.0f;
    bool _failed_item_visible = true;

    void addCompletionWall(Vector2 position, Vector2 size)
    {
        auto* wall = _completion_world.createObject(std::make_unique<magic_core::GameObject>());
        wall->groupId = static_cast<int>(dvd_magic::Group::Wall);
        wall->add(std::make_unique<magic_core::Transform>(position));
        wall->add(std::make_unique<magic_core::RectShape>(size));
        wall->add(std::make_unique<magic_core::Area>());
    }

    void addCompletionFrame()
    {
        addCompletionWall({kCompletionFieldWidth * 0.5f, 0.0f}, {kCompletionFieldWidth, 1.0f});
        addCompletionWall({kCompletionFieldWidth * 0.5f, kCompletionFieldHeight}, {kCompletionFieldWidth, 1.0f});
        addCompletionWall({0.0f, kCompletionFieldHeight * 0.5f}, {1.0f, kCompletionFieldHeight});
        addCompletionWall({kCompletionFieldWidth, kCompletionFieldHeight * 0.5f}, {1.0f, kCompletionFieldHeight});
    }

    void hideCompletionSprites()
    {
        for (std::size_t slot = 0; slot < _completion_sprites.size(); ++slot) {
            _completion_sprites[slot]->speed = 0.0f;
            _completion_sprites[slot]->direction = {};
            _completion_sprites[slot]->get<magic_core::Transform>()->position = {
                -1000.0f - static_cast<float>(slot) * 40.0f, -1200.0f};
        }
        _completion_score->speed = 0.0f;
        _completion_score->direction = {};
        _completion_score->get<magic_core::Transform>()->position = {-1400.0f, -1200.0f};
    }

    void onCaught(FallingItemObject& item)
    {
        if (_phase != MusicMagicPhase::Playing || !item.active()) {
            return;
        }
        if (item.kind() == MusicMagicItemKind::Hazard) {
            beginFailure(item);
            return;
        }
        ++_score;
        item.deactivate();
        if (_score >= kCompletionScore) {
            beginCompletion();
        }
    }

    void beginCompletion()
    {
        static const std::array<Vector2, MusicMagicSnapshot::kCompletionSpriteCount> positions{
            Vector2{52.0f, 35.0f},
            Vector2{128.0f, 105.0f},
            Vector2{210.0f, 48.0f},
            Vector2{276.0f, 126.0f},
        };
        static const std::array<Vector2, MusicMagicSnapshot::kCompletionSpriteCount> directions{
            Vector2{0.82f, 0.57f},
            Vector2{-0.71f, 0.71f},
            Vector2{0.64f, -0.77f},
            Vector2{-0.87f, -0.49f},
        };
        static constexpr std::array<float, MusicMagicSnapshot::kCompletionSpriteCount> speeds{
            52.0f,
            61.0f,
            68.0f,
            57.0f,
        };

        _phase = MusicMagicPhase::Completed;
        _spawn_timer = 0.0f;
        resetFailure();
        for (auto* item : _items) {
            item->deactivate();
        }
        for (std::size_t slot = 0; slot < _completion_sprites.size(); ++slot) {
            _completion_sprites[slot]->get<magic_core::Transform>()->position = positions[slot];
            _completion_sprites[slot]->direction = directions[slot].normalized();
            _completion_sprites[slot]->speed = speeds[slot];
        }
        _completion_score->get<magic_core::Transform>()->position = {160.0f, 145.0f};
        _completion_score->direction = Vector2{-0.76f, -0.65f}.normalized();
        _completion_score->speed = 58.0f;
    }

    void beginFailure(const FallingItemObject& item)
    {
        _phase = MusicMagicPhase::Failing;
        _failed_item = item.slot();
        _failure_elapsed = 0.0f;
        _failed_item_visible = true;
    }

    void resetFailure()
    {
        _failed_item = MusicMagicSnapshot::kNoFailedItem;
        _failure_elapsed = 0.0f;
        _failed_item_visible = true;
    }

    void updateFailure(float delta_seconds)
    {
        _failure_elapsed += delta_seconds;
        const float blink_elapsed = _failure_elapsed - kFailureHoldSeconds;
        if (blink_elapsed < 0.0f) {
            return;
        }

        const int blink_step = static_cast<int>(blink_elapsed / kFailureBlinkStepSeconds);
        if (blink_step >= kFailureBlinkSteps) {
            _phase = MusicMagicPhase::Finished;
            ++_revision;
            return;
        }

        const bool visible = (blink_step % 2) != 0;
        if (visible != _failed_item_visible) {
            _failed_item_visible = visible;
            ++_revision;
        }
    }

    bool spawnItem()
    {
        const auto available =
            std::find_if(_items.begin(), _items.end(), [](const auto* item) { return !item->active(); });
        if (available == _items.end()) {
            return false;
        }

        std::uniform_int_distribution<int> lane_distribution(0, static_cast<int>(kLaneCenters.size()) - 1);
        int lane = lane_distribution(_rng);
        if (lane == _last_spawn_lane) {
            lane = (lane + 1 + static_cast<int>(_rng() & 1U)) % static_cast<int>(kLaneCenters.size());
        }
        _last_spawn_lane = lane;

        const bool guaranteed_note = _spawn_count < 3;
        std::uniform_int_distribution<int> kind_distribution(0, 99);
        const MusicMagicItemKind kind =
            guaranteed_note || kind_distribution(_rng) >= 22 ? MusicMagicItemKind::Note : MusicMagicItemKind::Hazard;
        std::uniform_real_distribution<float> speed_jitter(-3.0f, 6.0f);
        const float speed = 60.0f + std::min(_score, 24) * 1.5f + speed_jitter(_rng);
        const std::uint8_t appearance = static_cast<std::uint8_t>(_rng() & 1U);
        (*available)->activate(kind, lane, speed, appearance);
        ++_spawn_count;
        return true;
    }

    float nextSpawnInterval()
    {
        std::uniform_real_distribution<float> jitter(0.0f, 0.12f);
        return std::max(0.48f, 0.88f - static_cast<float>(std::min(_score, 22)) * 0.017f) + jitter(_rng);
    }
};

MusicMagicModel::MusicMagicModel() : _impl(std::make_unique<Impl>()) {}

MusicMagicModel::~MusicMagicModel() = default;

void MusicMagicModel::start(std::uint32_t seed) { _impl->start(seed); }

void MusicMagicModel::stop() { _impl->stop(); }

void MusicMagicModel::update(float delta_seconds) { _impl->update(delta_seconds); }

void MusicMagicModel::moveLeft() { _impl->move(-1); }

void MusicMagicModel::moveRight() { _impl->move(1); }

MusicMagicSnapshot MusicMagicModel::snapshot() const { return _impl->snapshot(); }

}  // namespace music
