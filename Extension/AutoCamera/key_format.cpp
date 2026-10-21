#include "Extension/AutoCamera/key_format.h"
#include <cmath>
#include <cstring>
#include <format>

namespace dingosdk::auto_camera {
namespace {
bool plain_float(float value) noexcept {
    if (!std::isfinite(value)) return false;
    if (value == 0.0f) return true;
    const float size = std::fabs(value);
    return size >= 1e-6f && size <= 1e9f;
}
} // namespace

std::string describe_raw(std::span<const std::uint32_t> words) {
    std::string text;
    for (std::size_t i = 0; i < words.size(); ++i) {
        float value;
        std::memcpy(&value, &words[i], sizeof(value));
        if (!text.empty()) text += ' ';
        if (plain_float(value))
            text += std::format("{}={:g}", i, value);
        else
            text += std::format("{}=0x{:08x}", i, words[i]);
    }
    return text;
}

std::string describe_slot(std::span<const float> slot, std::span<const layout::Lane> lanes) {
    std::string text;
    for (std::size_t i = 0; i < slot.size(); ++i) {
        bool played = false;
        for (const auto& lane : lanes) played = played || lane.slot_float == i;
        if (!text.empty()) text += ' ';
        text += std::format("{}{}={:g}", i, played ? "*" : "", slot[i]);
    }
    return text;
}
} // namespace dingosdk::auto_camera
