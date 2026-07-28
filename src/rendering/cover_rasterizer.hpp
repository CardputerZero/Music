#pragma once

#include <cstddef>
#include <cstdint>

namespace music::rendering {

struct PointF {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rgb565Surface {
    std::uint16_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
    // Screen-space position represented by pixels[0]. This lets the same
    // projected geometry render directly into an LVGL partial draw buffer.
    int origin_x = 0;
    int origin_y = 0;
};

struct Rgb565ImageView {
    const std::uint16_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
};

// Corners are ordered clockwise in screen space. Any convex perspective
// projection of a rectangular cover can be represented by this quad.
struct ProjectedQuad {
    PointF top_left;
    PointF top_right;
    PointF bottom_right;
    PointF bottom_left;
};

enum class TextureFilter {
    Nearest,
    Bilinear,
};

struct DrawStyle {
    std::uint8_t brightness = 255;
    std::uint8_t opacity = 255;
    std::uint8_t top_opacity = 255;
    std::uint8_t bottom_opacity = 255;
    bool flip_horizontal = false;
    bool flip_vertical = false;
    TextureFilter filter = TextureFilter::Bilinear;
};

struct ReflectionStyle {
    bool enabled = false;
    float gap = 0.0f;
    float length = 0.45f;
    // Fraction of the source image, measured upward from its bottom edge,
    // that remains visible in the reflection.
    float source_length = 0.5f;
    std::uint8_t brightness = 210;
    std::uint8_t opacity = 150;
    std::uint8_t top_opacity = 255;
    std::uint8_t bottom_opacity = 0;
};

struct CoverLayer {
    Rgb565ImageView image;
    ProjectedQuad quad;
    // Smaller values are farther away and are rendered first.
    float depth = 0.0f;
    DrawStyle style;
    ReflectionStyle reflection;
};

class CoverRasterizer {
public:
    // Returns the number of destination pixels blended. Invalid or degenerate
    // inputs are ignored and return zero.
    static std::size_t draw(Rgb565Surface destination, Rgb565ImageView source, const ProjectedQuad& quad,
                            const DrawStyle& style = {});

    static ProjectedQuad reflectedQuad(const ProjectedQuad& quad, float gap, float length);

    static std::size_t drawReflection(Rgb565Surface destination, Rgb565ImageView source, const ProjectedQuad& quad,
                                      const DrawStyle& cover_style, const ReflectionStyle& reflection);

    // Layers may be supplied in any order. They are stably rendered from the
    // smallest depth to the largest, including each layer's reflection.
    static std::size_t drawLayers(Rgb565Surface destination, const CoverLayer* layers, std::size_t count);
};

}  // namespace music::rendering
