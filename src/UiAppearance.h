#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <string>
#include <string_view>

namespace notebot {

inline std::atomic<int> uiLanguageOverride{0}; // 0 follows Minecraft, 1 Chinese, 2 English.

enum class UiColor : size_t {
    Accent, Checkbox, Text, SecondaryText, Highlight, TitleText, Placeholder,
    BackgroundNormal, BackgroundHovered, BackgroundPressed, ActiveTab,
    OutlineNormal, OutlineHovered, OutlinePressed, SeparatorText, SeparatorLine,
    SliderHandle, SliderHandleHovered, SliderHandlePressed, SliderLeft, SliderRight,
    NowPlayingBackground, NowPlayingTitle, NowPlayingTime, NowPlayingProgress, NowPlayingTrack,
    NowPlayingOutline, Count
};

using UiRgba = std::array<float, 4>;
constexpr UiRgba uiRgba(int r, int g, int b, int a = 255) {
    return {r/255.f, g/255.f, b/255.f, a/255.f};
}

struct UiAppearance {
    float scale = 1.f;
    int titleAlignment = 1;
    int language = 0;
    bool helpText = true;
    bool hideHud = false; // Hide NoteBot's NowPlaying while its menu is open.
    std::array<UiRgba, static_cast<size_t>(UiColor::Count)> colors{{
        uiRgba(145,61,226), uiRgba(145,61,226), uiRgba(255,255,255), uiRgba(150,150,150),
        uiRgba(45,125,245,100), uiRgba(255,255,255), uiRgba(255,255,255,20),
        uiRgba(20,20,20,160), uiRgba(30,30,30,175), uiRgba(40,40,40,190), uiRgba(40,40,40,190),
        uiRgba(0,0,0), uiRgba(10,10,10), uiRgba(20,20,20), uiRgba(255,255,255), uiRgba(255,255,255,170),
        uiRgba(130,0,255), uiRgba(140,30,255), uiRgba(150,60,255), uiRgba(100,35,170), uiRgba(50,50,50),
        uiRgba(20,20,20,160), uiRgba(255,255,255), uiRgba(255,255,255), uiRgba(145,61,226),
        uiRgba(20,20,20,160), uiRgba(0,0,0)
    }};
    std::array<bool, static_cast<size_t>(UiColor::Count)> rainbowColors{};
    const UiRgba& color(UiColor key) const { return colors[static_cast<size_t>(key)]; }
    UiRgba& color(UiColor key) { return colors[static_cast<size_t>(key)]; }
    bool operator==(const UiAppearance&) const = default;
};

inline bool sanitizeUiAppearance(UiAppearance& appearance) {
    const auto before = appearance;
    const UiAppearance defaults;
    appearance.scale = std::isfinite(appearance.scale) ? std::clamp(appearance.scale, .1f, 4.f) : 1.f;
    appearance.titleAlignment = std::clamp(appearance.titleAlignment, 0, 2);
    appearance.language = std::clamp(appearance.language, 0, 2);
    for (size_t i = 0; i < appearance.colors.size(); ++i)
        for (size_t channel = 0; channel < 4; ++channel) {
            auto& value = appearance.colors[i][channel];
            value = std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : defaults.colors[i][channel];
        }
    return appearance != before;
}

// Clipboard data contains only UI appearance, never playback settings or file paths.
std::string serializeUiAppearance(const UiAppearance& appearance);
bool deserializeUiAppearance(std::string_view text, UiAppearance& appearance);

} // namespace notebot
