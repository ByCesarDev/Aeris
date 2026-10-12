#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace aeris::ui {

constexpr int kMaxPackCards = 32;
constexpr int kMaxActionButtons = 4;

struct Rect {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    [[nodiscard]] constexpr float width() const { return std::max(0.0f, x1 - x0); }
    [[nodiscard]] constexpr float height() const { return std::max(0.0f, y1 - y0); }
    [[nodiscard]] constexpr bool valid() const { return x1 > x0 && y1 > y0; }
    [[nodiscard]] constexpr bool contains(float x, float y) const {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1;
    }
};

enum class ButtonId {
    None = -1,
    Import = 0,
    OpenFolder = 1,
    Diagnostics = 2,
    Done = 3
};

struct Layout {
    float screenWidth = 0.0f;
    float screenHeight = 0.0f;

    // Header
    Rect headerRect;
    Rect masterToggleRect;

    // Center content split
    Rect contentArea;
    Rect listArea;
    Rect detailsArea;

    // Cards in the pack list
    Rect cardRects[kMaxPackCards];
    int  cardCount = 0;
    float cardHeight = 36.0f;

    // Scrollbar for the pack list
    Rect scrollTrack;
    Rect scrollHandle;

    // Bottom action bar
    Rect bottomBarRect;
    Rect buttons[kMaxActionButtons];
    int  buttonCount = 0;

    // Hit testing
    [[nodiscard]] int hitCard(float x, float y) const;
    [[nodiscard]] ButtonId hitButton(float x, float y) const;
    [[nodiscard]] bool hitMasterToggle(float x, float y) const;
    [[nodiscard]] bool hitScrollTrack(float x, float y) const;
};

/**
 * @brief Computes responsive layout geometry based on viewport dimensions.
 * @param width Viewport width in GUI units.
 * @param height Viewport height in GUI units.
 * @param totalPacks Number of items in the list (including Vanilla card at index 0).
 * @param scrollOffset Scroll vertical offset in GUI units.
 * @param showDiagnostics Whether the diagnostics panel is active instead of details.
 */
Layout buildLayout(
    float width,
    float height,
    int totalPacks,
    float scrollOffset = 0.0f,
    bool showDiagnostics = false
);

constexpr float kHeaderHeight = 38.0f;
constexpr float kBottomBarHeight = 36.0f;

} // namespace aeris::ui
