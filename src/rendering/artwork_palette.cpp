#include "rendering/artwork_palette.hpp"

#include "assets/runtime_assets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace music::rendering {
namespace {

constexpr std::size_t kClusterCount = 5;
constexpr int kIterations = 8;
constexpr int kPaletteImageSize = 64;

struct ColorF {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
};

struct Hsl {
    float hue = 0.0f;
    float saturation = 0.0f;
    float lightness = 0.0f;
};

struct Sample {
    ColorF color;
    float weight = 1.0f;
};

struct Cluster {
    ColorF color;
    float weight = 0.0f;
};

ColorF unpackRgb565(std::uint16_t value)
{
    return {
        static_cast<float>((value >> 11U) & 0x1fU) / 31.0f,
        static_cast<float>((value >> 5U) & 0x3fU) / 63.0f,
        static_cast<float>(value & 0x1fU) / 31.0f,
    };
}

float luminance(ColorF color) { return 0.2126f * color.red + 0.7152f * color.green + 0.0722f * color.blue; }

float distanceSquared(ColorF left, ColorF right)
{
    const float red = left.red - right.red;
    const float green = left.green - right.green;
    const float blue = left.blue - right.blue;
    return red * red + green * green + blue * blue;
}

Hsl rgbToHsl(ColorF color)
{
    const float maximum = std::max({color.red, color.green, color.blue});
    const float minimum = std::min({color.red, color.green, color.blue});
    const float chroma = maximum - minimum;

    Hsl result;
    result.lightness = (maximum + minimum) * 0.5f;
    if (chroma <= 0.0001f) {
        return result;
    }

    result.saturation = chroma / (1.0f - std::abs(2.0f * result.lightness - 1.0f));
    if (maximum == color.red) {
        result.hue = std::fmod((color.green - color.blue) / chroma, 6.0f);
    } else if (maximum == color.green) {
        result.hue = (color.blue - color.red) / chroma + 2.0f;
    } else {
        result.hue = (color.red - color.green) / chroma + 4.0f;
    }
    result.hue /= 6.0f;
    if (result.hue < 0.0f) {
        result.hue += 1.0f;
    }
    return result;
}

float hueComponent(float p, float q, float t)
{
    if (t < 0.0f) {
        t += 1.0f;
    }
    if (t > 1.0f) {
        t -= 1.0f;
    }
    if (t < 1.0f / 6.0f) {
        return p + (q - p) * 6.0f * t;
    }
    if (t < 0.5f) {
        return q;
    }
    if (t < 2.0f / 3.0f) {
        return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    }
    return p;
}

ColorF hslToRgb(Hsl color)
{
    if (color.saturation <= 0.0001f) {
        return {color.lightness, color.lightness, color.lightness};
    }
    const float q = color.lightness < 0.5f ? color.lightness * (1.0f + color.saturation)
                                           : color.lightness + color.saturation - color.lightness * color.saturation;
    const float p = 2.0f * color.lightness - q;
    return {
        hueComponent(p, q, color.hue + 1.0f / 3.0f),
        hueComponent(p, q, color.hue),
        hueComponent(p, q, color.hue - 1.0f / 3.0f),
    };
}

ColorF mix(ColorF first, ColorF second, float amount)
{
    return {
        first.red + (second.red - first.red) * amount,
        first.green + (second.green - first.green) * amount,
        first.blue + (second.blue - first.blue) * amount,
    };
}

ui::Color toColor(ColorF color)
{
    const auto channel = [](float value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return {channel(color.red), channel(color.green), channel(color.blue)};
}

std::vector<Sample> samplesFrom(const CoverImage& artwork)
{
    std::vector<Sample> samples;
    if (!artwork.valid()) {
        return samples;
    }

    const int stride = std::max(1, std::max(artwork.width, artwork.height) / kPaletteImageSize);
    samples.reserve(static_cast<std::size_t>((artwork.width / stride + 1) * (artwork.height / stride + 1)));
    for (int y = stride / 2; y < artwork.height; y += stride) {
        for (int x = stride / 2; x < artwork.width; x += stride) {
            const ColorF color = unpackRgb565(artwork.pixels[static_cast<std::size_t>(y * artwork.width + x)]);
            const float brightness = luminance(color);
            if (brightness < 0.05f || brightness > 0.95f) {
                continue;
            }
            const Hsl hsl = rgbToHsl(color);
            const float gray_weight = hsl.saturation < 0.1f ? 0.3f : 1.0f;
            samples.push_back({color, gray_weight});
        }
    }
    return samples;
}

std::array<Cluster, kClusterCount> clusterSamples(const std::vector<Sample>& samples)
{
    std::array<Cluster, kClusterCount> clusters{};
    if (samples.empty()) {
        return clusters;
    }

    const auto first = std::max_element(samples.begin(), samples.end(), [](const Sample& left, const Sample& right) {
        const Hsl left_hsl = rgbToHsl(left.color);
        const Hsl right_hsl = rgbToHsl(right.color);
        const float left_score = left_hsl.saturation * (1.0f - std::abs(left_hsl.lightness - 0.5f));
        const float right_score = right_hsl.saturation * (1.0f - std::abs(right_hsl.lightness - 0.5f));
        return left_score < right_score;
    });
    clusters[0].color = first->color;

    for (std::size_t index = 1; index < clusters.size(); ++index) {
        float best_score = -1.0f;
        for (const Sample& sample : samples) {
            float nearest = std::numeric_limits<float>::max();
            for (std::size_t existing = 0; existing < index; ++existing) {
                nearest = std::min(nearest, distanceSquared(sample.color, clusters[existing].color));
            }
            const float score = nearest * sample.weight;
            if (score > best_score) {
                best_score = score;
                clusters[index].color = sample.color;
            }
        }
    }

    for (int iteration = 0; iteration < kIterations; ++iteration) {
        std::array<ColorF, kClusterCount> sums{};
        std::array<float, kClusterCount> weights{};
        for (const Sample& sample : samples) {
            std::size_t nearest = 0;
            float nearest_distance = distanceSquared(sample.color, clusters[0].color);
            for (std::size_t index = 1; index < clusters.size(); ++index) {
                const float distance = distanceSquared(sample.color, clusters[index].color);
                if (distance < nearest_distance) {
                    nearest = index;
                    nearest_distance = distance;
                }
            }
            sums[nearest].red += sample.color.red * sample.weight;
            sums[nearest].green += sample.color.green * sample.weight;
            sums[nearest].blue += sample.color.blue * sample.weight;
            weights[nearest] += sample.weight;
        }
        for (std::size_t index = 0; index < clusters.size(); ++index) {
            clusters[index].weight = weights[index];
            if (weights[index] > 0.0f) {
                clusters[index].color = {
                    sums[index].red / weights[index],
                    sums[index].green / weights[index],
                    sums[index].blue / weights[index],
                };
            }
        }
    }
    return clusters;
}

std::size_t dominantCluster(const std::array<Cluster, kClusterCount>& clusters)
{
    std::size_t best = 0;
    float best_score = -1.0f;
    for (std::size_t index = 0; index < clusters.size(); ++index) {
        const Hsl hsl = rgbToHsl(clusters[index].color);
        const float brightness_weight = 1.0f - std::clamp(std::abs(hsl.lightness - 0.48f) * 1.25f, 0.0f, 0.75f);
        const float score = clusters[index].weight * (0.72f + hsl.saturation) * brightness_weight;
        if (score > best_score) {
            best = index;
            best_score = score;
        }
    }
    return best;
}

ui::PageTheme themeFrom(const std::array<Cluster, kClusterCount>& clusters)
{
    const std::size_t dominant = dominantCluster(clusters);
    const Hsl main_hsl = rgbToHsl(clusters[dominant].color);

    Hsl background_hsl = main_hsl;
    background_hsl.saturation = main_hsl.saturation < 0.12f ? main_hsl.saturation * 0.72f
                                                            : std::clamp(main_hsl.saturation * 0.72f, 0.34f, 0.62f);
    background_hsl.lightness = std::clamp(main_hsl.lightness * 0.62f, 0.23f, 0.36f);
    const ColorF background = hslToRgb(background_hsl);

    Hsl accent_hsl = main_hsl;
    accent_hsl.saturation =
        main_hsl.saturation < 0.12f ? main_hsl.saturation : std::clamp(main_hsl.saturation * 1.08f, 0.52f, 0.82f);
    accent_hsl.lightness = std::clamp(main_hsl.lightness, 0.48f, 0.66f);
    const ColorF accent = hslToRgb(accent_hsl);

    const bool use_dark_text = luminance(background) > 0.58f;
    const ColorF primary = use_dark_text ? ColorF{0.06f, 0.06f, 0.07f} : ColorF{0.97f, 0.97f, 0.98f};
    const ColorF secondary = mix(primary, background, 0.26f);
    return {toColor(background), toColor(primary), toColor(secondary), toColor(accent)};
}

}  // namespace

ui::PageTheme extractArtworkTheme(const CoverImage& artwork)
{
    const std::vector<Sample> samples = samplesFrom(artwork);
    if (samples.empty()) {
        return ui::defaultPageTheme();
    }
    return themeFrom(clusterSamples(samples));
}

ui::PageTheme ArtworkPaletteCache::themeFor(const std::filesystem::path& artwork_path)
{
    const std::filesystem::path display_path = displayCoverPath(artwork_path);
    std::error_code error;
    if (display_path.empty() || !std::filesystem::is_regular_file(display_path, error) || error) {
        return ui::defaultPageTheme();
    }
    const std::uintmax_t size = std::filesystem::file_size(display_path, error);
    if (error) {
        return ui::defaultPageTheme();
    }
    const auto modified_at = std::filesystem::last_write_time(display_path, error);
    if (error) {
        return ui::defaultPageTheme();
    }

    const std::string key = display_path.string();
    const auto cached = _entries.find(key);
    if (cached != _entries.end() && cached->second.size == size && cached->second.modified_at == modified_at) {
        return cached->second.theme;
    }

    const auto artwork = loadCoverImageWithFallback(artwork_path, kPaletteImageSize);
    const ui::PageTheme theme = artwork ? extractArtworkTheme(*artwork) : ui::defaultPageTheme();
    _entries[key] = {size, modified_at, theme};
    return theme;
}

void ArtworkPaletteCache::clear() { _entries.clear(); }

}  // namespace music::rendering
