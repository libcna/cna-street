// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <memory>
#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class ShaderEffect;
    class Texture2D;
    class TextureCube;
}

namespace CNA::Graphics {
    class FullscreenPass;
    class EnvironmentProcessor;
}

namespace CnaStreet {

struct RenderSettings;

/**
 * @brief The sky: atmosphere, sun disc, two cloud layers, and the light they give.
 *
 * Built on CNA's `AtmosphericSky` rather than beside it. That class exposes its
 * scattering model as GLSL through the static `getModelGlsl()`, so this system
 * compiles a `ShaderEffect` whose fragment program is *CNA's own sky radiance
 * function* plus a cloud layer and a sun disc on top. The alternative — writing
 * a second atmosphere model — would have meant the sky the camera sees and the
 * sky the scene is lit by disagreeing, which is exactly the sort of thing that
 * makes a render look wrong without anyone being able to say why.
 *
 * The same model then runs on the CPU (`AtmosphericSky::radiance`) to bake a
 * small environment cubemap, which `EnvironmentProcessor` turns into the
 * irradiance, prefiltered specular and BRDF LUT that `PbrEffect` uses for image
 * based lighting. So the ambient light in the street really is the light of the
 * sky above it.
 */
class SkySystem
{
public:
    explicit SkySystem(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~SkySystem();

    SkySystem(const SkySystem&) = delete;
    SkySystem& operator=(const SkySystem&) = delete;

    /// Compiles the sky shader and bakes the environment. Safe to call again
    /// when the sun moves; the cubemap is rebaked, which costs a few
    /// milliseconds on the CPU and is not done per frame.
    void build(const RenderSettings& settings);

    /// Re-bakes only the lighting for a new sun position.
    void updateSun(const RenderSettings& settings);

    /// Draws the sky over the whole target. Must run before the scene geometry
    /// with depth writes off.
    ///
    /// @p intensityScale multiplies the whole sky, and @p encodeSrgb writes it
    /// sRGB-encoded rather than linear. Both exist for the reflection-probe
    /// capture, which renders into an 8-bit target: the scale keeps a horizon
    /// brighter than 1.0 inside the range, and the encode is how a dark road
    /// keeps more than four distinguishable shades in it. The main frame passes
    /// neither.
    void draw(const Microsoft::Xna::Framework::Matrix& view,
              const Microsoft::Xna::Framework::Matrix& projection, int width, int height,
              float timeSeconds, float intensityScale = 1.0f, bool encodeSrgb = false);

    /// World direction of a cube-map texel, in the face layout `TextureCube`
    /// and `EnvironmentProcessor` use. Public because the reflection probes
    /// have to write their captures into the same layout.
    [[nodiscard]] static Microsoft::Xna::Framework::Vector3 cubeDirection(
        Microsoft::Xna::Framework::Graphics::CubeMapFace face, float u, float v);

    /// Whether the sky's screen-space Y has to be flipped. See the note in the
    /// fragment program: it depends on the texture-coordinate origin the
    /// fullscreen pass hands the shader, not on the target.
    void setFlipV(bool flip) { flipV_ = flip; }

    [[nodiscard]] bool isSupported() const { return supported_; }
    [[nodiscard]] const std::string& unsupportedReason() const { return reason_; }

    /// Unit vector *from* the scene *toward* the sun.
    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& sunDirection() const
    {
        return sunDirection_;
    }
    /// The direction light travels, which is what a directional light wants.
    [[nodiscard]] Microsoft::Xna::Framework::Vector3 lightDirection() const;
    /// Sun colour and intensity after atmospheric extinction at its elevation.
    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& sunColour() const
    {
        return sunColour_;
    }
    /// Average sky radiance over the upper hemisphere — the ambient term for
    /// materials that are not lit by the full IBL path.
    [[nodiscard]] const Microsoft::Xna::Framework::Vector3& ambientColour() const
    {
        return ambientColour_;
    }

    [[nodiscard]] Microsoft::Xna::Framework::Graphics::TextureCube* environment() const
    {
        return environment_.get();
    }
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::TextureCube* irradiance() const
    {
        return irradiance_.get();
    }
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::TextureCube* prefiltered() const
    {
        return prefiltered_.get();
    }
    [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* brdfLut() const
    {
        return brdfLut_.get();
    }
    [[nodiscard]] int prefilteredMipCount() const { return prefilteredMips_; }
    /// What the environment cube's texels have to be multiplied by to be
    /// radiance again. Goes into `ImageBasedLightEXT::Intensity`, which is the
    /// field CNA provides for exactly this: the products of the split sum are
    /// 8-bit, so a sky brighter than 1.0 lives half in the texture and half in
    /// this number.
    [[nodiscard]] float environmentScale() const { return environmentScale_; }
    [[nodiscard]] bool hasImageBasedLighting() const;

private:
    void bakeEnvironment(const RenderSettings& settings);
    void computeLighting(const RenderSettings& settings);

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::ShaderEffect> effect_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>    white_;
    std::unique_ptr<CNA::Graphics::FullscreenPass>                     fullscreen_;

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> environment_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> irradiance_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::TextureCube> prefiltered_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>   brdfLut_;
    float environmentScale_ = 1.0f;
    int prefilteredMips_ = 5;

    Microsoft::Xna::Framework::Vector3 sunDirection_{0.0f, 1.0f, 0.0f};
    Microsoft::Xna::Framework::Vector3 sunColour_{1.0f, 1.0f, 1.0f};
    Microsoft::Xna::Framework::Vector3 ambientColour_{0.2f, 0.24f, 0.3f};

    float turbidity_     = 2.9f;
    float skyIntensity_  = 1.0f;
    float cloudCoverage_ = 0.45f;
    float cloudSpeed_    = 0.006f;
    bool  cloudsEnabled_ = true;
    bool  flipV_         = true;
    bool  supported_     = false;
    std::string reason_;
};

}  // namespace CnaStreet
