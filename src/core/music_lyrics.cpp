#include "core/music_lyrics.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace music {
namespace {

std::string trim(std::string value)
{
    const auto not_space = [](unsigned char character) { return !std::isspace(character); };
    const auto first = std::find_if(value.begin(), value.end(), not_space);
    const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
    return first < last ? std::string(first, last) : std::string{};
}

bool parseTimestamp(std::string_view value, std::int64_t& time_ms)
{
    const std::size_t colon = value.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= value.size()) {
        return false;
    }

    const auto parseUnsigned = [](std::string_view part, std::int64_t& number) {
        if (part.empty()) {
            return false;
        }
        number = 0;
        for (const unsigned char character : part) {
            if (!std::isdigit(character)) {
                return false;
            }
            const int digit = character - '0';
            if (number > (std::numeric_limits<std::int64_t>::max() - digit) / 10) {
                return false;
            }
            number = number * 10 + digit;
        }
        return true;
    };

    const std::string_view minutes_part = value.substr(0, colon);
    const std::string_view seconds_value = value.substr(colon + 1);
    const std::size_t decimal = seconds_value.find_first_of(".:");
    const std::string_view seconds_part = seconds_value.substr(0, decimal);
    const std::string_view fraction_part =
        decimal == std::string_view::npos ? std::string_view{} : seconds_value.substr(decimal + 1);
    std::int64_t minutes = 0;
    std::int64_t seconds = 0;
    std::int64_t fraction = 0;
    if (!parseUnsigned(minutes_part, minutes) || !parseUnsigned(seconds_part, seconds) ||
        (!fraction_part.empty() && !parseUnsigned(fraction_part.substr(0, 3), fraction))) {
        return false;
    }
    if (seconds >= 60 || minutes > (std::numeric_limits<std::int64_t>::max() - seconds) / 60) {
        return false;
    }

    std::int64_t fraction_ms = 0;
    if (!fraction_part.empty()) {
        if (fraction_part.size() == 1) {
            fraction_ms = fraction * 100;
        } else if (fraction_part.size() == 2) {
            fraction_ms = fraction * 10;
        } else {
            fraction_ms = fraction;
        }
    }
    const std::int64_t total_seconds = minutes * 60 + seconds;
    if (total_seconds > (std::numeric_limits<std::int64_t>::max() - fraction_ms) / 1000) {
        return false;
    }
    time_ms = total_seconds * 1000 + fraction_ms;
    return true;
}

bool isMetadataTag(std::string_view value)
{
    const std::size_t colon = value.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    const std::string_view key = value.substr(0, colon);
    return !key.empty() &&
           std::all_of(key.begin(), key.end(), [](unsigned char character) { return std::isalpha(character) != 0; });
}

}  // namespace

LyricsDocument parseLyrics(const std::string& contents)
{
    LyricsDocument result;
    std::string normalized = contents;
    if (normalized.size() >= 3 && static_cast<unsigned char>(normalized[0]) == 0xef &&
        static_cast<unsigned char>(normalized[1]) == 0xbb && static_cast<unsigned char>(normalized[2]) == 0xbf) {
        normalized.erase(0, 3);
    }

    std::vector<LyricLine> plain_lines;
    std::istringstream stream(normalized);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::vector<std::int64_t> timestamps;
        std::size_t cursor = 0;
        while (cursor < line.size() && line[cursor] == '[') {
            const std::size_t end = line.find(']', cursor + 1);
            if (end == std::string::npos) {
                break;
            }
            const std::string_view tag(line.data() + cursor + 1, end - cursor - 1);
            std::int64_t time_ms = 0;
            if (parseTimestamp(tag, time_ms)) {
                timestamps.push_back(time_ms);
            } else if (!isMetadataTag(tag)) {
                break;
            }
            cursor = end + 1;
        }

        const std::string text = trim(line.substr(cursor));
        if (!timestamps.empty()) {
            for (const std::int64_t timestamp : timestamps) {
                result.lines.push_back({timestamp, text});
            }
            continue;
        }
        if (!text.empty() && cursor == 0) {
            plain_lines.push_back({-1, text});
        }
    }

    if (!result.lines.empty()) {
        result.synchronized = true;
        std::stable_sort(result.lines.begin(), result.lines.end(),
                         [](const LyricLine& left, const LyricLine& right) { return left.time_ms < right.time_ms; });
    } else {
        result.lines = std::move(plain_lines);
    }
    return result;
}

}  // namespace music
