#pragma once

#include <string>

namespace aeris::ui {

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

enum class Align {
    Left,
    Center,
    Right
};

/**
 * @brief Pure abstract canvas interface for Aeris UI.
 * Completely independent of game types so layouts can be 100% unit tested offline.
 */
class Canvas {
public:
    virtual ~Canvas() = default;

    /**
     * @brief Draws a filled solid rectangle.
     */
    virtual void fill(float x, float y, float w, float h, Color color, float alpha = 1.0f) = 0;

    /**
     * @brief Draws a single line of text bounded inside [x, x + boxWidth].
     */
    virtual void text(
        float              x,
        float              y,
        float              boxWidth,
        std::string const& value,
        Color              color,
        Align              align,
        float              size = 1.0f
    ) = 0;

    /**
     * @brief Measures the exact width of a text string in GUI units.
     */
    [[nodiscard]] virtual float textWidth(std::string const& value, float size = 1.0f) const {
        return static_cast<float>(value.size()) * 6.0f * size;
    }

    /**
     * @brief Commits/flushes queued batches.
     */
    virtual void flush() {}
};

} // namespace aeris::ui
