#include "rendering/cover_rasterizer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using music::rendering::CoverLayer;
using music::rendering::CoverRasterizer;
using music::rendering::DrawStyle;
using music::rendering::ProjectedQuad;
using music::rendering::ReflectionStyle;
using music::rendering::Rgb565ImageView;
using music::rendering::Rgb565Surface;
using music::rendering::TextureFilter;

constexpr std::uint16_t kBlack = 0x0000;
constexpr std::uint16_t kRed = 0xf800;
constexpr std::uint16_t kGreen = 0x07e0;
constexpr std::uint16_t kBlue = 0x001f;
constexpr std::uint16_t kWhite = 0xffff;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int red(std::uint16_t color) { return ((color >> 11U) & 0x1fU) * 255 / 31; }

int green(std::uint16_t color) { return ((color >> 5U) & 0x3fU) * 255 / 63; }

int blue(std::uint16_t color) { return (color & 0x1fU) * 255 / 31; }

void testCenteredCover()
{
    const std::array<std::uint16_t, 4> source = {kRed, kGreen, kBlue, kWhite};
    std::array<std::uint16_t, 49> destination{};
    DrawStyle style;
    style.filter = TextureFilter::Nearest;
    const std::size_t count = CoverRasterizer::draw({destination.data(), 7, 7, 7}, {source.data(), 2, 2, 2},
                                                    {{1, 1}, {5, 1}, {5, 5}, {1, 5}}, style);

    require(count == 16, "centered cover pixel count mismatch");
    require(destination[1 * 7 + 1] == kRed && destination[1 * 7 + 4] == kGreen, "top cover texels were not mapped");
    require(destination[4 * 7 + 1] == kBlue && destination[4 * 7 + 4] == kWhite, "bottom cover texels were not mapped");
    require(destination[0] == kBlack && destination[6 * 7 + 6] == kBlack, "cover escaped its projected bounds");
}

void testBoundsAndInvalidInputs()
{
    constexpr std::uint16_t guard = 0x5a5a;
    const std::array<std::uint16_t, 1> source = {kWhite};
    std::array<std::uint16_t, 18> guarded{};
    guarded.fill(kBlack);
    guarded.front() = guard;
    guarded.back() = guard;
    const Rgb565Surface surface{guarded.data() + 1, 4, 4, 4};

    const std::size_t count =
        CoverRasterizer::draw(surface, {source.data(), 1, 1, 1}, {{-100, -100}, {100, -80}, {90, 100}, {-90, 80}});
    require(count == 16, "clipped cover did not fill the destination");
    require(guarded.front() == guard && guarded.back() == guard, "rasterizer wrote beyond the destination");
    require(CoverRasterizer::draw({guarded.data() + 1, 4, 4, 3}, {source.data(), 1, 1, 1},
                                  {{0, 0}, {4, 0}, {4, 4}, {0, 4}}) == 0,
            "invalid destination stride was accepted");
    require(CoverRasterizer::draw(surface, {source.data(), 1, 1, 1}, {{0, 0}, {2, 0}, {4, 0}, {0, 4}}) == 0,
            "degenerate quad was accepted");

    std::array<std::uint16_t, 12> padded{};
    padded.fill(guard);
    require(CoverRasterizer::draw({padded.data(), 3, 2, 6}, {source.data(), 1, 1, 1},
                                  {{0, 0}, {3, 0}, {3, 2}, {0, 2}}) == 6,
            "padded surface coverage mismatch");
    require(padded[3] == guard && padded[4] == guard && padded[5] == guard && padded[9] == guard &&
                padded[10] == guard && padded[11] == guard,
            "rasterizer wrote into row padding");
}

void testSideTrapezoid()
{
    const std::array<std::uint16_t, 4> source = {kRed, kGreen, kBlue, kWhite};
    std::array<std::uint16_t, 100> destination{};
    DrawStyle style;
    style.filter = TextureFilter::Nearest;
    const ProjectedQuad side{{1, 1}, {7, 3}, {7, 7}, {1, 9}};
    const std::size_t count =
        CoverRasterizer::draw({destination.data(), 10, 10, 10}, {source.data(), 2, 2, 2}, side, style);

    require(count > 25 && count < 50, "side trapezoid coverage is implausible");
    require(destination[2 * 10 + 2] == kRed, "side trapezoid lost its upper-left texel");
    require(destination[4 * 10 + 6] == kGreen, "side trapezoid lost its upper-right texel");
    require(destination[7 * 10 + 2] == kBlue, "side trapezoid lost its lower-left texel");
    require(destination[6 * 10 + 6] == kWhite, "side trapezoid lost its lower-right texel");
    require(destination[1 * 10 + 7] == kBlack && destination[8 * 10 + 7] == kBlack,
            "side trapezoid wrote outside its edges");
}

void testBrightnessAndOpacity()
{
    const std::array<std::uint16_t, 1> source = {kWhite};
    std::array<std::uint16_t, 16> destination{};
    DrawStyle style;
    style.brightness = 128;
    style.opacity = 128;
    style.top_opacity = 255;
    style.bottom_opacity = 64;
    style.filter = TextureFilter::Nearest;
    CoverRasterizer::draw({destination.data(), 4, 4, 4}, {source.data(), 1, 1, 1}, {{0, 0}, {4, 0}, {4, 4}, {0, 4}},
                          style);

    const int top = red(destination[0]);
    const int bottom = red(destination[3 * 4]);
    require(top >= 50 && top <= 75, "brightness and opacity were not combined");
    require(bottom > 0 && bottom < top / 2, "vertical opacity fade was not applied");
    const int channel_min = std::min({red(destination[0]), green(destination[0]), blue(destination[0])});
    const int channel_max = std::max({red(destination[0]), green(destination[0]), blue(destination[0])});
    require(channel_max - channel_min <= 2, "RGB565 neutral blending gained a color cast");
}

void testReflectionFlipAndFade()
{
    const std::array<std::uint16_t, 2> source = {kRed, kBlue};
    std::array<std::uint16_t, 60> destination{};
    DrawStyle cover_style;
    cover_style.filter = TextureFilter::Nearest;
    ReflectionStyle reflection;
    reflection.enabled = true;
    reflection.length = 1.0f;
    reflection.source_length = 1.0f;
    reflection.brightness = 255;
    reflection.opacity = 255;
    reflection.top_opacity = 255;
    reflection.bottom_opacity = 64;

    const std::size_t count =
        CoverRasterizer::drawReflection({destination.data(), 6, 10, 6}, {source.data(), 1, 2, 1},
                                        {{1, 0}, {5, 0}, {5, 4}, {1, 4}}, cover_style, reflection);
    require(count == 16, "reflection pixel count mismatch");
    const std::uint16_t reflection_top = destination[4 * 6 + 2];
    const std::uint16_t reflection_bottom = destination[7 * 6 + 2];
    require(blue(reflection_top) > red(reflection_top), "reflection was not vertically flipped at its top");
    require(red(reflection_bottom) > blue(reflection_bottom), "reflection was not vertically flipped at its bottom");
    require(blue(reflection_top) > red(reflection_bottom), "reflection did not fade toward its bottom");
}

void testPerspectiveReflectionGeometry()
{
    const ProjectedQuad cover{{2, 1}, {6, 3}, {7, 7}, {1, 9}};
    const ProjectedQuad reflection = CoverRasterizer::reflectedQuad(cover, 2.0f, 0.5f);

    require(reflection.top_left.x == 1.0f && reflection.top_left.y == 11.0f,
            "reflection did not remain attached to its left bottom corner");
    require(reflection.top_right.x == 7.0f && reflection.top_right.y == 9.0f,
            "reflection did not remain attached to its right bottom corner");
    require(reflection.bottom_left.x == 0.5f && reflection.bottom_left.y == 15.0f,
            "left reflection edge did not continue along the projected cover edge");
    require(reflection.bottom_right.x == 7.5f && reflection.bottom_right.y == 11.0f,
            "right reflection edge did not continue along the projected cover edge");
    require(reflection.top_right.y - reflection.top_left.y == cover.bottom_right.y - cover.bottom_left.y,
            "reflection seam changed the cover bottom-edge slope");
}

void testReflectionSourceCrop()
{
    const std::array<std::uint16_t, 4> source = {kRed, kGreen, kBlue, kWhite};
    std::array<std::uint16_t, 32> destination{};
    DrawStyle cover_style;
    cover_style.filter = TextureFilter::Nearest;
    ReflectionStyle reflection;
    reflection.enabled = true;
    reflection.length = 1.0f;
    reflection.source_length = 0.5f;
    reflection.brightness = 255;
    reflection.opacity = 255;
    reflection.top_opacity = 255;
    reflection.bottom_opacity = 255;

    CoverRasterizer::drawReflection({destination.data(), 4, 8, 4}, {source.data(), 1, 4, 1},
                                    {{0, 0}, {4, 0}, {4, 4}, {0, 4}}, cover_style, reflection);
    require(destination[4 * 4] == kWhite, "reflection seam did not sample the source bottom edge");
    require(destination[7 * 4] == kBlue, "reflection did not stop at the configured source crop");
}

void testBackToFrontLayers()
{
    const std::array<std::uint16_t, 1> red_source = {kRed};
    const std::array<std::uint16_t, 1> green_source = {kGreen};
    std::array<std::uint16_t, 16> destination{};
    const ProjectedQuad quad{{0, 0}, {4, 0}, {4, 4}, {0, 4}};
    std::array<CoverLayer, 2> layers{};
    layers[0].image = {green_source.data(), 1, 1, 1};
    layers[0].quad = quad;
    layers[0].depth = 2.0f;
    layers[0].style.filter = TextureFilter::Nearest;
    layers[1].image = {red_source.data(), 1, 1, 1};
    layers[1].quad = quad;
    layers[1].depth = -2.0f;
    layers[1].style.filter = TextureFilter::Nearest;

    const std::size_t count = CoverRasterizer::drawLayers({destination.data(), 4, 4, 4}, layers.data(), layers.size());
    require(count == 32, "layer coverage count mismatch");
    for (const auto pixel : destination) {
        require(pixel == kGreen, "layers were not rendered back-to-front");
    }
}

void testPartialSurfacesMatchFullSurface()
{
    const std::array<std::uint16_t, 4> source = {kRed, kGreen, kBlue, kWhite};
    std::array<std::uint16_t, 80> full{};
    std::array<std::uint16_t, 80> partial{};
    DrawStyle style;
    style.filter = TextureFilter::Bilinear;
    style.opacity = 220;
    const ProjectedQuad quad{{-2, 1}, {9, 2}, {8, 7}, {0, 8}};

    CoverRasterizer::draw({full.data(), 10, 8, 10}, {source.data(), 2, 2, 2}, quad, style);

    CoverRasterizer::draw({partial.data(), 4, 8, 10, 0, 0}, {source.data(), 2, 2, 2}, quad, style);
    CoverRasterizer::draw({partial.data() + 4, 6, 3, 10, 4, 0}, {source.data(), 2, 2, 2}, quad, style);
    CoverRasterizer::draw({partial.data() + 34, 6, 5, 10, 4, 3}, {source.data(), 2, 2, 2}, quad, style);

    require(partial == full, "partial surfaces did not reproduce the full-surface rasterization");
}

}  // namespace

int main()
{
    try {
        testCenteredCover();
        testBoundsAndInvalidInputs();
        testSideTrapezoid();
        testBrightnessAndOpacity();
        testReflectionFlipAndFade();
        testPerspectiveReflectionGeometry();
        testReflectionSourceCrop();
        testBackToFrontLayers();
        testPartialSurfacesMatchFullSurface();
        std::cout << "cover rasterizer tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cover rasterizer test failed: " << error.what() << '\n';
        return 1;
    }
}
