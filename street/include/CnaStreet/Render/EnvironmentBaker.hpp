// SPDX-License-Identifier: MIT
#pragma once

#include <memory>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class Texture2D;
    class TextureCube;
}

namespace CnaStreet {

/**
 * @brief Turns an environment cube into what `PbrEffect`'s image-based light samples.
 *
 * The split-sum products -- a cosine-convolved irradiance cube, a GGX
 * prefiltered specular cube with one roughness per mip, and the BRDF lookup
 * table -- computed on the CPU from an 8-bit cube read back with `GetData`.
 * Every input and output texel is linear radiance divided by the scale the
 * caller keeps in `ImageBasedLightEXT::Intensity`; nothing here encodes or
 * decodes a curve.
 *
 */
class EnvironmentBaker
{
public:
    explicit EnvironmentBaker(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

    /// Cosine-weighted hemisphere integral per texel, on a regular
    /// `4n x n` grid in spherical coordinates for @p sampleCount `n`.
    [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> irradiance(
        Microsoft::Xna::Framework::Graphics::TextureCube& environment, int size,
        int sampleCount) const;

    /// GGX importance-sampled prefilter, roughness `mip / (mipCount - 1)`.
    [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube>
    prefilteredSpecular(Microsoft::Xna::Framework::Graphics::TextureCube& environment,
                        int baseSize, int mipCount, int sampleCount) const;

    /// Scale (red) and bias (green) to apply to F0, by NdotV across and
    /// roughness down.
    [[nodiscard]] std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D> brdfLut(
        int size, int sampleCount) const;

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
};

}  // namespace CnaStreet
