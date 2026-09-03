// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Assets/Image.hpp"

#include <string>
#include <vector>

namespace CnaStreet::Assets {

/**
 * @brief A tiny anti-aliased 2-D drawing surface, for signage.
 *
 * Every road sign, street-name plate, number plate and shop fascia in the city
 * is drawn with this. It works on signed distance fields rather than on scanline
 * fills: coverage is `smoothstep` of the distance across one pixel, which gives
 * clean edges on discs and diagonals at any size and costs about ten lines of
 * code. A scanline rasteriser with supersampling would be more code for a worse
 * edge.
 *
 * Coordinates are normalised 0..1 with the origin at the top left, so a sign
 * design does not have to know what resolution it will be baked at.
 */
class Canvas
{
public:
    Canvas(int size, float r, float g, float b, float a = 1.0f);

    void fill(float r, float g, float b, float a = 1.0f);
    void disc(float cx, float cy, float radius, const float rgb[3], float alpha = 1.0f);
    void ring(float cx, float cy, float radius, float thickness, const float rgb[3],
              float alpha = 1.0f);
    /// Axis-aligned rectangle with an optional corner radius.
    void roundedRect(float x0, float y0, float x1, float y1, float radius, const float rgb[3],
                     float alpha = 1.0f);
    void rectOutline(float x0, float y0, float x1, float y1, float radius, float thickness,
                     const float rgb[3], float alpha = 1.0f);
    /// Equilateral triangle pointing up (or down), inscribed in the given box,
    /// with rounded corners the way a real warning sign has.
    void triangle(float cx, float cy, float halfWidth, bool pointUp, float corner,
                  const float rgb[3], float alpha = 1.0f);
    void triangleOutline(float cx, float cy, float halfWidth, bool pointUp, float corner,
                         float thickness, const float rgb[3], float alpha = 1.0f);
    /// Square rotated 45 degrees — the European priority-road sign.
    void diamond(float cx, float cy, float halfWidth, float corner, const float rgb[3],
                 float alpha = 1.0f);
    void diamondOutline(float cx, float cy, float halfWidth, float corner, float thickness,
                        const float rgb[3], float alpha = 1.0f);
    void line(float x0, float y0, float x1, float y1, float width, const float rgb[3],
              float alpha = 1.0f);
    void polygon(const std::vector<float>& xy, const float rgb[3], float alpha = 1.0f);
    /// A solid arrow from tail to head.
    void arrow(float x0, float y0, float x1, float y1, float shaftWidth, float headWidth,
               float headLength, const float rgb[3]);

    /// Draws text with the built-in stroke font. Returns the advance width.
    /// Upper case, digits and a few punctuation marks; that is what signage uses.
    float text(const std::string& value, float x, float y, float height, float weight,
               const float rgb[3], bool centred = false);
    /// Width the same call to @ref text would occupy.
    [[nodiscard]] float measureText(const std::string& value, float height) const;

    [[nodiscard]] Image& image() { return image_; }
    [[nodiscard]] const Image& image() const { return image_; }

private:
    /// Composites a colour with the given coverage. The one place blending
    /// happens, so every primitive gets the same premultiplied behaviour.
    void blend(int x, int y, const float rgb[3], float coverage);
    /// Rasterises a signed distance function: negative inside.
    template <typename Sdf>
    void rasterise(int x0, int y0, int x1, int y1, Sdf&& sdf, const float rgb[3], float alpha);

    Image image_;
    int   size_ = 0;
    float pixel_ = 0.0f;   ///< one pixel in normalised units, the AA width
};

}  // namespace CnaStreet::Assets
