#include "rendering/cover_image_loader.hpp"
#include "rendering/artwork_palette.hpp"

#include <lvgl.h>

#include <jpeglib.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

#ifndef MUSIC_BUNDLED_ARTWORK_TEST_ROOT
#define MUSIC_BUNDLED_ARTWORK_TEST_ROOT ""
#endif

extern "C" {
unsigned lodepng_encode32_file(const char* filename, const unsigned char* image, unsigned width, unsigned height);
const char* lodepng_error_text(unsigned code);
}

namespace {

namespace fs = std::filesystem;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        _path = fs::temp_directory_path() /
                ("music-cover-loader-test-" + std::to_string(::getpid()) + "-" + std::to_string(nonce));
        require(fs::create_directories(_path), "failed to create temporary test directory");
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        fs::remove_all(_path, error);
    }

    const fs::path& path() const noexcept { return _path; }

private:
    fs::path _path;
};

int red(std::uint16_t color) { return static_cast<int>((color >> 11U) & 0x1fU) * 255 / 31; }

int green(std::uint16_t color) { return static_cast<int>((color >> 5U) & 0x3fU) * 255 / 63; }

int blue(std::uint16_t color) { return static_cast<int>(color & 0x1fU) * 255 / 31; }

std::uint16_t rgb565(std::uint8_t red_value, std::uint8_t green_value, std::uint8_t blue_value)
{
    return static_cast<std::uint16_t>(((red_value & 0xf8U) << 8U) | ((green_value & 0xfcU) << 3U) | (blue_value >> 3U));
}

void writePng(const fs::path& path)
{
    constexpr std::array<unsigned char, 16> pixels = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    };
    const unsigned int error = lodepng_encode32_file(path.string().c_str(), pixels.data(), 2, 2);
    require(error == 0, "failed to create PNG fixture: " + std::string(lodepng_error_text(error)));
}

void writeJpeg(const fs::path& path, bool progressive)
{
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    require(file != nullptr, "failed to create JPEG fixture");

    jpeg_compress_struct compressor{};
    jpeg_error_mgr error{};
    compressor.err = jpeg_std_error(&error);
    jpeg_create_compress(&compressor);
    jpeg_stdio_dest(&compressor, file);
    compressor.image_width = 4;
    compressor.image_height = 4;
    compressor.input_components = 3;
    compressor.in_color_space = JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, 90, TRUE);
    if (progressive) {
        jpeg_simple_progression(&compressor);
    }

    std::array<JSAMPLE, 4 * 4 * 3> pixels{};
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const std::size_t offset = static_cast<std::size_t>((y * 4 + x) * 3);
            pixels[offset] = static_cast<JSAMPLE>(40 + x * 50);
            pixels[offset + 1] = static_cast<JSAMPLE>(30 + y * 55);
            pixels[offset + 2] = static_cast<JSAMPLE>(180 - x * 20);
        }
    }

    jpeg_start_compress(&compressor, TRUE);
    while (compressor.next_scanline < compressor.image_height) {
        JSAMPROW row = pixels.data() + static_cast<std::size_t>(compressor.next_scanline) * 4U * 3U;
        require(jpeg_write_scanlines(&compressor, &row, 1) == 1, "failed to write JPEG fixture row");
    }
    jpeg_finish_compress(&compressor);
    jpeg_destroy_compress(&compressor);
    require(std::fclose(file) == 0, "failed to close JPEG fixture");
}

std::vector<unsigned char> readBinary(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "failed to open binary fixture");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void writeBinary(const fs::path& path, const std::vector<unsigned char>& bytes)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(stream), "failed to write binary fixture");
}

void writeJpegWithDimensions(const fs::path& source, const fs::path& destination, unsigned char sof_marker,
                             std::uint16_t width, std::uint16_t height)
{
    std::vector<unsigned char> bytes = readBinary(source);
    bool patched = false;
    for (std::size_t index = 0; index + 8 < bytes.size(); ++index) {
        if (bytes[index] != 0xffU || bytes[index + 1] != sof_marker) {
            continue;
        }
        bytes[index + 5] = static_cast<unsigned char>(height >> 8U);
        bytes[index + 6] = static_cast<unsigned char>(height & 0xffU);
        bytes[index + 7] = static_cast<unsigned char>(width >> 8U);
        bytes[index + 8] = static_cast<unsigned char>(width & 0xffU);
        patched = true;
        break;
    }
    require(patched, "JPEG fixture did not contain the requested SOF marker");
    writeBinary(destination, bytes);
}

void testPngNativePath(const fs::path& directory)
{
    const fs::path path = directory / "cover.png";
    writePng(path);

    const auto image = music::rendering::loadCoverImage(path, 4);
    require(image && image->valid() && image->width == 4 && image->height == 4, "PNG cover did not decode");
    require(red(image->pixels[0]) > 220 && green(image->pixels[0]) < 30 && blue(image->pixels[0]) < 30,
            "PNG upper-left color mismatch");
    require(green(image->pixels[3]) > 220 && red(image->pixels[3]) < 30 && blue(image->pixels[3]) < 30,
            "PNG upper-right color mismatch");
    require(blue(image->pixels[12]) > 220 && red(image->pixels[12]) < 30 && green(image->pixels[12]) < 30,
            "PNG lower-left color mismatch");
    require(red(image->pixels[15]) > 220 && green(image->pixels[15]) > 220 && blue(image->pixels[15]) > 220,
            "PNG lower-right color mismatch");
}

void testJpegDecodeAndLimits(const fs::path& directory)
{
    const fs::path baseline = directory / "baseline.jpg";
    const fs::path progressive = directory / "progressive.jpg";
    writeJpeg(baseline, false);
    writeJpeg(progressive, true);

    const auto baseline_image = music::rendering::loadCoverImage(baseline, 8);
    const auto progressive_image = music::rendering::loadCoverImage(progressive, 8);
    require(baseline_image && baseline_image->valid(), "baseline JPEG did not decode");
    require(progressive_image && progressive_image->valid(), "progressive JPEG did not decode");

    const fs::path oversized_baseline = directory / "oversized-baseline.jpg";
    const fs::path oversized_progressive = directory / "oversized-progressive.jpg";
    writeJpegWithDimensions(baseline, oversized_baseline, 0xc0, 10000, 10000);
    writeJpegWithDimensions(progressive, oversized_progressive, 0xc2, 5000, 5000);
    require(!music::rendering::loadCoverImage(oversized_baseline, 8), "oversized baseline JPEG was accepted");
    require(!music::rendering::loadCoverImage(oversized_progressive, 8), "oversized progressive JPEG was accepted");
}

void testJpegErrorRecovery(const fs::path& directory)
{
    const fs::path valid = directory / "recovery.jpg";
    const fs::path truncated = directory / "truncated.jpg";
    writeJpeg(valid, false);

    std::vector<unsigned char> bytes = readBinary(valid);
    require(bytes.size() > 16, "JPEG fixture is unexpectedly small");
    bytes.resize(bytes.size() / 2);
    writeBinary(truncated, bytes);

    require(!music::rendering::loadCoverImage(truncated, 8), "truncated JPEG was accepted");
    const auto recovered = music::rendering::loadCoverImage(valid, 8);
    require(recovered && recovered->valid(), "JPEG decoder did not recover after a fatal decode error");
}

void testBundledArtwork()
{
    const fs::path root = MUSIC_BUNDLED_ARTWORK_TEST_ROOT;
    const std::array<fs::path, 4> artwork = {
        root / "all-music.jpg",
        root / "guide-add-music.jpg",
        root / "guide-cover-art.jpg",
        root / "guide-lyrics.jpg",
    };
    for (const auto& path : artwork) {
        const auto image = music::rendering::loadCoverImage(path, 128);
        require(image && image->valid(), "bundled artwork did not decode: " + path.string());
    }
}

void testDefaultArtworkFallback(const fs::path& directory)
{
    const auto expected =
        music::rendering::loadCoverImage(fs::path(MUSIC_BUNDLED_ARTWORK_TEST_ROOT) / "all-music.jpg", 32);
    require(expected && expected->valid(), "bundled All Music cover did not decode");

    const auto missing = music::rendering::loadCoverImageWithFallback(directory / "missing.jpg", 32);
    require(missing && missing->width == expected->width && missing->height == expected->height &&
                missing->pixels == expected->pixels,
            "missing artwork did not use the bundled All Music cover");

    const fs::path invalid = directory / "invalid.jpg";
    writeBinary(invalid, {0x00, 0x01, 0x02, 0x03});
    const auto unreadable = music::rendering::loadCoverImageWithFallback(invalid, 32);
    require(unreadable && unreadable->width == expected->width && unreadable->height == expected->height &&
                unreadable->pixels == expected->pixels,
            "unreadable artwork did not use the bundled All Music cover");
}

void testArtworkPalette()
{
    music::rendering::CoverImage artwork;
    artwork.width = 64;
    artwork.height = 64;
    artwork.pixels.assign(64U * 64U, rgb565(230, 36, 64));

    const auto red_theme = music::rendering::extractArtworkTheme(artwork);
    require(red_theme.background.red > red_theme.background.green + 35 &&
                red_theme.background.red > red_theme.background.blue + 25,
            "vibrant artwork hue was not preserved");
    require(red_theme.primary_text.red > 220 && red_theme.secondary_text.red > 140,
            "artwork theme did not generate readable text colors");

    std::fill(artwork.pixels.begin(), artwork.pixels.end(), rgb565(128, 128, 128));
    std::fill_n(artwork.pixels.begin(), artwork.pixels.size() / 3U, rgb565(24, 110, 230));
    const auto mixed_theme = music::rendering::extractArtworkTheme(artwork);
    require(mixed_theme.background.blue > mixed_theme.background.red + 20,
            "low-saturation pixels overpowered the vivid artwork color");

    const music::rendering::CoverImage invalid;
    const auto fallback = music::rendering::extractArtworkTheme(invalid);
    const auto expected = music::ui::defaultPageTheme();
    require(fallback.background.red == expected.background.red, "invalid artwork did not return the default theme");
}

}  // namespace

int main()
{
    lv_init();
    try {
        const TemporaryDirectory temporary;
        testPngNativePath(temporary.path());
        testJpegDecodeAndLimits(temporary.path());
        testJpegErrorRecovery(temporary.path());
        testBundledArtwork();
        testDefaultArtworkFallback(temporary.path());
        testArtworkPalette();
        lv_deinit();
        std::cout << "cover image loader tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        lv_deinit();
        std::cerr << "cover image loader test failed: " << error.what() << '\n';
        return 1;
    }
}
