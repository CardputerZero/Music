#include "rendering/cover_image_loader.hpp"

#include <lvgl.h>
#include "src/draw/lv_image_decoder_private.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

#include <jpeglib.h>

namespace music::rendering {
namespace {

constexpr std::uint64_t kMaxBufferedSourcePixels = 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxBaselineJpegSourcePixels = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaxProgressiveJpegSourcePixels = 12ULL * 1024ULL * 1024ULL;

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

std::uint16_t packRgb565(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha = 255)
{
    if (alpha != 255) {
        red = static_cast<std::uint8_t>((static_cast<unsigned int>(red) * alpha + 127U) / 255U);
        green = static_cast<std::uint8_t>((static_cast<unsigned int>(green) * alpha + 127U) / 255U);
        blue = static_cast<std::uint8_t>((static_cast<unsigned int>(blue) * alpha + 127U) / 255U);
    }
    return static_cast<std::uint16_t>(((red & 0xf8U) << 8U) | ((green & 0xfcU) << 3U) | (blue >> 3U));
}

std::uint16_t scaleRgb565(std::uint16_t color, std::uint8_t alpha)
{
    if (alpha == 255) {
        return color;
    }
    const std::uint8_t red = static_cast<std::uint8_t>(((color >> 11U) & 0x1fU) * 255U / 31U);
    const std::uint8_t green = static_cast<std::uint8_t>(((color >> 5U) & 0x3fU) * 255U / 63U);
    const std::uint8_t blue = static_cast<std::uint8_t>((color & 0x1fU) * 255U / 31U);
    return packRgb565(red, green, blue, alpha);
}

std::optional<std::uint16_t> pixelAt(const lv_draw_buf_t& buffer, int x, int y)
{
    if (x < 0 || y < 0 || x >= static_cast<int>(buffer.header.w) || y >= static_cast<int>(buffer.header.h) ||
        buffer.data == nullptr) {
        return std::nullopt;
    }

    const auto* row = buffer.data + static_cast<std::size_t>(y) * buffer.header.stride;
    switch (static_cast<lv_color_format_t>(buffer.header.cf)) {
        case LV_COLOR_FORMAT_RGB565: {
            std::uint16_t color = 0;
            std::memcpy(&color, row + static_cast<std::size_t>(x) * sizeof(color), sizeof(color));
            return color;
        }
        case LV_COLOR_FORMAT_RGB565_SWAPPED: {
            std::uint16_t color = 0;
            std::memcpy(&color, row + static_cast<std::size_t>(x) * sizeof(color), sizeof(color));
            return static_cast<std::uint16_t>((color << 8U) | (color >> 8U));
        }
        case LV_COLOR_FORMAT_RGB565A8: {
            std::uint16_t color = 0;
            std::memcpy(&color, row + static_cast<std::size_t>(x) * sizeof(color), sizeof(color));
            const std::size_t alpha_offset = static_cast<std::size_t>(buffer.header.stride) * buffer.header.h +
                                             static_cast<std::size_t>(y) * buffer.header.w + x;
            return scaleRgb565(color, buffer.data[alpha_offset]);
        }
        case LV_COLOR_FORMAT_RGB888: {
            const auto* pixel = row + static_cast<std::size_t>(x) * 3U;
            return packRgb565(pixel[2], pixel[1], pixel[0]);
        }
        case LV_COLOR_FORMAT_XRGB8888:
        case LV_COLOR_FORMAT_ARGB8888:
        case LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED: {
            const auto* pixel = row + static_cast<std::size_t>(x) * 4U;
            const std::uint8_t alpha =
                static_cast<lv_color_format_t>(buffer.header.cf) == LV_COLOR_FORMAT_XRGB8888 ? 255 : pixel[3];
            if (static_cast<lv_color_format_t>(buffer.header.cf) == LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED) {
                return packRgb565(pixel[2], pixel[1], pixel[0]);
            }
            return packRgb565(pixel[2], pixel[1], pixel[0], alpha);
        }
        default:
            return std::nullopt;
    }
}

struct SampleMap {
    std::vector<int> x;
    std::vector<int> y;
};

SampleMap makeSampleMap(int source_width, int source_height, int output_size)
{
    const int side = std::min(source_width, source_height);
    const int crop_x = (source_width - side) / 2;
    const int crop_y = (source_height - side) / 2;

    SampleMap map;
    map.x.resize(static_cast<std::size_t>(output_size));
    map.y.resize(static_cast<std::size_t>(output_size));
    for (int index = 0; index < output_size; ++index) {
        const int source_offset = std::min(side - 1, (2 * index + 1) * side / (2 * output_size));
        map.x[static_cast<std::size_t>(index)] = crop_x + source_offset;
        map.y[static_cast<std::size_t>(index)] = crop_y + source_offset;
    }
    return map;
}

struct JpegErrorManager {
    jpeg_error_mgr manager{};
    std::jmp_buf jump{};
    std::array<char, JMSG_LENGTH_MAX> message{};
};

struct JpegDecodeContext {
    jpeg_decompress_struct decoder{};
    JpegErrorManager error;
    std::FILE* file = nullptr;
    bool decoder_created = false;
    CoverImage output;
    SampleMap map;
    std::vector<JSAMPLE> row;
};

void jpegErrorExit(j_common_ptr common)
{
    auto* error = reinterpret_cast<JpegErrorManager*>(common->err);
    common->err->format_message(common, error->message.data());
    std::longjmp(error->jump, 1);
}

void closeJpeg(JpegDecodeContext& context)
{
    if (context.decoder_created) {
        jpeg_destroy_decompress(&context.decoder);
        context.decoder_created = false;
    }
    if (context.file) {
        std::fclose(context.file);
        context.file = nullptr;
    }
}

std::optional<CoverImage> loadJpegImage(const std::filesystem::path& path, int output_size)
{
    auto context = std::make_unique<JpegDecodeContext>();
    context->file = std::fopen(path.string().c_str(), "rb");
    if (!context->file) {
        spdlog::warn("Music cover: cannot open JPEG '{}'", path.string());
        return std::nullopt;
    }

    context->decoder.err = jpeg_std_error(&context->error.manager);
    context->error.manager.error_exit = jpegErrorExit;
    if (setjmp(context->error.jump) != 0) {
        const std::string message = context->error.message.data();
        closeJpeg(*context);
        spdlog::warn("Music cover: cannot decode JPEG '{}': {}", path.string(), message);
        return std::nullopt;
    }

    // jpeg_CreateDecompress initializes decoder.mem to null before any
    // allocation, so destroy is also safe when its error handler jumps out.
    context->decoder_created = true;
    jpeg_create_decompress(&context->decoder);
    jpeg_stdio_src(&context->decoder, context->file);
    jpeg_read_header(&context->decoder, TRUE);

    const std::uint64_t header_width = context->decoder.image_width;
    const std::uint64_t header_height = context->decoder.image_height;
    const bool progressive = context->decoder.progressive_mode != FALSE;
    const std::uint64_t maximum_source_pixels =
        progressive ? kMaxProgressiveJpegSourcePixels : kMaxBaselineJpegSourcePixels;
    if (header_width == 0 || header_height == 0 || header_height > maximum_source_pixels / header_width) {
        closeJpeg(*context);
        spdlog::warn("Music cover: {} JPEG '{}' is too large for low-memory decode ({}x{}, limit={} pixels)",
                     progressive ? "progressive" : "baseline", path.string(), header_width, header_height,
                     maximum_source_pixels);
        return std::nullopt;
    }

    unsigned int scale_denominator = 1;
    const unsigned int source_side = std::min(context->decoder.image_width, context->decoder.image_height);
    while (scale_denominator < 8 && source_side / (scale_denominator * 2U) >= static_cast<unsigned int>(output_size)) {
        scale_denominator *= 2;
    }
    context->decoder.scale_num = 1;
    context->decoder.scale_denom = scale_denominator;
    context->decoder.out_color_space = JCS_RGB;
    context->decoder.dct_method = JDCT_IFAST;
    jpeg_start_decompress(&context->decoder);

    const int source_width = static_cast<int>(context->decoder.output_width);
    const int source_height = static_cast<int>(context->decoder.output_height);
    if (source_width <= 0 || source_height <= 0 || context->decoder.output_components != 3) {
        closeJpeg(*context);
        spdlog::warn("Music cover: unsupported JPEG output '{}' ({}x{}, components={})", path.string(), source_width,
                     source_height, context->decoder.output_components);
        return std::nullopt;
    }

    context->output.width = output_size;
    context->output.height = output_size;
    context->output.pixels.assign(static_cast<std::size_t>(output_size * output_size), 0);
    context->map = makeSampleMap(source_width, source_height, output_size);
    context->row.resize(static_cast<std::size_t>(source_width * context->decoder.output_components));

    std::size_t sampled_rows = 0;
    while (context->decoder.output_scanline < context->decoder.output_height) {
        const int source_y = static_cast<int>(context->decoder.output_scanline);
        JSAMPROW row = context->row.data();
        if (jpeg_read_scanlines(&context->decoder, &row, 1) != 1) {
            closeJpeg(*context);
            spdlog::warn("Music cover: incomplete JPEG scanline read '{}'", path.string());
            return std::nullopt;
        }

        const auto y_begin = std::lower_bound(context->map.y.begin(), context->map.y.end(), source_y);
        const auto y_end = std::upper_bound(y_begin, context->map.y.end(), source_y);
        for (auto y_iterator = y_begin; y_iterator != y_end; ++y_iterator) {
            const std::size_t output_y = static_cast<std::size_t>(std::distance(context->map.y.begin(), y_iterator));
            for (std::size_t output_x = 0; output_x < context->map.x.size(); ++output_x) {
                const std::size_t source_x = static_cast<std::size_t>(context->map.x[output_x]);
                const JSAMPLE* pixel = context->row.data() + source_x * 3U;
                context->output.pixels[output_y * static_cast<std::size_t>(output_size) + output_x] =
                    packRgb565(pixel[0], pixel[1], pixel[2]);
            }
            ++sampled_rows;
        }
    }
    jpeg_finish_decompress(&context->decoder);

    if (sampled_rows != static_cast<std::size_t>(output_size)) {
        closeJpeg(*context);
        spdlog::warn("Music cover: incomplete JPEG sample '{}' ({}/{})", path.string(), sampled_rows, output_size);
        return std::nullopt;
    }

    closeJpeg(*context);
    return std::move(context->output);
}

std::size_t sampleArea(const lv_draw_buf_t& decoded, const lv_area_t& decoded_area, const SampleMap& map,
                       CoverImage& output, std::vector<bool>& sampled)
{
    const auto x_begin = std::lower_bound(map.x.begin(), map.x.end(), decoded_area.x1);
    const auto x_end = std::upper_bound(x_begin, map.x.end(), decoded_area.x2);
    const auto y_begin = std::lower_bound(map.y.begin(), map.y.end(), decoded_area.y1);
    const auto y_end = std::upper_bound(y_begin, map.y.end(), decoded_area.y2);

    std::size_t count = 0;
    for (auto y_iterator = y_begin; y_iterator != y_end; ++y_iterator) {
        const std::size_t output_y = static_cast<std::size_t>(std::distance(map.y.begin(), y_iterator));
        const int local_y = *y_iterator - decoded_area.y1;
        for (auto x_iterator = x_begin; x_iterator != x_end; ++x_iterator) {
            const std::size_t output_x = static_cast<std::size_t>(std::distance(map.x.begin(), x_iterator));
            const int local_x = *x_iterator - decoded_area.x1;
            const auto color = pixelAt(decoded, local_x, local_y);
            if (!color) {
                continue;
            }
            const std::size_t output_index = output_y * static_cast<std::size_t>(output.width) + output_x;
            output.pixels[output_index] = *color;
            if (!sampled[output_index]) {
                sampled[output_index] = true;
                ++count;
            }
        }
    }
    return count;
}

}  // namespace

std::optional<CoverImage> loadCoverImage(const std::filesystem::path& path, int output_size)
{
    if (output_size <= 0 || !std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }

    const std::string extension = lowerAscii(path.extension().string());
    if (extension == ".jpg" || extension == ".jpeg") {
        return loadJpegImage(path, output_size);
    }

    // Keep the native path here. LVGL's POSIX driver accepts paths without a
    // drive prefix through LV_FS_DEFAULT_DRIVER_LETTER, while LodePNG opens
    // dsc->src directly with the host file API.
    const std::string source = path.string();
    lv_image_header_t header{};
    if (lv_image_decoder_get_info(source.c_str(), &header) != LV_RESULT_OK || header.w == 0 || header.h == 0) {
        spdlog::warn("Music cover: unsupported image '{}'", path.string());
        return std::nullopt;
    }

    const std::uint64_t source_pixels = static_cast<std::uint64_t>(header.w) * header.h;
    if (source_pixels > kMaxBufferedSourcePixels) {
        spdlog::warn("Music cover: '{}' is too large for low-memory decode ({}x{})", path.string(),
                     static_cast<unsigned int>(header.w), static_cast<unsigned int>(header.h));
        return std::nullopt;
    }

    lv_image_decoder_args_t arguments{};
    arguments.no_cache = true;
    lv_image_decoder_dsc_t decoder{};
    if (lv_image_decoder_open(&decoder, source.c_str(), &arguments) != LV_RESULT_OK) {
        spdlog::warn("Music cover: cannot decode '{}'", path.string());
        return std::nullopt;
    }

    CoverImage output;
    output.width = output_size;
    output.height = output_size;
    output.pixels.assign(static_cast<std::size_t>(output_size * output_size), 0);
    std::vector<bool> sampled(output.pixels.size(), false);
    const SampleMap map = makeSampleMap(static_cast<int>(header.w), static_cast<int>(header.h), output_size);

    std::size_t sampled_count = 0;
    if (decoder.decoded != nullptr) {
        const lv_area_t area{0, 0, static_cast<int32_t>(header.w) - 1, static_cast<int32_t>(header.h) - 1};
        sampled_count += sampleArea(*decoder.decoded, area, map, output, sampled);
    } else {
        const lv_area_t full_area{0, 0, static_cast<int32_t>(header.w) - 1, static_cast<int32_t>(header.h) - 1};
        lv_area_t decoded_area{LV_COORD_MIN, LV_COORD_MIN, LV_COORD_MIN, LV_COORD_MIN};
        while (lv_image_decoder_get_area(&decoder, &full_area, &decoded_area) == LV_RESULT_OK) {
            if (decoder.decoded == nullptr) {
                break;
            }
            sampled_count += sampleArea(*decoder.decoded, decoded_area, map, output, sampled);
        }
    }
    lv_image_decoder_close(&decoder);

    if (sampled_count != output.pixels.size()) {
        spdlog::warn("Music cover: incomplete decode '{}' ({}/{})", path.string(), sampled_count, output.pixels.size());
        return std::nullopt;
    }
    return output;
}

}  // namespace music::rendering
