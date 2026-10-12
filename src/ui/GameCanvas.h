#pragma once

#include <vector>
#include <string>

#include "ui/UiCanvas.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IMinecraftGame.h"
#include "mc/client/gui/CaretMeasureData.h"
#include "mc/client/gui/Font.h"
#include "mc/client/gui/FontHandle.h"
#include "mc/client/gui/FontRepository.h"
#include "mc/client/gui/TextAlignment.h"
#include "mc/client/gui/TextMeasureData.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/input/RectangleArea.h"

namespace aeris::ui {

inline Font& getClientFont(MinecraftUIRenderContext& context) {
    auto repo = context.mClient.getMinecraftGame_DEPRECATED().getFontRepository();
    return repo->getFontFromFontType("default").getFont();
}

inline mce::Color toMceColor(Color const& c, float alphaMultiplier = 1.0f) {
    return mce::Color{c.r, c.g, c.b, c.a * alphaMultiplier};
}

inline ::ui::TextAlignment toMceAlign(Align align) {
    switch (align) {
    case Align::Center: return ::ui::TextAlignment::Center;
    case Align::Right:  return ::ui::TextAlignment::Right;
    case Align::Left:
    default:            return ::ui::TextAlignment::Left;
    }
}

/**
 * @brief Batched MinecraftUIRenderContext implementation of Canvas.
 * Minimizes draw-call overhead and avoids random UI lockups.
 */
class GameCanvas final : public Canvas {
public:
    explicit GameCanvas(MinecraftUIRenderContext& context)
        : mContext(context), mFont(getClientFont(context)) {}

    void fill(float x, float y, float w, float h, Color color, float alpha = 1.0f) override {
        if (w <= 0.0f || h <= 0.0f) return;
        mFills.push_back({RectangleArea{x, x + w, y, y + h}, toMceColor(color), alpha});
    }

    void text(
        float              x,
        float              y,
        float              boxWidth,
        std::string const& value,
        Color              color,
        Align              align,
        float              size = 1.0f
    ) override {
        if (value.empty() || boxWidth <= 0.0f) return;
        mTexts.push_back({x, y, boxWidth, value, toMceColor(color), toMceAlign(align), size});
    }

    [[nodiscard]] float textWidth(std::string const& value, float size = 1.0f) const override {
        if (value.empty()) return 0.0f;
        return static_cast<float>(mFont.getLineLength(value, size, false));
    }

    void flush() override {
        if (!mFills.empty()) {
            for (auto const& f : mFills) {
                mContext.fillRectangle(f.rect, f.color, f.alpha);
            }
            mFills.clear();
            mContext.flushImages(mce::Color{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, HashedString{"ui_fillColor"});
        }

        if (!mTexts.empty()) {
            TextMeasureData const measure{1.0f, 0.0f, false, false, false, ::ui::TextAlignment::Left};
            CaretMeasureData const caret{-1, false};

            for (auto const& t : mTexts) {
                TextMeasureData m{t.size, 0.0f, false, false, false, t.align};
                mContext.drawText(
                    mFont,
                    RectangleArea{t.x, t.x + t.boxWidth, t.y, t.y + 14.0f * t.size},
                    std::string(t.value),
                    t.color,
                    t.size,
                    t.align,
                    m,
                    caret
                );
            }
            mTexts.clear();
            mContext.flushText(0.0f, std::nullopt);
        }
    }

private:
    struct FillCmd {
        RectangleArea rect;
        mce::Color    color;
        float         alpha = 1.0f;
    };

    struct TextCmd {
        float               x = 0.0f;
        float               y = 0.0f;
        float               boxWidth = 0.0f;
        std::string         value;
        mce::Color          color;
        ::ui::TextAlignment align = ::ui::TextAlignment::Left;
        float               size = 1.0f;
    };

    MinecraftUIRenderContext& mContext;
    Font&                     mFont;
    std::vector<FillCmd>      mFills;
    std::vector<TextCmd>      mTexts;
};

} // namespace aeris::ui
