// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Render/Camera.hpp"
#include "CnaStreet/Render/CameraController.hpp"
#include "CnaStreet/Render/RenderSettings.hpp"

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework {
    class GraphicsDeviceManager;
}

namespace CnaStreet {

class CityScene;
class DebugOverlay;
class MaterialLibrary;
class SceneRenderer;

/**
 * @brief The application: owns the window, the device and the city.
 *
 * A thin CNA @c Game subclass. Everything that is not lifecycle plumbing lives
 * in the systems it owns, so the interesting code stays testable without a
 * graphics device.
 */
class StreetApplication : public Microsoft::Xna::Framework::Game
{
public:
    StreetApplication();
    ~StreetApplication() override;

    /// Applies command-line options. Returns false when the arguments ask for
    /// something the program cannot do, having already explained why.
    bool configure(int argc, char** argv);
    [[nodiscard]] const RenderSettings& settings() const { return settings_; }

protected:
    void Initialize() override;
    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

private:
    void loadSettingsFile();
    void captureScreenshot(const std::string& path);
    void runCaptureScript();
    void handleHotkeys(const Microsoft::Xna::Framework::Input::KeyboardState& keyboard,
                       const Microsoft::Xna::Framework::Input::KeyboardState& previous);

    std::unique_ptr<Microsoft::Xna::Framework::GraphicsDeviceManager> graphics_;
    std::unique_ptr<MaterialLibrary> materials_;
    std::unique_ptr<SceneRenderer> renderer_;
    std::unique_ptr<CityScene>     scene_;
    std::unique_ptr<DebugOverlay>  overlay_;

    RenderSettings   settings_;
    Camera           camera_;
    CameraController controller_;

    Microsoft::Xna::Framework::Input::KeyboardState previousKeyboard_;
    Microsoft::Xna::Framework::Input::MouseState    previousMouse_;

    float elapsedSeconds_ = 0.0f;
    int   framesDrawn_    = 0;
    int   frameBudget_    = 0;
    bool  contentLoaded_  = false;

    /// `--capture DIR` renders every named viewpoint into DIR and exits. This is
    /// the mechanism behind the screenshot set in the README and the visual
    /// regression views.
    std::string captureDirectory_;
    int         captureIndex_    = 0;
    int         captureSettle_   = 0;
    std::string screenshotPath_;
    std::string settingsPath_;
    int         startViewpoint_  = 0;
};

}  // namespace CnaStreet
