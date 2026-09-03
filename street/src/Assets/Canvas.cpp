// SPDX-License-Identifier: MIT
#include "CnaStreet/Assets/Canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_map>

namespace CnaStreet::Assets {

namespace {

float Saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }

/// Hermite interpolation that also works with a descending edge pair -- see the
/// note on CnaStreet::Noise::smoothstep for why that matters.
float Smoothstep(float edge0, float edge1, float x)
{
    if (edge0 == edge1) return x < edge0 ? 0.0f : 1.0f;
    const float t = Saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

/// Distance from a point to a segment. The building block for every stroke.
float SegmentDistance(float px, float py, float ax, float ay, float bx, float by)
{
    const float vx = bx - ax, vy = by - ay;
    const float wx = px - ax, wy = py - ay;
    const float lengthSquared = vx * vx + vy * vy;
    const float t = lengthSquared > 1e-12f
                        ? std::clamp((wx * vx + wy * vy) / lengthSquared, 0.0f, 1.0f)
                        : 0.0f;
    const float dx = wx - vx * t, dy = wy - vy * t;
    return std::sqrt(dx * dx + dy * dy);
}

/// Signed distance to a convex polygon given as half-planes (outward normals).
float ConvexDistance(float px, float py, const float* nx, const float* ny, const float* d,
                     int count)
{
    float worst = -1e9f;
    for (int i = 0; i < count; ++i)
        worst = std::max(worst, px * nx[i] + py * ny[i] - d[i]);
    return worst;
}

// ---------------------------------------------------------------------------
// Stroke font. Each glyph is a set of polylines in a 0..1 box (x right, y down),
// with the baseline at y = 1 and the cap height at y = 0. Deliberately a stroke
// font and not a bitmap: signage lettering is set large, and a 5x7 bitmap scaled
// to 400 pixels looks like a spreadsheet, whereas a stroked skeleton with round
// caps looks like a stencilled plate -- which is what a street-name sign is.
// ---------------------------------------------------------------------------
struct Glyph
{
    std::vector<std::vector<float>> strokes;   ///< flat x,y pairs per polyline
    float advance = 0.62f;
};

const std::unordered_map<char, Glyph>& GlyphTable()
{
    static const std::unordered_map<char, Glyph> table = [] {
        std::unordered_map<char, Glyph> t;
        auto add = [&t](char c, float advance, std::vector<std::vector<float>> strokes) {
            t[c] = Glyph{std::move(strokes), advance};
        };
        // Letters are drawn on a 0.56-wide body with 0.06 side bearing.
        const float L = 0.06f, R = 0.56f, M = 0.31f;
        const float T = 0.0f, B = 1.0f, C = 0.5f;

        add('A', 0.66f, {{L, B, M, T, R, B}, {L + 0.10f, 0.66f, R - 0.10f, 0.66f}});
        add('B', 0.64f, {{L, T, L, B}, {L, T, R - 0.06f, T, R, 0.16f, R - 0.06f, C, L, C},
                         {L, C, R - 0.02f, C, R, 0.72f, R - 0.06f, B, L, B}});
        add('C', 0.64f, {{R, 0.20f, M, T, L, 0.28f, L, 0.72f, M, B, R, 0.80f}});
        add('D', 0.66f, {{L, T, L, B}, {L, T, M + 0.10f, T, R, 0.28f, R, 0.72f, M + 0.10f, B, L, B}});
        add('E', 0.60f, {{R, T, L, T, L, B, R, B}, {L, C, R - 0.10f, C}});
        add('F', 0.58f, {{R, T, L, T, L, B}, {L, C, R - 0.12f, C}});
        add('G', 0.68f, {{R, 0.20f, M, T, L, 0.28f, L, 0.72f, M, B, R, 0.76f, R, C, M + 0.04f, C}});
        add('H', 0.66f, {{L, T, L, B}, {R, T, R, B}, {L, C, R, C}});
        add('I', 0.34f, {{0.17f, T, 0.17f, B}});
        add('J', 0.56f, {{R - 0.06f, T, R - 0.06f, 0.78f, M, B, L, 0.80f}});
        add('K', 0.64f, {{L, T, L, B}, {R, T, L, 0.56f}, {L + 0.12f, 0.46f, R, B}});
        add('L', 0.56f, {{L, T, L, B, R, B}});
        add('M', 0.78f, {{L, B, L, T, 0.36f, 0.52f, 0.66f, T, 0.66f, B}});
        add('N', 0.68f, {{L, B, L, T, R, B, R, T}});
        add('O', 0.70f, {{M, T, L, 0.28f, L, 0.72f, M, B, R, 0.72f, R, 0.28f, M, T}});
        add('P', 0.62f, {{L, B, L, T, R - 0.04f, T, R, 0.20f, R - 0.04f, 0.44f, L, 0.46f}});
        add('Q', 0.70f, {{M, T, L, 0.28f, L, 0.72f, M, B, R, 0.72f, R, 0.28f, M, T},
                         {M + 0.06f, 0.74f, R + 0.02f, B}});
        add('R', 0.64f, {{L, B, L, T, R - 0.04f, T, R, 0.20f, R - 0.04f, 0.44f, L, 0.46f},
                         {M - 0.02f, 0.46f, R, B}});
        add('S', 0.62f, {{R, 0.16f, M, T, L, 0.18f, L + 0.04f, 0.40f, R - 0.02f, 0.58f, R, 0.80f,
                          M, B, L, 0.82f}});
        add('T', 0.60f, {{L - 0.02f, T, R + 0.02f, T}, {M, T, M, B}});
        add('U', 0.66f, {{L, T, L, 0.74f, M, B, R, 0.74f, R, T}});
        add('V', 0.66f, {{L, T, M, B, R, T}});
        add('W', 0.86f, {{L - 0.02f, T, 0.20f, B, 0.38f, 0.34f, 0.56f, B, 0.74f, T}});
        add('X', 0.64f, {{L, T, R, B}, {R, T, L, B}});
        add('Y', 0.64f, {{L, T, M, C}, {R, T, M, C}, {M, C, M, B}});
        add('Z', 0.62f, {{L, T, R, T, L, B, R, B}});

        add('0', 0.62f, {{M, T, L, 0.26f, L, 0.74f, M, B, R, 0.74f, R, 0.26f, M, T}});
        add('1', 0.44f, {{L + 0.02f, 0.20f, 0.24f, T, 0.24f, B}, {L, B, 0.44f, B}});
        add('2', 0.62f, {{L, 0.20f, M, T, R, 0.22f, L, B, R, B}});
        add('3', 0.62f, {{L, 0.16f, M, T, R, 0.20f, M, 0.48f}, {M, 0.48f, R, 0.72f, M, B, L, 0.84f}});
        add('4', 0.64f, {{R - 0.10f, B, R - 0.10f, T, L, 0.68f, R, 0.68f}});
        add('5', 0.62f, {{R, T, L, T, L, 0.44f, M, 0.38f, R, 0.60f, M, B, L, 0.86f}});
        add('6', 0.62f, {{R, 0.14f, M, T, L, 0.34f, L, 0.76f, M, B, R, 0.76f, R - 0.02f, 0.54f,
                          M - 0.04f, 0.44f, L, 0.54f}});
        add('7', 0.58f, {{L, T, R, T, M - 0.04f, B}});
        add('8', 0.62f, {{M, 0.48f, L, 0.30f, M, T, R, 0.30f, M, 0.48f, R, 0.74f, M, B, L, 0.74f,
                          M, 0.48f}});
        add('9', 0.62f, {{L, 0.86f, M, B, R, 0.66f, R, 0.24f, M, T, L, 0.24f, L + 0.02f, 0.42f,
                          M + 0.04f, 0.52f, R, 0.42f}});

        // Lower case. x-height top at 0.42, baseline at 1.0, descenders to 1.26.
        // Signage is set in capitals, but the diagnostics overlay reads far
        // better in mixed case, and the same stroke font serves both.
        const float X = 0.42f, D = 1.26f;
        add('a', 0.58f, {{L, 0.52f, M, 0.42f, R - 0.06f, 0.54f, R - 0.06f, B},
                         {R - 0.06f, 0.62f, M - 0.06f, 0.66f, L, 0.80f, M, B, R - 0.06f, 0.90f}});
        add('b', 0.58f, {{L, T, L, B}, {L, 0.54f, M, X, R - 0.04f, 0.60f, R - 0.04f, 0.84f,
                                        M, B, L, 0.90f}});
        add('c', 0.54f, {{R - 0.06f, 0.54f, M - 0.02f, X, L, 0.60f, L, 0.84f, M - 0.02f, B,
                          R - 0.06f, 0.90f}});
        add('d', 0.58f, {{R - 0.04f, T, R - 0.04f, B}, {R - 0.04f, 0.54f, M, X, L, 0.60f,
                                                        L, 0.84f, M, B, R - 0.04f, 0.90f}});
        add('e', 0.56f, {{L, 0.72f, R - 0.06f, 0.72f, R - 0.06f, 0.56f, M - 0.02f, X, L, 0.60f,
                          L, 0.84f, M, B, R - 0.06f, 0.90f}});
        add('f', 0.38f, {{0.30f, B, 0.30f, 0.16f, 0.40f, 0.04f}, {0.08f, X, 0.40f, X}});
        add('g', 0.58f, {{R - 0.04f, X, R - 0.04f, D - 0.02f, M, D, L + 0.02f, D - 0.10f},
                         {R - 0.04f, 0.54f, M, X, L, 0.60f, L, 0.84f, M, B, R - 0.04f, 0.90f}});
        add('h', 0.58f, {{L, T, L, B}, {L, 0.54f, M, X, R - 0.04f, 0.56f, R - 0.04f, B}});
        add('i', 0.28f, {{0.13f, X, 0.13f, B}, {0.13f, 0.24f, 0.15f, 0.26f}});
        add('j', 0.30f, {{0.16f, X, 0.16f, D - 0.04f, 0.06f, D}, {0.16f, 0.24f, 0.18f, 0.26f}});
        add('k', 0.54f, {{L, T, L, B}, {R - 0.08f, X, L + 0.02f, 0.76f}, {M - 0.06f, 0.70f,
                                                                          R - 0.04f, B}});
        add('l', 0.28f, {{0.13f, T, 0.13f, 0.92f, 0.22f, B}});
        add('m', 0.82f, {{L, B, L, X}, {L, 0.52f, 0.22f, X, 0.34f, 0.54f, 0.34f, B},
                         {0.34f, 0.52f, 0.50f, X, 0.62f, 0.54f, 0.62f, B}});
        add('n', 0.58f, {{L, B, L, X}, {L, 0.52f, M, X, R - 0.04f, 0.56f, R - 0.04f, B}});
        add('o', 0.58f, {{M, X, L, 0.58f, L, 0.84f, M, B, R - 0.04f, 0.84f, R - 0.04f, 0.58f,
                          M, X}});
        add('p', 0.58f, {{L, X, L, D}, {L, 0.54f, M, X, R - 0.04f, 0.60f, R - 0.04f, 0.84f,
                                        M, B, L, 0.90f}});
        add('q', 0.58f, {{R - 0.04f, X, R - 0.04f, D}, {R - 0.04f, 0.54f, M, X, L, 0.60f,
                                                        L, 0.84f, M, B, R - 0.04f, 0.90f}});
        add('r', 0.42f, {{L, B, L, X}, {L, 0.54f, M - 0.02f, X, R - 0.14f, 0.46f}});
        add('s', 0.52f, {{R - 0.08f, 0.50f, M - 0.04f, X, L, 0.52f, M, 0.70f, R - 0.08f, 0.80f,
                          M - 0.02f, B, L, 0.92f}});
        add('t', 0.38f, {{0.16f, 0.22f, 0.16f, 0.90f, 0.30f, B}, {0.04f, X, 0.34f, X}});
        add('u', 0.58f, {{L, X, L, 0.86f, M, B, R - 0.04f, 0.86f}, {R - 0.04f, X, R - 0.04f, B}});
        add('v', 0.54f, {{L, X, M - 0.04f, B, R - 0.08f, X}});
        add('w', 0.74f, {{L, X, 0.16f, B, 0.32f, 0.62f, 0.48f, B, 0.62f, X}});
        add('x', 0.52f, {{L, X, R - 0.08f, B}, {R - 0.08f, X, L, B}});
        add('y', 0.54f, {{L, X, M - 0.02f, 0.96f}, {R - 0.06f, X, 0.22f, D}});
        add('z', 0.52f, {{L, X, R - 0.08f, X, L, B, R - 0.08f, B}});
        (void)D;

        add('.', 0.28f, {{0.13f, B - 0.02f, 0.15f, B}});
        add('-', 0.44f, {{0.06f, C, 0.36f, C}});
        add('/', 0.44f, {{0.04f, B, 0.38f, T}});
        add(' ', 0.34f, {});
        add(':', 0.28f, {{0.13f, 0.34f, 0.15f, 0.36f}, {0.13f, 0.78f, 0.15f, 0.80f}});
        // Sentence-case accents are not needed; the plates are set in capitals.
        return t;
    }();
    return table;
}

/// Looks a character up, falling back to its upper-case form so a design that
/// asks for a glyph this font does not carry still gets a letter.
std::unordered_map<char, Glyph>::const_iterator FindGlyph(
    const std::unordered_map<char, Glyph>& table, char c)
{
    auto it = table.find(c);
    if (it != table.end()) return it;
    return table.find(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
}

}  // namespace

Canvas::Canvas(int size, float r, float g, float b, float a)
    : image_(size, size, r, g, b, a), size_(size),
      pixel_(size > 0 ? 1.0f / static_cast<float>(size) : 1.0f)
{
}

void Canvas::fill(float r, float g, float b, float a)
{
    image_.forEach([&](int, int, float* p) {
        p[0] = r; p[1] = g; p[2] = b; p[3] = a;
    });
}

void Canvas::blend(int x, int y, const float rgb[3], float coverage)
{
    if (coverage <= 0.0f || x < 0 || y < 0 || x >= size_ || y >= size_) return;
    coverage = Saturate(coverage);
    float* p = image_.at(x, y);
    for (int c = 0; c < 3; ++c) p[c] = p[c] * (1.0f - coverage) + rgb[c] * coverage;
    p[3] = p[3] + (1.0f - p[3]) * coverage;
}

template <typename Sdf>
void Canvas::rasterise(int x0, int y0, int x1, int y1, Sdf&& sdf, const float rgb[3], float alpha)
{
    x0 = std::max(0, x0); y0 = std::max(0, y0);
    x1 = std::min(size_, x1); y1 = std::min(size_, y1);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) * pixel_;
            const float v = (static_cast<float>(y) + 0.5f) * pixel_;
            // Coverage is one inside the shape and zero outside, over a band
            // one pixel wide. Written as `1 - smoothstep(-w, +w, d)` rather than
            // `smoothstep(+w, -w, d)` because a descending edge pair is exactly
            // the degenerate case a smoothstep is allowed to reject -- and when
            // it does, every glyph comes out as a solid block with the strokes
            // punched out of it, which is what happened here first time round.
            const float d = sdf(u, v);
            const float band = pixel_ * 0.6f;
            blend(x, y, rgb, (1.0f - Smoothstep(-band, band, d)) * alpha);
        }
}

void Canvas::disc(float cx, float cy, float radius, const float rgb[3], float alpha)
{
    const int pad = 2;
    rasterise(static_cast<int>((cx - radius) / pixel_) - pad,
              static_cast<int>((cy - radius) / pixel_) - pad,
              static_cast<int>((cx + radius) / pixel_) + pad,
              static_cast<int>((cy + radius) / pixel_) + pad,
              [=](float u, float v) {
                  return std::sqrt((u - cx) * (u - cx) + (v - cy) * (v - cy)) - radius;
              },
              rgb, alpha);
}

void Canvas::ring(float cx, float cy, float radius, float thickness, const float rgb[3],
                  float alpha)
{
    const float outer = radius + thickness * 0.5f;
    const int pad = 2;
    rasterise(static_cast<int>((cx - outer) / pixel_) - pad,
              static_cast<int>((cy - outer) / pixel_) - pad,
              static_cast<int>((cx + outer) / pixel_) + pad,
              static_cast<int>((cy + outer) / pixel_) + pad,
              [=](float u, float v) {
                  const float d = std::sqrt((u - cx) * (u - cx) + (v - cy) * (v - cy));
                  return std::fabs(d - radius) - thickness * 0.5f;
              },
              rgb, alpha);
}

void Canvas::roundedRect(float x0, float y0, float x1, float y1, float radius, const float rgb[3],
                         float alpha)
{
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    const float hx = std::fabs(x1 - x0) * 0.5f - radius;
    const float hy = std::fabs(y1 - y0) * 0.5f - radius;
    const int pad = 2;
    rasterise(static_cast<int>(std::min(x0, x1) / pixel_) - pad,
              static_cast<int>(std::min(y0, y1) / pixel_) - pad,
              static_cast<int>(std::max(x0, x1) / pixel_) + pad,
              static_cast<int>(std::max(y0, y1) / pixel_) + pad,
              [=](float u, float v) {
                  const float dx = std::max(std::fabs(u - cx) - std::max(hx, 0.0f), 0.0f);
                  const float dy = std::max(std::fabs(v - cy) - std::max(hy, 0.0f), 0.0f);
                  return std::sqrt(dx * dx + dy * dy) - radius;
              },
              rgb, alpha);
}

void Canvas::rectOutline(float x0, float y0, float x1, float y1, float radius, float thickness,
                         const float rgb[3], float alpha)
{
    const float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    const float hx = std::fabs(x1 - x0) * 0.5f - radius;
    const float hy = std::fabs(y1 - y0) * 0.5f - radius;
    const int pad = 3;
    rasterise(static_cast<int>(std::min(x0, x1) / pixel_) - pad,
              static_cast<int>(std::min(y0, y1) / pixel_) - pad,
              static_cast<int>(std::max(x0, x1) / pixel_) + pad,
              static_cast<int>(std::max(y0, y1) / pixel_) + pad,
              [=](float u, float v) {
                  const float dx = std::max(std::fabs(u - cx) - std::max(hx, 0.0f), 0.0f);
                  const float dy = std::max(std::fabs(v - cy) - std::max(hy, 0.0f), 0.0f);
                  return std::fabs(std::sqrt(dx * dx + dy * dy) - radius) - thickness * 0.5f;
              },
              rgb, alpha);
}

namespace {

/// Half-plane description of an equilateral triangle with rounded corners.
void TriangleHalfPlanes(float cx, float cy, float halfWidth, bool pointUp, float nx[3],
                        float ny[3], float d[3])
{
    // Three outward normals 120 degrees apart, starting from straight down for a
    // point-up triangle. The inradius of an equilateral triangle of half-width h
    // is h / sqrt(3).
    const float inradius = halfWidth / 1.7320508f;
    const float flip = pointUp ? 1.0f : -1.0f;
    const float angles[3] = {1.57079633f, 1.57079633f + 2.09439510f, 1.57079633f - 2.09439510f};
    for (int i = 0; i < 3; ++i)
    {
        nx[i] = std::cos(angles[i]);
        ny[i] = std::sin(angles[i]) * flip;
        d[i]  = inradius + (nx[i] * cx + ny[i] * cy);
    }
}

}  // namespace

void Canvas::triangle(float cx, float cy, float halfWidth, bool pointUp, float corner,
                      const float rgb[3], float alpha)
{
    float nx[3], ny[3], d[3];
    TriangleHalfPlanes(cx, cy, halfWidth, pointUp, nx, ny, d);
    for (int i = 0; i < 3; ++i) d[i] -= corner;
    const int pad = 3;
    const float reach = halfWidth * 1.3f;
    rasterise(static_cast<int>((cx - reach) / pixel_) - pad,
              static_cast<int>((cy - reach) / pixel_) - pad,
              static_cast<int>((cx + reach) / pixel_) + pad,
              static_cast<int>((cy + reach) / pixel_) + pad,
              [=](float u, float v) {
                  return ConvexDistance(u, v, nx, ny, d, 3) - corner;
              },
              rgb, alpha);
}

void Canvas::triangleOutline(float cx, float cy, float halfWidth, bool pointUp, float corner,
                             float thickness, const float rgb[3], float alpha)
{
    float nx[3], ny[3], d[3];
    TriangleHalfPlanes(cx, cy, halfWidth, pointUp, nx, ny, d);
    for (int i = 0; i < 3; ++i) d[i] -= corner;
    const int pad = 3;
    const float reach = halfWidth * 1.3f;
    rasterise(static_cast<int>((cx - reach) / pixel_) - pad,
              static_cast<int>((cy - reach) / pixel_) - pad,
              static_cast<int>((cx + reach) / pixel_) + pad,
              static_cast<int>((cy + reach) / pixel_) + pad,
              [=](float u, float v) {
                  return std::fabs(ConvexDistance(u, v, nx, ny, d, 3) - corner)
                         - thickness * 0.5f;
              },
              rgb, alpha);
}

void Canvas::diamond(float cx, float cy, float halfWidth, float corner, const float rgb[3],
                     float alpha)
{
    const int pad = 3;
    rasterise(static_cast<int>((cx - halfWidth * 1.2f) / pixel_) - pad,
              static_cast<int>((cy - halfWidth * 1.2f) / pixel_) - pad,
              static_cast<int>((cx + halfWidth * 1.2f) / pixel_) + pad,
              static_cast<int>((cy + halfWidth * 1.2f) / pixel_) + pad,
              [=](float u, float v) {
                  return (std::fabs(u - cx) + std::fabs(v - cy)) * 0.70710678f
                         - (halfWidth * 0.70710678f - corner) - corner;
              },
              rgb, alpha);
}

void Canvas::diamondOutline(float cx, float cy, float halfWidth, float corner, float thickness,
                            const float rgb[3], float alpha)
{
    const int pad = 3;
    rasterise(static_cast<int>((cx - halfWidth * 1.2f) / pixel_) - pad,
              static_cast<int>((cy - halfWidth * 1.2f) / pixel_) - pad,
              static_cast<int>((cx + halfWidth * 1.2f) / pixel_) + pad,
              static_cast<int>((cy + halfWidth * 1.2f) / pixel_) + pad,
              [=](float u, float v) {
                  const float d = (std::fabs(u - cx) + std::fabs(v - cy)) * 0.70710678f
                                  - (halfWidth * 0.70710678f - corner);
                  return std::fabs(d - corner) - thickness * 0.5f;
              },
              rgb, alpha);
}

void Canvas::line(float x0, float y0, float x1, float y1, float width, const float rgb[3],
                  float alpha)
{
    const int pad = 3;
    const float reach = width;
    rasterise(static_cast<int>((std::min(x0, x1) - reach) / pixel_) - pad,
              static_cast<int>((std::min(y0, y1) - reach) / pixel_) - pad,
              static_cast<int>((std::max(x0, x1) + reach) / pixel_) + pad,
              static_cast<int>((std::max(y0, y1) + reach) / pixel_) + pad,
              [=](float u, float v) {
                  return SegmentDistance(u, v, x0, y0, x1, y1) - width * 0.5f;
              },
              rgb, alpha);
}

void Canvas::polygon(const std::vector<float>& xy, const float rgb[3], float alpha)
{
    if (xy.size() < 6) return;
    const std::size_t count = xy.size() / 2;
    float minX = xy[0], maxX = xy[0], minY = xy[1], maxY = xy[1];
    for (std::size_t i = 0; i < count; ++i)
    {
        minX = std::min(minX, xy[i * 2]);     maxX = std::max(maxX, xy[i * 2]);
        minY = std::min(minY, xy[i * 2 + 1]); maxY = std::max(maxY, xy[i * 2 + 1]);
    }
    const int pad = 2;
    // Even-odd crossing test for the inside, with the distance to the boundary
    // supplying the anti-aliasing. Works for concave outlines, which the arrow
    // and the pictograms need.
    rasterise(static_cast<int>(minX / pixel_) - pad, static_cast<int>(minY / pixel_) - pad,
              static_cast<int>(maxX / pixel_) + pad, static_cast<int>(maxY / pixel_) + pad,
              [&](float u, float v) {
                  bool inside = false;
                  float best = 1e9f;
                  for (std::size_t i = 0, j = count - 1; i < count; j = i++)
                  {
                      const float xi = xy[i * 2], yi = xy[i * 2 + 1];
                      const float xj = xy[j * 2], yj = xy[j * 2 + 1];
                      if ((yi > v) != (yj > v)
                          && u < (xj - xi) * (v - yi) / (yj - yi + 1e-12f) + xi)
                          inside = !inside;
                      best = std::min(best, SegmentDistance(u, v, xi, yi, xj, yj));
                  }
                  return inside ? -best : best;
              },
              rgb, alpha);
}

void Canvas::arrow(float x0, float y0, float x1, float y1, float shaftWidth, float headWidth,
                   float headLength, const float rgb[3])
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-6f) return;
    const float ux = dx / length, uy = dy / length;
    const float px = -uy, py = ux;

    const float bx = x1 - ux * headLength, by = y1 - uy * headLength;
    const std::vector<float> outline = {
        x0 + px * shaftWidth * 0.5f, y0 + py * shaftWidth * 0.5f,
        bx + px * shaftWidth * 0.5f, by + py * shaftWidth * 0.5f,
        bx + px * headWidth * 0.5f,  by + py * headWidth * 0.5f,
        x1,                          y1,
        bx - px * headWidth * 0.5f,  by - py * headWidth * 0.5f,
        bx - px * shaftWidth * 0.5f, by - py * shaftWidth * 0.5f,
        x0 - px * shaftWidth * 0.5f, y0 - py * shaftWidth * 0.5f};
    polygon(outline, rgb, 1.0f);
}

float Canvas::measureText(const std::string& value, float height) const
{
    const auto& table = GlyphTable();
    float width = 0.0f;
    for (const char raw : value)
    {
        const auto it = FindGlyph(table, raw);
        width += (it != table.end() ? it->second.advance : 0.5f) * height;
    }
    return width;
}

float Canvas::text(const std::string& value, float x, float y, float height, float weight,
                   const float rgb[3], bool centred)
{
    const auto& table = GlyphTable();
    float cursor = centred ? x - measureText(value, height) * 0.5f : x;

    for (const char raw : value)
    {
        const auto it = FindGlyph(table, raw);
        if (it == table.end())
        {
            cursor += 0.5f * height;
            continue;
        }
        for (const std::vector<float>& stroke : it->second.strokes)
            for (std::size_t i = 0; i + 3 < stroke.size(); i += 2)
                line(cursor + stroke[i] * height, y + stroke[i + 1] * height,
                     cursor + stroke[i + 2] * height, y + stroke[i + 3] * height, weight, rgb);
        cursor += it->second.advance * height;
    }
    return cursor - x;
}

}  // namespace CnaStreet::Assets
