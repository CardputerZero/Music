#pragma once

#include "rendering/cover_image_loader.hpp"
#include "rendering/cover_rasterizer.hpp"
#include "view_models/cover_flow_view_model.hpp"
#include "views/view.hpp"

#include <cstdint>
#include <array>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

#include <lvgl/lvgl_cpp/label.hpp>
#include <lvgl/lvgl_cpp/obj.hpp>

namespace music {

class CoverFlowView final : public View {
public:
    explicit CoverFlowView(CoverFlowViewModel& view_model);
    ~CoverFlowView() override;

    void onEnter(lv_obj_t* parent) override;
    void onExit() override;
    void update(float delta_seconds) override;
    void draw() override;

private:
    struct CachedCover {
        rendering::CoverImage image;
        std::string source;
        std::uint64_t last_used_frame = 0;
    };

    CoverFlowViewModel& _view_model;
    std::unordered_map<std::string, CachedCover> _cover_cache;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _root;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _surface;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _detail_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _status_dot;
    std::array<rendering::CoverLayer, 9> _layers{};
    std::size_t _layer_count = 0;
    std::string _shown_title;
    std::string _shown_detail;
    std::string _shown_status;
    std::uint64_t _snapshot_revision = 0;
    std::uint64_t _rendered_revision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t _rendered_frame = 0;
    float _rendered_position = std::numeric_limits<float>::quiet_NaN();
    ScanPhase _shown_scan_phase = ScanPhase::Idle;
    bool _render_dirty = true;
    bool _draw_buffer_error_reported = false;

    void createUi(lv_obj_t* parent);
    void destroyUi();
    CachedCover& coverFor(const Album& album);
    rendering::CoverImage loadCover(const Album& album) const;
    rendering::CoverImage makeFallbackCover(const Album& album) const;
    void pruneCoverCache(const LibrarySnapshot& snapshot);
    void trimCoverCache();
    void prepareFrame(const LibrarySnapshot* snapshot);
    void renderSurface(lv_layer_t* layer);
    void updateAlbumText(const Album* album);
    void updateScanState();
    static void surfaceDrawEvent(lv_event_t* event);
};

}  // namespace music
