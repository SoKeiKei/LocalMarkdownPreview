#pragma once

#include <windows.h>

namespace LocalMarkdownPreview {

struct MarkdownThemePalette {
    COLORREF background = RGB(255, 255, 255);
    COLORREF text = RGB(31, 31, 31);
    COLORREF heading = RGB(20, 82, 138);
    COLORREF link = RGB(0, 95, 184);
    COLORREF secondary_text = RGB(88, 96, 105);
    COLORREF code_background = RGB(246, 247, 249);
    COLORREF code_text = RGB(31, 31, 31);
    COLORREF table_header_background = RGB(238, 244, 250);
    COLORREF divider = RGB(215, 218, 222);
    bool dark = false;
    bool high_contrast = false;
};

inline int PerceivedBrightness(COLORREF color) noexcept {
    return (GetRValue(color) * 299 + GetGValue(color) * 587 +
            GetBValue(color) * 114) /
           1000;
}

inline bool IsDarkThemeColor(COLORREF color) noexcept {
    return PerceivedBrightness(color) < 128;
}

inline COLORREF BlendThemeColor(COLORREF background,
                                COLORREF foreground,
                                unsigned foregroundWeight) noexcept {
    const unsigned weight = foregroundWeight > 255u ? 255u : foregroundWeight;
    const unsigned inverse = 255u - weight;
    const auto blendChannel = [inverse, weight](unsigned back, unsigned front) {
        return static_cast<BYTE>((back * inverse + front * weight + 127u) / 255u);
    };
    return RGB(blendChannel(GetRValue(background), GetRValue(foreground)),
               blendChannel(GetGValue(background), GetGValue(foreground)),
               blendChannel(GetBValue(background), GetBValue(foreground)));
}

inline bool HasReadableThemeContrast(COLORREF foreground,
                                     COLORREF background) noexcept {
    const int difference =
        PerceivedBrightness(foreground) - PerceivedBrightness(background);
    return difference >= 92 || difference <= -92;
}

inline MarkdownThemePalette BuildMarkdownThemePalette(
    COLORREF requestedBackground,
    bool hasRequestedBackground,
    COLORREF requestedText,
    bool hasRequestedText,
    bool systemDark,
    bool highContrast,
    COLORREF systemAccent) noexcept {
    MarkdownThemePalette palette;
    palette.dark = systemDark;
    palette.high_contrast = highContrast;

    if (highContrast) {
        palette.background = requestedBackground;
        palette.text = requestedText;
        palette.heading = requestedText;
        palette.link = systemAccent;
        palette.secondary_text = requestedText;
        palette.code_background = requestedBackground;
        palette.code_text = requestedText;
        palette.table_header_background = requestedBackground;
        palette.divider = requestedText;
        return palette;
    }

    const COLORREF fallbackBackground =
        systemDark ? RGB(32, 32, 32) : RGB(255, 255, 255);
    const COLORREF fallbackText =
        systemDark ? RGB(242, 242, 242) : RGB(31, 31, 31);
    const COLORREF accent =
        systemDark ? RGB(102, 179, 255) : RGB(0, 95, 184);

    palette.background =
        hasRequestedBackground && IsDarkThemeColor(requestedBackground) == systemDark
            ? requestedBackground
            : fallbackBackground;
    palette.text =
        hasRequestedText && HasReadableThemeContrast(requestedText, palette.background)
            ? requestedText
            : fallbackText;
    palette.heading = BlendThemeColor(palette.text, accent, systemDark ? 174u : 196u);
    palette.link = accent;
    palette.secondary_text =
        BlendThemeColor(palette.background, palette.text, systemDark ? 178u : 164u);
    palette.code_background =
        BlendThemeColor(palette.background, palette.text, systemDark ? 24u : 13u);
    palette.code_text = palette.text;
    palette.table_header_background =
        BlendThemeColor(palette.background, accent, systemDark ? 36u : 20u);
    palette.divider =
        BlendThemeColor(palette.background, palette.text, systemDark ? 54u : 38u);
    return palette;
}

} // namespace LocalMarkdownPreview
