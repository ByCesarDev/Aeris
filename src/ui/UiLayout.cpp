#include "ui/UiLayout.h"

namespace aeris::ui {

int Layout::hitCard(float x, float y) const {
    if (!listArea.contains(x, y)) return -1;
    for (int i = 0; i < cardCount; ++i) {
        if (cardRects[i].contains(x, y)) {
            return i;
        }
    }
    return -1;
}

ButtonId Layout::hitButton(float x, float y) const {
    for (int i = 0; i < buttonCount; ++i) {
        if (buttons[i].contains(x, y)) {
            return static_cast<ButtonId>(i);
        }
    }
    return ButtonId::None;
}

bool Layout::hitMasterToggle(float x, float y) const {
    return masterToggleRect.contains(x, y);
}

bool Layout::hitScrollTrack(float x, float y) const {
    return scrollTrack.contains(x, y);
}

Layout buildLayout(
    float width,
    float height,
    int totalPacks,
    float scrollOffset,
    bool /*showDiagnostics*/
) {
    Layout layout{};
    layout.screenWidth = width;
    layout.screenHeight = height;

    if (width <= 0.0f || height <= 0.0f) {
        return layout;
    }

    // 1. Header geometry
    layout.headerRect = Rect{0.0f, 0.0f, width, kHeaderHeight};
    float toggleWidth = 160.0f;
    float toggleHeight = 22.0f;
    layout.masterToggleRect = Rect{
        width - toggleWidth - 16.0f,
        (kHeaderHeight - toggleHeight) * 0.5f,
        width - 16.0f,
        (kHeaderHeight + toggleHeight) * 0.5f
    };

    // 2. Bottom bar geometry
    float bottomY = height - kBottomBarHeight;
    layout.bottomBarRect = Rect{0.0f, bottomY, width, height};

    layout.buttonCount = kMaxActionButtons;
    float buttonW = std::clamp(width * 0.22f, 80.0f, 130.0f);
    float buttonH = 22.0f;
    float buttonGap = 8.0f;
    float totalBtnWidth = static_cast<float>(layout.buttonCount) * buttonW + 
                         static_cast<float>(layout.buttonCount - 1) * buttonGap;
    float btnStartX = (width - totalBtnWidth) * 0.5f;

    for (int i = 0; i < layout.buttonCount; ++i) {
        float bx = btnStartX + static_cast<float>(i) * (buttonW + buttonGap);
        float by = bottomY + (kBottomBarHeight - buttonH) * 0.5f;
        layout.buttons[i] = Rect{bx, by, bx + buttonW, by + buttonH};
    }

    // 3. Central area split
    layout.contentArea = Rect{12.0f, kHeaderHeight + 6.0f, width - 12.0f, bottomY - 6.0f};

    float contentW = layout.contentArea.width();
    float contentH = layout.contentArea.height();

    // Two-column layout: Left = Pack List (58%), Right = Details/Diagnostics (42%)
    float splitX = layout.contentArea.x0 + contentW * 0.58f;
    layout.listArea = Rect{layout.contentArea.x0, layout.contentArea.y0, splitX - 6.0f, layout.contentArea.y1};
    layout.detailsArea = Rect{splitX + 6.0f, layout.contentArea.y0, layout.contentArea.x1, layout.contentArea.y1};

    // 4. Cards layout
    layout.cardHeight = 36.0f;
    float cardGap = 4.0f;
    float listW = layout.listArea.width() - 12.0f; // Leave space for scrollbar

    layout.cardCount = std::min(totalPacks, kMaxPackCards);
    for (int i = 0; i < layout.cardCount; ++i) {
        float cy = layout.listArea.y0 + static_cast<float>(i) * (layout.cardHeight + cardGap) - scrollOffset;
        layout.cardRects[i] = Rect{
            layout.listArea.x0 + 4.0f,
            cy,
            layout.listArea.x0 + 4.0f + listW,
            cy + layout.cardHeight
        };
    }

    // 5. Scrollbar for pack list
    float totalContentHeight = static_cast<float>(totalPacks) * (layout.cardHeight + cardGap);
    layout.scrollTrack = Rect{layout.listArea.x1 - 8.0f, layout.listArea.y0, layout.listArea.x1 - 2.0f, layout.listArea.y1};

    if (totalContentHeight > contentH && totalContentHeight > 0.0f) {
        float ratio = std::clamp(contentH / totalContentHeight, 0.1f, 1.0f);
        float handleH = std::max(20.0f, contentH * ratio);
        float maxScroll = totalContentHeight - contentH;
        float scrollRatio = std::clamp(scrollOffset / (maxScroll > 0.0f ? maxScroll : 1.0f), 0.0f, 1.0f);
        float handleY = layout.scrollTrack.y0 + scrollRatio * (contentH - handleH);
        layout.scrollHandle = Rect{layout.scrollTrack.x0, handleY, layout.scrollTrack.x1, handleY + handleH};
    } else {
        layout.scrollHandle = Rect{};
    }

    return layout;
}

} // namespace aeris::ui
