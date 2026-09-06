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
#include <utility>
#include <vector>

namespace Microsoft::Xna::Framework {
    class GraphicsDeviceManager;
}

namespace Microsoft::Xna::Framework::Content {
    class ContentManager;
}

namespace CnaStreet {

class CityScene;
class DebugOverlay;
class MaterialLibrary;
class ModelLibrary;
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
    /// Drives the walking camera along a scripted route with collision on,
    /// and reports what it met. The point is that a street has to *behave*,
    /// not only photograph: a car has to be solid, a car that drives into
    /// the camera has to give it back, and a moving car has to face the way
    /// it is going. All three are things a still cannot show, and all three
    /// were wrong.
    void runWalkthrough(float deltaSeconds);
    /// One leg of that route.
    struct WalkLeg
    {
        std::string name;
        Microsoft::Xna::Framework::Vector3 from{0.0f, 0.0f, 0.0f};
        Microsoft::Xna::Framework::Vector3 towards{0.0f, 0.0f, 0.0f};
        float yaw = 0.0f;
        float seconds = 4.0f;
        float pace = 1.4f;
        std::string expectation;
        // --- what happened -------------------------------------------------
        float wanted = 0.0f;      ///< metres the walker asked to move
        float travelled = 0.0f;   ///< metres it actually moved
        float closest = 1e9f;     ///< nearest it came to a vehicle's skin
        int   blocked = 0;        ///< steps where it moved less than it asked
        int   inside = 0;         ///< steps where it was inside a vehicle
        float pushed = 0.0f;      ///< metres a vehicle pushed it out of itself
    };
    void buildWalkthrough();
    void handleHotkeys(const Microsoft::Xna::Framework::Input::KeyboardState& keyboard,
                       const Microsoft::Xna::Framework::Input::KeyboardState& previous);

    std::unique_ptr<Microsoft::Xna::Framework::GraphicsDeviceManager> graphics_;
    std::unique_ptr<MaterialLibrary> materials_;
    std::unique_ptr<ModelLibrary>    models_;
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

    /// Frame times gathered over a `--frames` run and reported on the way out.
    ///
    /// The overlay shows a smoothed headline and one frame's breakdown, which is
    /// the right thing to look at while flying the camera and the wrong thing to
    /// tune against: the first frames of a run are warm-up, and a single frame's
    /// stage times are dominated by whatever the driver happened to be doing.
    /// A profile wants the median and the tail over a settled run, so that is
    /// what this collects.
    struct FrameProfile
    {
        std::vector<float> frameMs;
        double cullMs = 0.0, shadowMs = 0.0, prepassMs = 0.0;
        double skyMs = 0.0, opaqueMs = 0.0, postMs = 0.0;
        long long draws = 0, shadowDraws = 0, triangles = 0;
        int samples = 0;

        /// The GPU's own answers, summed over the frames that carried one.
        /// Counted separately because a timer result lands a frame or two
        /// after the range closed, so the first settled frames have none and
        /// dividing by `samples` would understate every stage.
        double gpuShadowMs = 0.0, gpuPrepassMs = 0.0, gpuSkyMs = 0.0;
        double gpuOpaqueMs = 0.0, gpuPostMs = 0.0;
        int gpuSamples = 0;
        std::vector<std::pair<std::string, double>> postPassMs;

        /// Per-cascade shadow work, summed over the settled frames.
        struct Cascade { double draws = 0.0, triangles = 0.0, radius = 0.0; float split = 0.0f; };
        std::vector<Cascade> cascades;

        long long vehicleDraws = 0, driverDraws = 0, skinnedDraws = 0;
        long long characterShadowDraws = 0;
        long long vehicleTriangles = 0, characterTriangles = 0;
    };
    FrameProfile profile_;
    /// How many frames to discard before measuring. Three is what the screenshot
    /// path already treats as settled.
    static constexpr int kProfileWarmup = 6;
    void recordFrame();
    void reportProfile();

    /// `--capture DIR` renders every named viewpoint into DIR and exits. This is
    /// the mechanism behind the screenshot set in the README and the visual
    /// regression views.
    std::string captureDirectory_;
    std::string walkDirectory_;
    std::vector<WalkLeg> walkRoute_;
    int   walkLeg_ = -1;
    float walkTime_ = 0.0f;
    int   walkSettle_ = 0;
    /// Seconds left of a side-step round something the walker has run into.
    float walkSideStep_ = 0.0f;
    /// The worst disagreement seen between a moving vehicle's drawn heading
    /// and the direction it is actually travelling, in degrees, and which
    /// model it was.
    float walkWorstHeading_ = 0.0f;
    std::string walkWorstVehicle_;
    int   walkHeadingSamples_ = 0;
    int         captureIndex_    = 0;
    int         captureSettle_   = 0;
    std::string screenshotPath_;
    /// `--supersample N` renders a still at N times the requested size and
    /// box-filters it down on the way to the file: the one anti-aliasing
    /// this application can add on its own side of the framework, and the
    /// only one that settles leaf cards, wheel spokes and shutter rails
    /// smaller than a pixel. Stills only; the window is not shown at it.
    int         supersample_     = 1;
    std::string shadowDumpPath_;
    std::string probeDumpPath_;
    /// The sun moved this session and the reflection probes have not followed
    /// it yet. Re-baked on the first frame the sun holds still.
    bool probesStale_ = false;
    std::string settingsPath_;
    /// Where `--content` points, or empty for `<assets>/content`.
    std::string contentDirectory_;
    std::unique_ptr<Microsoft::Xna::Framework::Content::ContentManager> content_;
    int         startViewpoint_  = 0;
    /// `--camera x,y,z,yaw,pitch` overrides the starting viewpoint. Not a
    /// feature so much as a tool: every visual defect in this project was
    /// found by pointing a camera at it from a place the viewpoint list does
    /// not contain, and doing that by editing the viewpoint table and
    /// rebuilding is a five-minute round trip instead of a five-second one.
    bool        cameraOverride_  = false;
    Viewpoint   cameraOverrideAt_{};
};

}  // namespace CnaStreet
