// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework {
    class GameTime;
}
namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class SpriteBatch;
    class SpriteFont;
    class Texture2D;
}

namespace CnaStreet {

class Camera;
class CameraController;
class CityScene;
class SceneRenderer;
struct RenderSettings;

/**
 * @brief The diagnostics panel, toggled with F1.
 *
 * Draws with `SpriteBatch` and a `SpriteFont` built at run time from the same
 * stroke font the road signs use — there is no font file to ship and no content
 * pipeline step to run, and the glyphs are vector shapes baked into an atlas at
 * start-up rather than a bitmap that would only look right at one size.
 *
 * What it reports is chosen to answer the questions that actually come up while
 * working on a scene like this: is the frame time CPU or GPU bound, how much is
 * being culled, how many draw calls the batching is really producing, which EXT
 * subsystems the renderer refused, and where the camera is so a viewpoint can be
 * written down.
 */
class DebugOverlay
{
public:
    explicit DebugOverlay(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);
    ~DebugOverlay();

    DebugOverlay(const DebugOverlay&) = delete;
    DebugOverlay& operator=(const DebugOverlay&) = delete;

    /// Bakes the font atlas and creates the sprite batch.
    void build();

    void draw(const SceneRenderer& renderer, const CityScene& scene, const Camera& camera,
              const CameraController& controller, const RenderSettings& settings,
              const Microsoft::Xna::Framework::GameTime& gameTime);

    /// A one-line summary for the window title and the log.
    [[nodiscard]] std::string summary(const SceneRenderer& renderer) const;

private:
    void drawPanel(int x, int y, int width, int height, float alpha);

    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteBatch> batch_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont>  font_;
    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Texture2D>   panel_;

    /// A short history of frame times, so the number shown is not the last
    /// frame's noise and a spike is visible as a spike.
    std::vector<float> frameHistory_;
    std::size_t        historyIndex_ = 0;
    float              smoothedMs_ = 16.0f;
    bool               ready_ = false;
};

}  // namespace CnaStreet
