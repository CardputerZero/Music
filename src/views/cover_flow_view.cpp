#include "views/cover_flow_view.hpp"

#include "assets/font_assets.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>

namespace music {
namespace {

constexpr int kRenderWidth = 320;
constexpr int kRenderHeight = 170;
constexpr int kCoverTextureSize = 128;
constexpr std::size_t kMaxCachedCovers = 9;
constexpr float kPi = 3.14159265358979323846f;

struct Rgb {
    int red;
    int green;
    int blue;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

float smoothStep(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

std::uint64_t stableHash(const std::string& text)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char character : text) {
        hash ^= character;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

std::uint16_t packRgb565(Rgb color)
{
    const int red = std::clamp(color.red, 0, 255);
    const int green = std::clamp(color.green, 0, 255);
    const int blue = std::clamp(color.blue, 0, 255);
    return static_cast<std::uint16_t>(((red * 31 + 127) / 255) << 11U | ((green * 63 + 127) / 255) << 5U |
                                      (blue * 31 + 127) / 255);
}

Rgb scaleColor(Rgb color, float factor)
{
    return {static_cast<int>(std::lround(color.red * factor)), static_cast<int>(std::lround(color.green * factor)),
            static_cast<int>(std::lround(color.blue * factor))};
}

void fillRectangle(rendering::CoverImage& image, int x, int y, int width, int height, Rgb color)
{
    const int left = std::clamp(x, 0, image.width);
    const int right = std::clamp(x + width, 0, image.width);
    const int top = std::clamp(y, 0, image.height);
    const int bottom = std::clamp(y + height, 0, image.height);
    const std::uint16_t packed = packRgb565(color);
    for (int row = top; row < bottom; ++row) {
        auto begin = image.pixels.begin() + static_cast<std::size_t>(row * image.width + left);
        std::fill(begin, begin + (right - left), packed);
    }
}

void fillCircle(rendering::CoverImage& image, int center_x, int center_y, int radius, Rgb color)
{
    const std::uint16_t packed = packRgb565(color);
    const int radius_squared = radius * radius;
    const int top = std::max(center_y - radius, 0);
    const int bottom = std::min(center_y + radius, image.height - 1);
    for (int y = top; y <= bottom; ++y) {
        const int y_offset = y - center_y;
        const int half_width = static_cast<int>(std::floor(std::sqrt(radius_squared - y_offset * y_offset)));
        const int left = std::max(center_x - half_width, 0);
        const int right = std::min(center_x + half_width, image.width - 1);
        auto begin = image.pixels.begin() + static_cast<std::size_t>(y * image.width + left);
        std::fill(begin, begin + (right - left + 1), packed);
    }
}

void drawCircleOutline(rendering::CoverImage& image, int center_x, int center_y, int radius, int width, Rgb color)
{
    const int outer_squared = radius * radius;
    const int inner_squared = std::max(radius - width, 0) * std::max(radius - width, 0);
    const std::uint16_t packed = packRgb565(color);
    for (int y = std::max(center_y - radius, 0); y <= std::min(center_y + radius, image.height - 1); ++y) {
        for (int x = std::max(center_x - radius, 0); x <= std::min(center_x + radius, image.width - 1); ++x) {
            const int x_offset = x - center_x;
            const int y_offset = y - center_y;
            const int distance_squared = x_offset * x_offset + y_offset * y_offset;
            if (distance_squared <= outer_squared && distance_squared >= inner_squared) {
                image.pixels[static_cast<std::size_t>(y * image.width + x)] = packed;
            }
        }
    }
}

Vec3 subtract(Vec3 left, Vec3 right) { return {left.x - right.x, left.y - right.y, left.z - right.z}; }

float dot(Vec3 left, Vec3 right) { return left.x * right.x + left.y * right.y + left.z * right.z; }

Vec3 cross(Vec3 left, Vec3 right)
{
    return {left.y * right.z - left.z * right.y, left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

Vec3 normalized(Vec3 value)
{
    const float length = std::sqrt(dot(value, value));
    return length > 0.0f ? Vec3{value.x / length, value.y / length, value.z / length} : Vec3{};
}

rendering::PointF projectPoint(Vec3 point)
{
    constexpr Vec3 camera_position{0.0f, 0.18f, 5.2f};
    constexpr Vec3 camera_target{0.0f, 0.15f, 0.0f};
    constexpr Vec3 camera_up{0.0f, 1.0f, 0.0f};
    static const Vec3 forward = normalized(subtract(camera_target, camera_position));
    static const Vec3 right = normalized(cross(forward, camera_up));
    static const Vec3 up = cross(right, forward);
    static const float focal_length = static_cast<float>(kRenderHeight) / (2.0f * std::tan(38.0f * kPi / 360.0f));

    const Vec3 relative = subtract(point, camera_position);
    const float depth = std::max(dot(relative, forward), 0.001f);
    return {kRenderWidth * 0.5f + focal_length * dot(relative, right) / depth,
            kRenderHeight * 0.5f - focal_length * dot(relative, up) / depth};
}

rendering::ProjectedQuad projectCover(float center_x, float center_y, float center_z, float size, float yaw_degrees)
{
    const float half_size = size * 0.5f;
    const float yaw = yaw_degrees * kPi / 180.0f;
    const float cosine = std::cos(yaw);
    const float sine = std::sin(yaw);
    const auto corner = [=](float local_x, float local_y) {
        return projectPoint({center_x + local_x * cosine, center_y + local_y, center_z - local_x * sine});
    };
    return {corner(-half_size, half_size), corner(half_size, half_size), corner(half_size, -half_size),
            corner(-half_size, -half_size)};
}

bool intersectAreas(lv_area_t& result, const lv_area_t& first, const lv_area_t& second)
{
    result.x1 = std::max(first.x1, second.x1);
    result.y1 = std::max(first.y1, second.y1);
    result.x2 = std::min(first.x2, second.x2);
    result.y2 = std::min(first.y2, second.y2);
    return result.x1 <= result.x2 && result.y1 <= result.y2;
}

}  // namespace

CoverFlowView::CoverFlowView(CoverFlowViewModel& view_model) : _view_model(view_model) {}

CoverFlowView::~CoverFlowView() { destroyUi(); }

void CoverFlowView::onEnter(lv_obj_t* parent)
{
    createUi(parent);
    _render_dirty = true;
}

void CoverFlowView::onExit()
{
    destroyUi();
    _cover_cache.clear();
    _layer_count = 0;
}

void CoverFlowView::update(float delta_seconds)
{
    (void)delta_seconds;
    if (const auto snapshot = _view_model.snapshot(); snapshot && snapshot->revision != _snapshot_revision) {
        _snapshot_revision = snapshot->revision;
        pruneCoverCache(*snapshot);
        _render_dirty = true;
    }
}

void CoverFlowView::draw()
{
    if (!_root || !_root->isValid() || !_surface || !_surface->isValid()) {
        return;
    }

    ++_rendered_frame;
    const auto snapshot = _view_model.snapshot();
    const std::uint64_t revision = snapshot ? snapshot->revision : 0;
    const float position = snapshot ? _view_model.animatedPosition() : 0.0f;
    if (_render_dirty || revision != _rendered_revision || !std::isfinite(_rendered_position) ||
        std::abs(position - _rendered_position) > 0.0001f) {
        prepareFrame(snapshot.get());
        _rendered_revision = revision;
        _rendered_position = position;
        _render_dirty = false;
    }

    updateAlbumText(snapshot && !snapshot->albums.empty() ? _view_model.selectedAlbum() : nullptr);
    updateScanState();
}

void CoverFlowView::createUi(lv_obj_t* parent)
{
    if (_root && _root->isValid()) {
        return;
    }
    if (!parent) {
        return;
    }

    _root = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(parent);
    _root->setSize(kRenderWidth, kRenderHeight);
    _root->setPos(0, 0);
    _root->setBgOpa(LV_OPA_TRANSP);
    _root->setBorderWidth(0);
    _root->setShadowWidth(0);
    _root->setPaddingAll(0);
    _root->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _root->removeFlag(LV_OBJ_FLAG_CLICKABLE);

    _surface = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
    _surface->setSize(kRenderWidth, kRenderHeight);
    _surface->setPos(0, 0);
    _surface->setBgOpa(LV_OPA_TRANSP);
    _surface->setBorderWidth(0);
    _surface->setShadowWidth(0);
    _surface->setPaddingAll(0);
    _surface->removeFlag(LV_OBJ_FLAG_SCROLLABLE);
    _surface->removeFlag(LV_OBJ_FLAG_CLICKABLE);
    _surface->addEventCb(surfaceDrawEvent, LV_EVENT_DRAW_MAIN, this);

    _title_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
    _title_label->setSize(300, 24);
    _title_label->setPos(10, 127);
    _title_label->setLongMode(LV_LABEL_LONG_DOT);
    _title_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _title_label->setTextColor(lv_color_hex(0xf4f4f4));
    _title_label->setTextFont(font(FontFamily::Sans, FontSize::Px14));

    _detail_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
    _detail_label->setSize(300, 20);
    _detail_label->setPos(10, 146);
    _detail_label->setLongMode(LV_LABEL_LONG_DOT);
    _detail_label->setTextAlign(LV_TEXT_ALIGN_CENTER);
    _detail_label->setTextColor(lv_color_hex(0xAAAAAA));
    _detail_label->setTextFont(font(FontFamily::Sans, FontSize::Px12));

    _status_label = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Label>(_root->raw_ptr());
    _status_label->setTextColor(lv_color_hex(0xb4b6ba));
    _status_label->setTextFont(&lv_font_montserrat_12);

    _status_dot = std::make_unique<smooth_ui_toolkit::lvgl_cpp::Container>(_root->raw_ptr());
    _status_dot->setSize(4, 4);
    _status_dot->setRadius(LV_RADIUS_CIRCLE);
    _status_dot->setBgOpa(LV_OPA_COVER);
    _status_dot->setBorderWidth(0);
    _status_dot->setShadowWidth(0);
    _status_dot->setPaddingAll(0);
    _status_dot->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _shown_title.clear();
    _shown_detail.clear();
    _shown_status.clear();
    _title_label->setText("");
    _detail_label->setText("");
    _status_label->setText("");
    _status_label->setHidden(true);
    _status_dot->setHidden(true);
    _rendered_revision = std::numeric_limits<std::uint64_t>::max();
    _rendered_position = std::numeric_limits<float>::quiet_NaN();
    _render_dirty = true;
}

void CoverFlowView::destroyUi()
{
    _status_dot.reset();
    _status_label.reset();
    _detail_label.reset();
    _title_label.reset();
    _surface.reset();
    _root.reset();
}

CoverFlowView::CachedCover& CoverFlowView::coverFor(const Album& album)
{
    const std::string source = album.cover_path.string();
    auto iterator = _cover_cache.find(album.id);
    if (iterator != _cover_cache.end() && iterator->second.source != source) {
        _cover_cache.erase(iterator);
        iterator = _cover_cache.end();
    }

    if (iterator == _cover_cache.end()) {
        CachedCover cached;
        cached.image = loadCover(album);
        cached.source = source;
        iterator = _cover_cache.emplace(album.id, std::move(cached)).first;
    }
    iterator->second.last_used_frame = _rendered_frame;
    return iterator->second;
}

rendering::CoverImage CoverFlowView::loadCover(const Album& album) const
{
    if (!album.cover_path.empty() && std::filesystem::is_regular_file(album.cover_path)) {
        if (auto image = rendering::loadCoverImage(album.cover_path, kCoverTextureSize)) {
            return std::move(*image);
        }
    }
    return makeFallbackCover(album);
}

rendering::CoverImage CoverFlowView::makeFallbackCover(const Album& album) const
{
    constexpr std::array<Rgb, 8> palette = {
        Rgb{63, 204, 117}, Rgb{241, 92, 75},  Rgb{50, 132, 219},  Rgb{244, 185, 66},
        Rgb{53, 189, 181}, Rgb{217, 84, 127}, Rgb{141, 105, 210}, Rgb{222, 222, 222},
    };
    const std::uint64_t hash = stableHash(album.id);
    const Rgb base = palette[hash % palette.size()];
    const Rgb dark = scaleColor(base, 0.32f);
    const Rgb light = scaleColor(base, 1.15f);

    rendering::CoverImage image;
    image.width = kCoverTextureSize;
    image.height = kCoverTextureSize;
    image.pixels.resize(static_cast<std::size_t>(image.width * image.height));
    fillRectangle(image, 0, 0, image.width, image.height, dark);
    fillRectangle(image, 0, 0, image.width, 36, base);
    fillRectangle(image, 0, 92, image.width, 36, scaleColor(base, 0.62f));

    if (album.all_music) {
        fillCircle(image, 64, 64, 41, Rgb{18, 19, 21});
        drawCircleOutline(image, 64, 64, 41, 1, light);
        drawCircleOutline(image, 64, 64, 29, 1, scaleColor(base, 0.78f));
        fillCircle(image, 64, 64, 11, base);
        fillCircle(image, 64, 64, 3, Rgb{232, 232, 232});
    } else {
        const int inset = 16 + static_cast<int>(hash % 12);
        fillRectangle(image, inset, 42, 96 - inset, 44, light);
        fillCircle(image, 88, 57, 21, base);
        fillCircle(image, 42, 75, 12, Rgb{238, 238, 238});
    }
    return image;
}

void CoverFlowView::pruneCoverCache(const LibrarySnapshot& snapshot)
{
    std::unordered_set<std::string> active;
    active.reserve(snapshot.albums.size());
    for (const auto& album : snapshot.albums) {
        active.insert(album.id);
    }

    for (auto iterator = _cover_cache.begin(); iterator != _cover_cache.end();) {
        iterator = active.count(iterator->first) == 0 ? _cover_cache.erase(iterator) : std::next(iterator);
    }
}

void CoverFlowView::trimCoverCache()
{
    while (_cover_cache.size() > kMaxCachedCovers) {
        const auto oldest =
            std::min_element(_cover_cache.begin(), _cover_cache.end(), [](const auto& left, const auto& right) {
                return left.second.last_used_frame < right.second.last_used_frame;
            });
        if (oldest == _cover_cache.end()) {
            return;
        }
        _cover_cache.erase(oldest);
    }
}

void CoverFlowView::prepareFrame(const LibrarySnapshot* snapshot)
{
    _layers = {};
    _layer_count = 0;

    if (snapshot && !snapshot->albums.empty()) {
        const float position = _view_model.animatedPosition();
        for (std::size_t index = 0; index < snapshot->albums.size() && _layer_count < _layers.size(); ++index) {
            const float relative = static_cast<float>(index) - position;
            const float absolute = std::abs(relative);
            if (absolute > 4.0f) {
                continue;
            }

            const float fade = 1.0f - smoothStep((absolute - 3.15f) / 0.85f);
            const std::uint8_t opacity = static_cast<std::uint8_t>(std::lround(255.0f * std::clamp(fade, 0.0f, 1.0f)));
            if (opacity == 0) {
                continue;
            }

            const float side = relative < 0.0f ? -1.0f : 1.0f;
            const float bend = smoothStep(std::min(absolute, 1.0f));
            const float extra = std::max(absolute - 1.0f, 0.0f);
            const float x = absolute < 0.001f ? 0.0f : side * (1.44f * bend + extra * 0.62f);
            const float z = 0.22f - 0.72f * bend - extra * 0.10f;
            const float yaw = -side * 48.0f * bend;
            const float size = 2.08f * (1.0f - 0.035f * bend);
            CachedCover& cached = coverFor(snapshot->albums[index]);

            rendering::CoverLayer& layer = _layers[_layer_count++];
            layer.image = {cached.image.pixels.data(), cached.image.width, cached.image.height, cached.image.width};
            layer.quad = projectCover(x, 0.48f, z, size, yaw);
            layer.depth = z;
            layer.style.brightness = static_cast<std::uint8_t>(std::lround(255.0f * (1.0f - 0.22f * bend)));
            layer.style.opacity = opacity;
            layer.style.filter = rendering::TextureFilter::Bilinear;
            layer.reflection.enabled = true;
            layer.reflection.gap = 1.0f;
            layer.reflection.length = 0.29f;
            layer.reflection.brightness = 255;
            layer.reflection.opacity = 174;
            layer.reflection.top_opacity = 255;
            layer.reflection.bottom_opacity = 12;
        }

        trimCoverCache();
    }

    lv_obj_invalidate(_surface->raw_ptr());
}

void CoverFlowView::renderSurface(lv_layer_t* layer)
{
    if (!layer || !layer->draw_buf || !_surface || !_surface->isValid()) {
        return;
    }

    lv_draw_buf_t* draw_buffer = layer->draw_buf;
    const bool valid_format =
        layer->color_format == LV_COLOR_FORMAT_RGB565 && draw_buffer->header.cf == LV_COLOR_FORMAT_RGB565;
    const bool valid_stride = draw_buffer->header.stride % sizeof(std::uint16_t) == 0 &&
                              draw_buffer->header.stride / sizeof(std::uint16_t) >= draw_buffer->header.w;
    const bool valid_buffer =
        draw_buffer->data && draw_buffer->header.w > 0 && draw_buffer->header.h > 0 && valid_format && valid_stride;
    if (!valid_buffer) {
        if (!_draw_buffer_error_reported) {
            const int buffer_width = draw_buffer->header.w;
            const int buffer_height = draw_buffer->header.h;
            const int buffer_stride = draw_buffer->header.stride;
            spdlog::error(
                "Music Cover Flow: expected an RGB565 LVGL draw buffer (format={}, layer={}, {}x{}, "
                "stride={})",
                static_cast<int>(draw_buffer->header.cf), static_cast<int>(layer->color_format), buffer_width,
                buffer_height, buffer_stride);
            _draw_buffer_error_reported = true;
        }
        return;
    }

    lv_area_t surface_area;
    lv_obj_get_coords(_surface->raw_ptr(), &surface_area);
    lv_area_t clipped_area;
    if (!intersectAreas(clipped_area, layer->_clip_area, surface_area) ||
        !intersectAreas(clipped_area, clipped_area, layer->buf_area)) {
        return;
    }

    const int buffer_x = clipped_area.x1 - layer->buf_area.x1;
    const int buffer_y = clipped_area.y1 - layer->buf_area.y1;
    const int width = lv_area_get_width(&clipped_area);
    const int height = lv_area_get_height(&clipped_area);
    if (buffer_x < 0 || buffer_y < 0 || width <= 0 || height <= 0 ||
        buffer_x + width > static_cast<int>(draw_buffer->header.w) ||
        buffer_y + height > static_cast<int>(draw_buffer->header.h)) {
        if (!_draw_buffer_error_reported) {
            const int buffer_width = draw_buffer->header.w;
            const int buffer_height = draw_buffer->header.h;
            spdlog::error(
                "Music Cover Flow: invalid LVGL partial buffer mapping (buffer={}x{}, area=({}, {}) "
                "{}x{})",
                buffer_width, buffer_height, buffer_x, buffer_y, width, height);
            _draw_buffer_error_reported = true;
        }
        return;
    }

    // LVGL's software renderer runs on a worker thread. Finish every task
    // submitted before this draw event before writing the same buffer directly.
    // This is the same drain sequence LVGL uses before flushing a draw buffer.
    while (layer->draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        lv_draw_dispatch();
    }

    lv_area_t buffer_area{buffer_x, buffer_y, buffer_x + width - 1, buffer_y + height - 1};
    lv_draw_buf_invalidate_cache(draw_buffer, &buffer_area);
    auto* pixels = static_cast<std::uint16_t*>(lv_draw_buf_goto_xy(draw_buffer, buffer_x, buffer_y));
    const int stride_pixels = static_cast<int>(draw_buffer->header.stride / sizeof(std::uint16_t));
    const int origin_x = clipped_area.x1 - surface_area.x1;
    const int origin_y = clipped_area.y1 - surface_area.y1;

    for (int row = 0; row < height; ++row) {
        const int screen_y = origin_y + row;
        const float amount = 1.0f - static_cast<float>(screen_y) / static_cast<float>(kRenderHeight - 1);
        const std::uint16_t color =
            packRgb565({static_cast<int>(std::lround(13.0f * amount)), static_cast<int>(std::lround(14.0f * amount)),
                        static_cast<int>(std::lround(16.0f * amount))});
        std::fill_n(pixels + static_cast<std::size_t>(row) * stride_pixels, width, color);
    }

    rendering::CoverRasterizer::drawLayers({pixels, width, height, stride_pixels, origin_x, origin_y}, _layers.data(),
                                           _layer_count);
    lv_draw_buf_flush_cache(draw_buffer, &buffer_area);
}

void CoverFlowView::surfaceDrawEvent(lv_event_t* event)
{
    auto* self = static_cast<CoverFlowView*>(lv_event_get_user_data(event));
    if (self) {
        self->renderSurface(lv_event_get_layer(event));
    }
}

void CoverFlowView::updateAlbumText(const Album* album)
{
    const std::string title = album ? (album->title.empty() ? "Untitled Album" : album->title) : std::string{};
    const std::string artist =
        album ? (album->all_music ? "Various Artists" : (album->artist.empty() ? "Unknown Artist" : album->artist))
              : std::string{};
    if (title != _shown_title) {
        _shown_title = title;
        _title_label->setText(_shown_title);
    }
    if (artist != _shown_detail) {
        _shown_detail = artist;
        _detail_label->setText(_shown_detail);
    }
}

void CoverFlowView::updateScanState()
{
    const ScanState state = _view_model.scanState();
    const bool visible =
        state.phase != ScanPhase::Complete && state.phase != ScanPhase::Idle && state.phase != ScanPhase::Stopped;
    if (!visible) {
        _status_label->setHidden(true);
        _status_dot->setHidden(true);
        _shown_status.clear();
        _shown_scan_phase = state.phase;
        return;
    }

    const bool failed = state.phase == ScanPhase::Error;
    const std::string status = failed ? "LIBRARY ERROR" : "SCANNING " + std::to_string(state.files_discovered);
    if (status != _shown_status || state.phase != _shown_scan_phase) {
        _shown_status = status;
        _shown_scan_phase = state.phase;
        _status_label->setText(_shown_status);
        _status_dot->setBgColor(lv_color_hex(failed ? 0xf15c4b : 0x3fcc75));
        _status_label->setHidden(false);
        _status_dot->setHidden(false);
        _status_label->align(LV_ALIGN_TOP_RIGHT, -7, 3);
        lv_obj_update_layout(_root->raw_ptr());
        _status_dot->alignTo(*_status_label, LV_ALIGN_OUT_LEFT_MID, -4, 0);
    }
}

}  // namespace music
