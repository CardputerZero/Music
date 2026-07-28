#include "views/music_magic_view.hpp"

#include "assets/font_assets.hpp"
#include "assets/runtime_assets.hpp"
#include "models/music_magic_model.hpp"
#include "view_models/info_page_view_model.hpp"

#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace music {
namespace {

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 170;
constexpr int kNoteWidth = 24;
constexpr int kNoteHeight = 24;
constexpr int kHazardWidth = 24;
constexpr int kHazardHeight = 24;
constexpr int kCatcherWidth = 30;
constexpr int kCatcherHeight = 30;
constexpr int kCompletionScoreWidth = 60;
constexpr float kThankYouAnimationPeriod = 4.8f;
constexpr float kTau = 6.28318530717958647692f;
constexpr std::array<float, MusicMagicSnapshot::kCompletionSpriteCount> kCompletionRotationPeriods{
    3.7f,
    4.6f,
    3.2f,
    5.1f,
};
constexpr std::array<float, MusicMagicSnapshot::kCompletionSpriteCount> kCompletionRotationPhases{
    0.2f,
    1.7f,
    3.4f,
    5.0f,
};
constexpr std::array<float, MusicMagicSnapshot::kCompletionSpriteCount> kCompletionRotationAmplitudes{
    48.0f,
    60.0f,
    42.0f,
    54.0f,
};

lv_color_t lvColor(ui::Color color) { return lv_color_make(color.red, color.green, color.blue); }

void drawSprite(lv_layer_t* layer, const std::string& path, int width, int height, float center_x, float center_y,
                lv_color_t color, int32_t rotation = 0, lv_opa_t opacity = LV_OPA_COVER)
{
    lv_draw_image_dsc_t descriptor;
    lv_draw_image_dsc_init(&descriptor);
    descriptor.src = path.c_str();
    descriptor.recolor = color;
    descriptor.recolor_opa = LV_OPA_COVER;
    descriptor.opa = opacity;
    descriptor.rotation = rotation;
    descriptor.pivot = {width / 2, height / 2};
    descriptor.antialias = 1;

    const int left = static_cast<int>(std::lround(center_x)) - width / 2;
    const int top = static_cast<int>(std::lround(center_y)) - height / 2;
    const lv_area_t area{left, top, left + width - 1, top + height - 1};
    lv_draw_image(layer, &descriptor, &area);
}

int completionSpriteRotation(std::size_t index, float elapsed_seconds)
{
    const float phase = elapsed_seconds * kTau / kCompletionRotationPeriods[index] + kCompletionRotationPhases[index];
    return static_cast<int>(std::lround(std::sin(phase) * kCompletionRotationAmplitudes[index]));
}

void drawCompletionScore(lv_layer_t* layer, int score, int target_score, float center_x, float center_y,
                         lv_color_t color)
{
    char text[20];
    std::snprintf(text, sizeof(text), "%d/%d", score, target_score);

    const lv_font_t* score_font = font(FontFamily::Sans, FontSize::Px12);
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.text = text;
    descriptor.text_local = true;
    descriptor.font = score_font;
    descriptor.color = color;
    descriptor.align = LV_TEXT_ALIGN_CENTER;

    const int height = lv_font_get_line_height(score_font);
    const int left = static_cast<int>(std::lround(center_x)) - kCompletionScoreWidth / 2;
    const int top = static_cast<int>(std::lround(center_y)) - height / 2;
    const lv_area_t area{left, top, left + kCompletionScoreWidth - 1, top + height - 1};
    lv_draw_label(layer, &descriptor, &area);
}

}  // namespace

struct MusicMagicView::Impl {
    Impl(lv_obj_t* parent, InfoPageViewModel& view_model, ui::PageTheme theme)
        : view_model(view_model),
          theme(theme),
          note_a_path(assetPath("images/music_magic_note_a.png").string()),
          note_b_path(assetPath("images/music_magic_note_b.png").string()),
          hazard_path(assetPath("images/music_magic_hazard.png").string()),
          catcher_path(assetPath("images/music_magic_catcher.png").string())
    {
        root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
        root->setSize(kScreenWidth, kScreenHeight);
        root->setPos(0, 0);
        root->setBgOpa(LV_OPA_COVER);
        root->setBorderWidth(0);
        root->setRadius(0);
        root->setShadowWidth(0);
        root->setPaddingAll(0);
        root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

        thank_you = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(root->raw_ptr());
        const int thank_you_height = lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px18));
        thank_you->setSize(kScreenWidth, thank_you_height);
        thank_you_base_y = (kScreenHeight - thank_you_height) / 2 - 5;
        thank_you->setPos(0, thank_you_base_y);
        thank_you->setLongMode(LV_LABEL_LONG_CLIP);
        thank_you->setTextAlign(LV_TEXT_ALIGN_CENTER);
        thank_you->setTextFont(font(FontFamily::Sans, FontSize::Px18));
        thank_you->setText("Thank you!");
        thank_you->setTransformPivot(kScreenWidth / 2, thank_you_height / 2);

        score = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(root->raw_ptr());
        score->setSize(72, lv_font_get_line_height(font(FontFamily::Sans, FontSize::Px12)));
        score->setPos(240, 5);
        score->setLongMode(LV_LABEL_LONG_CLIP);
        score->setTextAlign(LV_TEXT_ALIGN_RIGHT);
        score->setTextFont(font(FontFamily::Sans, FontSize::Px12));

        surface = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(root->raw_ptr());
        surface->setSize(kScreenWidth, kScreenHeight);
        surface->setPos(0, 0);
        surface->setBgOpa(LV_OPA_TRANSP);
        surface->setBorderWidth(0);
        surface->setRadius(0);
        surface->setShadowWidth(0);
        surface->setPaddingAll(0);
        surface->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
        surface->removeFlag(LV_OBJ_FLAG_CLICKABLE);
        surface->addEventCb(onDraw, LV_EVENT_DRAW_MAIN, this);

        applyTheme();
        refresh(true);
    }

    InfoPageViewModel& view_model;
    ui::PageTheme theme;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> surface;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> score;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> thank_you;
    std::string note_a_path;
    std::string note_b_path;
    std::string hazard_path;
    std::string catcher_path;
    MusicMagicSnapshot snapshot;
    std::uint64_t shown_revision = 0;
    int shown_score = -1;
    int shown_target_score = -1;
    int thank_you_base_y = 0;
    float thank_you_animation_time = 0.0f;
    float completion_sprite_animation_time = 0.0f;
    bool thank_you_animating = false;

    void setTheme(ui::PageTheme next_theme)
    {
        theme = next_theme;
        applyTheme();
        if (surface) {
            lv_obj_invalidate(surface->raw_ptr());
        }
    }

    void update(float delta_seconds)
    {
        refresh(false);
        if (snapshot.phase == MusicMagicPhase::Completed) {
            completion_sprite_animation_time += std::clamp(delta_seconds, 0.0f, 0.1f);
        } else {
            completion_sprite_animation_time = 0.0f;
        }
        updateThankYouAnimation(delta_seconds);
    }

    void refresh(bool force)
    {
        const MusicMagicSnapshot next = view_model.magicSnapshot();
        if (!force && next.revision == shown_revision) {
            return;
        }
        snapshot = next;
        shown_revision = next.revision;

        if (force || shown_score != snapshot.score || shown_target_score != snapshot.target_score) {
            shown_score = snapshot.score;
            shown_target_score = snapshot.target_score;
            char score_text[20];
            std::snprintf(score_text, sizeof(score_text), "%d / %d", shown_score, shown_target_score);
            score->setText(score_text);
        }
        const bool completed = snapshot.phase == MusicMagicPhase::Completed;
        score->setHidden(completed);
        thank_you->setHidden(!completed);
        lv_obj_invalidate(surface->raw_ptr());
    }

    void updateThankYouAnimation(float delta_seconds)
    {
        if (snapshot.phase != MusicMagicPhase::Completed) {
            if (thank_you_animating) {
                thank_you_animating = false;
                thank_you_animation_time = 0.0f;
                thank_you->setX(0);
                thank_you->setY(thank_you_base_y);
                thank_you->setRotation(0);
                lv_obj_set_style_transform_scale_x(thank_you->raw_ptr(), 256, LV_PART_MAIN);
                lv_obj_set_style_transform_scale_y(thank_you->raw_ptr(), 256, LV_PART_MAIN);
            }
            return;
        }

        thank_you_animating = true;
        thank_you_animation_time =
            std::fmod(thank_you_animation_time + std::clamp(delta_seconds, 0.0f, 0.1f), kThankYouAnimationPeriod);
        const float phase = thank_you_animation_time * kTau / kThankYouAnimationPeriod;
        const float bounce = std::sin(phase);
        const float wobble = std::sin(phase * 2.0f);
        const float drift = std::cos(phase) * 3.0f + std::sin(phase * 3.0f) * 1.0f;
        thank_you->setX(static_cast<int>(std::lround(drift)));
        thank_you->setY(thank_you_base_y + static_cast<int>(std::lround(bounce * 5.0f + wobble * 1.5f)));
        thank_you->setRotation(static_cast<int>(std::lround(wobble * 30.0f)));
        lv_obj_set_style_transform_scale_x(thank_you->raw_ptr(), 256 + static_cast<int>(std::lround(wobble * 10.0f)),
                                           LV_PART_MAIN);
        lv_obj_set_style_transform_scale_y(thank_you->raw_ptr(), 256 - static_cast<int>(std::lround(wobble * 8.0f)),
                                           LV_PART_MAIN);
    }

    void applyTheme()
    {
        const lv_color_t background = lvColor(theme.background);
        const lv_color_t secondary = lvColor(theme.secondary_text);
        const lv_color_t highlight = lvColor(theme.accent);
        root->setBgColor(background);
        score->setTextColor(secondary);
        thank_you->setTextColor(highlight);
    }

    void draw(lv_event_t* event)
    {
        lv_layer_t* layer = lv_event_get_layer(event);
        if (!layer || !surface) {
            return;
        }

        lv_area_t coordinates;
        lv_obj_get_coords(surface->raw_ptr(), &coordinates);
        const int origin_x = coordinates.x1;
        const int origin_y = coordinates.y1;
        const lv_color_t primary = lvColor(theme.primary_text);

        if (snapshot.phase == MusicMagicPhase::Completed) {
            const auto& sprites = snapshot.completion_sprites;
            drawSprite(layer, note_a_path, kNoteWidth, kNoteHeight, origin_x + sprites[0].x, origin_y + sprites[0].y,
                       primary, completionSpriteRotation(0, completion_sprite_animation_time));
            drawSprite(layer, note_b_path, kNoteWidth, kNoteHeight, origin_x + sprites[1].x, origin_y + sprites[1].y,
                       primary, completionSpriteRotation(1, completion_sprite_animation_time));
            drawSprite(layer, hazard_path, kHazardWidth, kHazardHeight, origin_x + sprites[2].x,
                       origin_y + sprites[2].y, primary, completionSpriteRotation(2, completion_sprite_animation_time));
            drawSprite(layer, catcher_path, kCatcherWidth, kCatcherHeight, origin_x + sprites[3].x,
                       origin_y + sprites[3].y, primary, completionSpriteRotation(3, completion_sprite_animation_time));
            drawCompletionScore(layer, snapshot.target_score, snapshot.target_score,
                                origin_x + snapshot.completion_score.x, origin_y + snapshot.completion_score.y,
                                primary);
            return;
        }

        for (std::size_t slot = 0; slot < snapshot.items.size(); ++slot) {
            const auto& item = snapshot.items[slot];
            if (!item.active) {
                continue;
            }
            if (slot == snapshot.failed_item && !snapshot.failed_item_visible) {
                continue;
            }
            if (item.kind == MusicMagicItemKind::Note) {
                const std::string& note_path = item.appearance == 0 ? note_a_path : note_b_path;
                drawSprite(layer, note_path, kNoteWidth, kNoteHeight, origin_x + item.x, origin_y + item.y, primary);
            } else {
                drawSprite(layer, hazard_path, kHazardWidth, kHazardHeight, origin_x + item.x, origin_y + item.y,
                           primary);
            }
        }
        drawSprite(layer, catcher_path, kCatcherWidth, kCatcherHeight, origin_x + snapshot.player_x,
                   origin_y + snapshot.player_y, primary);
    }

    static void onDraw(lv_event_t* event)
    {
        auto* self = static_cast<Impl*>(lv_event_get_user_data(event));
        if (self) {
            self->draw(event);
        }
    }
};

MusicMagicView::MusicMagicView(InfoPageViewModel& view_model) : _view_model(view_model) {}

MusicMagicView::~MusicMagicView() = default;

void MusicMagicView::setTheme(ui::PageTheme theme)
{
    _theme = theme;
    if (_impl) {
        _impl->setTheme(theme);
    }
}

void MusicMagicView::onEnter(lv_obj_t* parent)
{
    if (!_impl && parent) {
        _impl = std::make_unique<Impl>(parent, _view_model, _theme);
    }
}

void MusicMagicView::onExit() { _impl.reset(); }

void MusicMagicView::update(float delta_seconds)
{
    if (_impl) {
        _impl->update(delta_seconds);
    }
}

void MusicMagicView::draw() {}

}  // namespace music
