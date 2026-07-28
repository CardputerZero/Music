#include "rendering/cover_rasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace music::rendering {
namespace {

constexpr double kGeometryEpsilon = 1.0e-8;
constexpr double kUvEpsilon = 1.0e-5;

struct Rgb888 {
    int red;
    int green;
    int blue;
};

bool isValid(Rgb565Surface surface)
{
    return surface.pixels != nullptr && surface.width > 0 && surface.height > 0 &&
           surface.stride_pixels >= surface.width;
}

bool isValid(Rgb565ImageView image)
{
    return image.pixels != nullptr && image.width > 0 && image.height > 0 && image.stride_pixels >= image.width;
}

double cross(PointF first, PointF second, PointF third)
{
    return static_cast<double>(second.x - first.x) * static_cast<double>(third.y - second.y) -
           static_cast<double>(second.y - first.y) * static_cast<double>(third.x - second.x);
}

bool isFiniteAndConvex(const ProjectedQuad& quad)
{
    const std::array<PointF, 4> points = {quad.top_left, quad.top_right, quad.bottom_right, quad.bottom_left};
    for (const auto point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return false;
        }
    }

    double orientation = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const double value =
            cross(points[index], points[(index + 1) % points.size()], points[(index + 2) % points.size()]);
        if (std::abs(value) <= kGeometryEpsilon) {
            return false;
        }
        if (orientation == 0.0) {
            orientation = value;
        } else if ((value > 0.0) != (orientation > 0.0)) {
            return false;
        }
    }
    return true;
}

bool invertMatrix(const std::array<double, 9>& matrix, std::array<double, 9>& inverse)
{
    const double cofactor_00 = matrix[4] * matrix[8] - matrix[5] * matrix[7];
    const double cofactor_01 = matrix[2] * matrix[7] - matrix[1] * matrix[8];
    const double cofactor_02 = matrix[1] * matrix[5] - matrix[2] * matrix[4];
    const double cofactor_10 = matrix[5] * matrix[6] - matrix[3] * matrix[8];
    const double cofactor_11 = matrix[0] * matrix[8] - matrix[2] * matrix[6];
    const double cofactor_12 = matrix[2] * matrix[3] - matrix[0] * matrix[5];
    const double cofactor_20 = matrix[3] * matrix[7] - matrix[4] * matrix[6];
    const double cofactor_21 = matrix[1] * matrix[6] - matrix[0] * matrix[7];
    const double cofactor_22 = matrix[0] * matrix[4] - matrix[1] * matrix[3];
    const double determinant = matrix[0] * cofactor_00 + matrix[1] * cofactor_10 + matrix[2] * cofactor_20;
    if (!std::isfinite(determinant) || std::abs(determinant) <= kGeometryEpsilon) {
        return false;
    }

    const double reciprocal = 1.0 / determinant;
    inverse = {cofactor_00 * reciprocal, cofactor_01 * reciprocal, cofactor_02 * reciprocal,
               cofactor_10 * reciprocal, cofactor_11 * reciprocal, cofactor_12 * reciprocal,
               cofactor_20 * reciprocal, cofactor_21 * reciprocal, cofactor_22 * reciprocal};
    return true;
}

bool inverseProjection(const ProjectedQuad& quad, std::array<double, 9>& inverse)
{
    const double x0 = quad.top_left.x;
    const double y0 = quad.top_left.y;
    const double x1 = quad.top_right.x;
    const double y1 = quad.top_right.y;
    const double x2 = quad.bottom_right.x;
    const double y2 = quad.bottom_right.y;
    const double x3 = quad.bottom_left.x;
    const double y3 = quad.bottom_left.y;
    const double dx1 = x1 - x2;
    const double dx2 = x3 - x2;
    const double dx3 = x0 - x1 + x2 - x3;
    const double dy1 = y1 - y2;
    const double dy2 = y3 - y2;
    const double dy3 = y0 - y1 + y2 - y3;

    std::array<double, 9> projection{};
    if (std::abs(dx3) <= kGeometryEpsilon && std::abs(dy3) <= kGeometryEpsilon) {
        projection = {x1 - x0, x3 - x0, x0, y1 - y0, y3 - y0, y0, 0.0, 0.0, 1.0};
    } else {
        const double denominator = dx1 * dy2 - dx2 * dy1;
        if (std::abs(denominator) <= kGeometryEpsilon) {
            return false;
        }
        const double projective_x = (dx3 * dy2 - dx2 * dy3) / denominator;
        const double projective_y = (dx1 * dy3 - dx3 * dy1) / denominator;
        projection = {x1 - x0 + projective_x * x1,
                      x3 - x0 + projective_y * x3,
                      x0,
                      y1 - y0 + projective_x * y1,
                      y3 - y0 + projective_y * y3,
                      y0,
                      projective_x,
                      projective_y,
                      1.0};
    }
    return invertMatrix(projection, inverse);
}

Rgb888 unpack(std::uint16_t color)
{
    const int red5 = (color >> 11U) & 0x1fU;
    const int green6 = (color >> 5U) & 0x3fU;
    const int blue5 = color & 0x1fU;
    return {(red5 << 3) | (red5 >> 2), (green6 << 2) | (green6 >> 4), (blue5 << 3) | (blue5 >> 2)};
}

std::uint16_t pack(Rgb888 color)
{
    const int red5 = (std::clamp(color.red, 0, 255) * 31 + 127) / 255;
    const int green6 = (std::clamp(color.green, 0, 255) * 63 + 127) / 255;
    const int blue5 = (std::clamp(color.blue, 0, 255) * 31 + 127) / 255;
    return static_cast<std::uint16_t>((red5 << 11U) | (green6 << 5U) | blue5);
}

Rgb888 sampleNearest(Rgb565ImageView source, double u, double v)
{
    const int x = std::clamp(static_cast<int>(u * source.width), 0, source.width - 1);
    const int y = std::clamp(static_cast<int>(v * source.height), 0, source.height - 1);
    return unpack(source.pixels[static_cast<std::size_t>(y) * source.stride_pixels + x]);
}

Rgb888 sampleBilinear(Rgb565ImageView source, double u, double v)
{
    const double source_x = u * static_cast<double>(source.width - 1);
    const double source_y = v * static_cast<double>(source.height - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, source.width - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, source.height - 1);
    const int x1 = std::min(x0 + 1, source.width - 1);
    const int y1 = std::min(y0 + 1, source.height - 1);
    const double x_fraction = source_x - x0;
    const double y_fraction = source_y - y0;
    const Rgb888 top_left = unpack(source.pixels[static_cast<std::size_t>(y0) * source.stride_pixels + x0]);
    const Rgb888 top_right = unpack(source.pixels[static_cast<std::size_t>(y0) * source.stride_pixels + x1]);
    const Rgb888 bottom_left = unpack(source.pixels[static_cast<std::size_t>(y1) * source.stride_pixels + x0]);
    const Rgb888 bottom_right = unpack(source.pixels[static_cast<std::size_t>(y1) * source.stride_pixels + x1]);

    const auto interpolate = [x_fraction, y_fraction](int value_00, int value_10, int value_01, int value_11) {
        const double top = value_00 + (value_10 - value_00) * x_fraction;
        const double bottom = value_01 + (value_11 - value_01) * x_fraction;
        return static_cast<int>(std::lround(top + (bottom - top) * y_fraction));
    };
    return {interpolate(top_left.red, top_right.red, bottom_left.red, bottom_right.red),
            interpolate(top_left.green, top_right.green, bottom_left.green, bottom_right.green),
            interpolate(top_left.blue, top_right.blue, bottom_left.blue, bottom_right.blue)};
}

std::uint8_t multiply(std::uint8_t first, std::uint8_t second)
{
    return static_cast<std::uint8_t>((static_cast<unsigned int>(first) * second + 127U) / 255U);
}

void blendPixel(std::uint16_t& destination, Rgb888 source, std::uint8_t brightness, std::uint8_t alpha)
{
    source.red = (source.red * brightness + 127) / 255;
    source.green = (source.green * brightness + 127) / 255;
    source.blue = (source.blue * brightness + 127) / 255;
    if (alpha == 255) {
        destination = pack(source);
        return;
    }

    const Rgb888 background = unpack(destination);
    const int inverse_alpha = 255 - alpha;
    destination = pack({(source.red * alpha + background.red * inverse_alpha + 127) / 255,
                        (source.green * alpha + background.green * inverse_alpha + 127) / 255,
                        (source.blue * alpha + background.blue * inverse_alpha + 127) / 255});
}

std::uint8_t opacityAt(const DrawStyle& style, double vertical_position)
{
    const double gradient = static_cast<double>(style.top_opacity) +
                            (static_cast<double>(style.bottom_opacity) - style.top_opacity) * vertical_position;
    return multiply(style.opacity, static_cast<std::uint8_t>(std::clamp(std::lround(gradient), 0L, 255L)));
}

}  // namespace

std::size_t CoverRasterizer::draw(Rgb565Surface destination, Rgb565ImageView source, const ProjectedQuad& quad,
                                  const DrawStyle& style)
{
    if (!isValid(destination) || !isValid(source) || style.opacity == 0 || !isFiniteAndConvex(quad)) {
        return 0;
    }

    std::array<double, 9> inverse{};
    if (!inverseProjection(quad, inverse)) {
        return 0;
    }

    const std::array<PointF, 4> points = {quad.top_left, quad.top_right, quad.bottom_right, quad.bottom_left};
    const auto [minimum_x, maximum_x] = std::minmax_element(
        points.begin(), points.end(), [](PointF first, PointF second) { return first.x < second.x; });
    const auto [minimum_y, maximum_y] = std::minmax_element(
        points.begin(), points.end(), [](PointF first, PointF second) { return first.y < second.y; });
    const double surface_left = destination.origin_x;
    const double surface_right = static_cast<double>(destination.origin_x) + destination.width;
    const double surface_top = destination.origin_y;
    const double surface_bottom = static_cast<double>(destination.origin_y) + destination.height;
    const double clipped_left = std::clamp(static_cast<double>(minimum_x->x), surface_left, surface_right);
    const double clipped_right = std::clamp(static_cast<double>(maximum_x->x), surface_left, surface_right);
    const double clipped_top = std::clamp(static_cast<double>(minimum_y->y), surface_top, surface_bottom);
    const double clipped_bottom = std::clamp(static_cast<double>(maximum_y->y), surface_top, surface_bottom);
    const int left = static_cast<int>(std::floor(clipped_left));
    const int right = static_cast<int>(std::ceil(clipped_right));
    const int top = static_cast<int>(std::floor(clipped_top));
    const int bottom = static_cast<int>(std::ceil(clipped_bottom));
    if (left >= right || top >= bottom) {
        return 0;
    }

    std::size_t pixels_blended = 0;
    for (int y = top; y < bottom; ++y) {
        const double sample_y = y + 0.5;
        const double first_x = left + 0.5;
        double u_numerator = inverse[0] * first_x + inverse[1] * sample_y + inverse[2];
        double v_numerator = inverse[3] * first_x + inverse[4] * sample_y + inverse[5];
        double denominator = inverse[6] * first_x + inverse[7] * sample_y + inverse[8];
        for (int x = left; x < right; ++x) {
            if (std::abs(denominator) > kGeometryEpsilon) {
                double u = u_numerator / denominator;
                double v = v_numerator / denominator;
                if (u >= -kUvEpsilon && u <= 1.0 + kUvEpsilon && v >= -kUvEpsilon && v <= 1.0 + kUvEpsilon) {
                    u = std::clamp(u, 0.0, 1.0);
                    v = std::clamp(v, 0.0, 1.0);
                    const std::uint8_t alpha = opacityAt(style, v);
                    if (alpha != 0) {
                        const double texture_u = style.flip_horizontal ? 1.0 - u : u;
                        const double texture_v = style.flip_vertical ? 1.0 - v : v;
                        const Rgb888 sampled = style.filter == TextureFilter::Nearest
                                                   ? sampleNearest(source, texture_u, texture_v)
                                                   : sampleBilinear(source, texture_u, texture_v);
                        const std::size_t destination_index =
                            static_cast<std::size_t>(y - destination.origin_y) * destination.stride_pixels +
                            static_cast<std::size_t>(x - destination.origin_x);
                        blendPixel(destination.pixels[destination_index], sampled, style.brightness, alpha);
                        ++pixels_blended;
                    }
                }
            }
            u_numerator += inverse[0];
            v_numerator += inverse[3];
            denominator += inverse[6];
        }
    }
    return pixels_blended;
}

ProjectedQuad CoverRasterizer::reflectedQuad(const ProjectedQuad& quad, float gap, float length)
{
    const PointF reflected_top_left{quad.bottom_left.x, quad.bottom_left.y + gap};
    const PointF reflected_top_right{quad.bottom_right.x, quad.bottom_right.y + gap};
    const PointF reflected_bottom_right{quad.bottom_right.x + (quad.bottom_right.x - quad.top_right.x) * length,
                                        quad.bottom_right.y + gap + (quad.bottom_right.y - quad.top_right.y) * length};
    const PointF reflected_bottom_left{quad.bottom_left.x + (quad.bottom_left.x - quad.top_left.x) * length,
                                       quad.bottom_left.y + gap + (quad.bottom_left.y - quad.top_left.y) * length};
    return {reflected_top_left, reflected_top_right, reflected_bottom_right, reflected_bottom_left};
}

std::size_t CoverRasterizer::drawReflection(Rgb565Surface destination, Rgb565ImageView source,
                                            const ProjectedQuad& quad, const DrawStyle& cover_style,
                                            const ReflectionStyle& reflection)
{
    if (!reflection.enabled || !std::isfinite(reflection.gap) || !std::isfinite(reflection.length) ||
        !std::isfinite(reflection.source_length) || reflection.length <= 0.0f || reflection.source_length <= 0.0f ||
        reflection.source_length > 1.0f || reflection.opacity == 0 || !isValid(source)) {
        return 0;
    }

    const int reflected_rows =
        std::clamp(static_cast<int>(std::lround(source.height * reflection.source_length)), 1, source.height);
    source.pixels += static_cast<std::size_t>(source.height - reflected_rows) * source.stride_pixels;
    source.height = reflected_rows;

    DrawStyle style = cover_style;
    style.brightness = multiply(style.brightness, reflection.brightness);
    style.opacity = multiply(style.opacity, reflection.opacity);
    style.top_opacity = reflection.top_opacity;
    style.bottom_opacity = reflection.bottom_opacity;
    style.flip_vertical = !style.flip_vertical;
    return draw(destination, source, reflectedQuad(quad, reflection.gap, reflection.length), style);
}

std::size_t CoverRasterizer::drawLayers(Rgb565Surface destination, const CoverLayer* layers, std::size_t count)
{
    if (!isValid(destination) || layers == nullptr || count == 0) {
        return 0;
    }

    std::size_t pixels_blended = 0;
    std::size_t previous = count;
    for (std::size_t rendered = 0; rendered < count; ++rendered) {
        std::size_t next = count;
        for (std::size_t index = 0; index < count; ++index) {
            const float depth = std::isfinite(layers[index].depth) ? layers[index].depth : 0.0f;
            if (previous != count) {
                const float previous_depth = std::isfinite(layers[previous].depth) ? layers[previous].depth : 0.0f;
                if (depth < previous_depth || (depth == previous_depth && index <= previous)) {
                    continue;
                }
            }
            if (next == count) {
                next = index;
                continue;
            }
            const float next_depth = std::isfinite(layers[next].depth) ? layers[next].depth : 0.0f;
            if (depth < next_depth || (depth == next_depth && index < next)) {
                next = index;
            }
        }
        if (next == count) {
            break;
        }
        const CoverLayer& layer = layers[next];
        pixels_blended += drawReflection(destination, layer.image, layer.quad, layer.style, layer.reflection);
        pixels_blended += draw(destination, layer.image, layer.quad, layer.style);
        previous = next;
    }
    return pixels_blended;
}

}  // namespace music::rendering
