// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Color.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CnaStreet::Assets {

/**
 * @brief A linear-light RGBA image, the working format for texture generation.
 *
 * Float rather than bytes because every generator composites: a stain over a
 * gradient over an aggregate pattern, each step multiplying or screening the
 * one below. Doing that in 8 bits banding-quantises at every step, and the
 * result looks like a JPEG of a texture rather than a texture.
 *
 * The channel meaning depends on the map. Colour maps hold **linear** light and
 * are converted to sRGB exactly once, on the way to the GPU; normal maps hold a
 * vector encoded as `n*0.5+0.5` and must never be sRGB-converted; ORM maps hold
 * three unrelated scalars and likewise stay linear. Getting that wrong is the
 * single most common way a PBR scene ends up looking washed out.
 */
class Image
{
public:
    Image() = default;
    Image(int width, int height, float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f);

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] bool empty() const { return width_ <= 0 || height_ <= 0; }

    [[nodiscard]] float* at(int x, int y) { return &data_[index(x, y)]; }
    [[nodiscard]] const float* at(int x, int y) const { return &data_[index(x, y)]; }

    void set(int x, int y, float r, float g, float b, float a = 1.0f);
    void setRgb(int x, int y, float r, float g, float b);
    /// Wrapping fetch, so filters can run over a tileable image without edge cases.
    [[nodiscard]] const float* wrapped(int x, int y) const;
    /// Bilinear sample in normalised coordinates, wrapping.
    void sampleBilinear(float u, float v, float out[4]) const;

    /// Per-pixel visitor: `fn(x, y, float rgba[4])`.
    template <typename Fn>
    void forEach(Fn&& fn)
    {
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) fn(x, y, &data_[index(x, y)]);
    }

    /// Blurs in place with a separable box filter, repeated to approximate a
    /// Gaussian. Wraps, so the result still tiles.
    void blur(int radius, int passes = 2);

    /// Half-resolution box downsample — one mip level.
    [[nodiscard]] Image downsampled() const;

    /// Encodes to the GPU's RGBA8. `srgb` applies the sRGB transfer curve, which
    /// is right for a base-colour or emissive map and wrong for everything else.
    [[nodiscard]] std::vector<Microsoft::Xna::Framework::Color> toColors(bool srgb) const;

    /// Writes an 8-bit RGBA PNG. Used by the offline texture-bake tool, which is
    /// what feeds CNA's content pipeline; the runtime never needs it.
    bool writePng(const std::string& path, bool srgb) const;

    /// Derives a tangent-space normal map from a height field held in this
    /// image's red channel. Sobel-filtered and wrapping.
    [[nodiscard]] Image toNormalMap(float strength) const;

    /// Copies the red channel of @p source into channel @p channel of this image.
    void setChannelFrom(int channel, const Image& source, int sourceChannel = 0);

private:
    [[nodiscard]] std::size_t index(int x, int y) const
    {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width_)
                + static_cast<std::size_t>(x)) * 4u;
    }

    int width_ = 0;
    int height_ = 0;
    std::vector<float> data_;
};

/**
 * @brief The three maps a PBR surface needs, produced together.
 *
 * `orm` follows the glTF packing so that one image can be bound as both the
 * occlusion map and the metallic-roughness map: R = ambient occlusion,
 * G = roughness, B = metallic. `PbrEffect` reads exactly those channels.
 */
struct SurfaceMaps
{
    Image albedo;    ///< linear base colour, alpha = opacity/mask
    Image normal;    ///< tangent-space normal, encoded n*0.5+0.5
    Image orm;       ///< R occlusion, G roughness, B metallic
    Image emissive;  ///< optional; empty when the surface does not glow

    [[nodiscard]] bool hasEmissive() const { return !emissive.empty(); }
};

}  // namespace CnaStreet::Assets
