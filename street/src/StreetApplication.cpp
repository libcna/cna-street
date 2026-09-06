// SPDX-License-Identifier: MIT
#include "CnaStreet/StreetApplication.hpp"

#include "CnaStreet/Render/DebugOverlay.hpp"
#include "CnaStreet/Assets/ModelLibrary.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "CnaStreet/Render/SceneRenderer.hpp"
#include "CnaStreet/Scene/CityScene.hpp"
#include "CnaStreet/Scene/StreetMetrics.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::KeyboardState;
using Microsoft::Xna::Framework::Input::Keys;
using Microsoft::Xna::Framework::Input::Mouse;

namespace CnaStreet {

namespace M = Metrics;

namespace {

bool Pressed(const KeyboardState& now, const KeyboardState& before, Keys key)
{
    return now.IsKeyDown(key) && before.IsKeyUp(key);
}

std::string SanitiseFileName(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        if (std::isalnum(static_cast<unsigned char>(c))) out.push_back(
            static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        else if (!out.empty() && out.back() != '-') out.push_back('-');
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "view" : out;
}

}  // namespace

StreetApplication::StreetApplication()
    : graphics_(std::make_unique<GraphicsDeviceManager>(this))
{
    settings_.applyPreset(QualityPreset::High);
}

StreetApplication::~StreetApplication() = default;

bool StreetApplication::configure(int argc, char** argv)
{
    auto next = [&](int& i) -> const char* {
        return (i + 1 < argc) ? argv[++i] : nullptr;
    };

    // The settings file is read *before* the options, not after, so that the
    // command line wins. It used to be loaded at the end, which meant a settings
    // document silently overrode every flag on the command line -- `--preset low`
    // ran at high quality and there was nothing on screen to say why.
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--settings") settingsPath_ = argv[i + 1];
    loadSettingsFile();

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            std::printf(
                "cna-street " CNA_STREET_VERSION " -- a realistic city street built with CNA\n\n"
                "Usage: cna-street [options]\n\n"
                "  --preset <low|medium|high|ultra>  quality preset (default: high)\n"
                "  --settings <file.json>            load a settings document\n"
                "  --content <dir>                   load compiled assets from dir\n"
                "  --width <n> --height <n>          window size\n"
                "  --seed <n>                        procedural seed (default: %u)\n"
                "  --viewpoint <n>                   start at named viewpoint n (1-based)\n"
                "  --camera x,y,z,yaw,pitch          start at an explicit camera (radians)\n"
                "  --walkthrough <dir>               walk the camera through the street with\n"
                "                                    collision on, write a frame per leg and\n"
                "                                    report what it met\n"
                "  --lineup                          park one of every vehicle in a row, and\n"
                "                                    add a side and a front viewpoint for each\n"
                "  --frames <n>                      render n frames and exit\n"
                "  --screenshot <file.png>           write one frame and exit\n"
                "  --capture <dir>                   write every viewpoint into dir and exit\n"
                "  --supersample <n>                 render stills at n times the size and\n"
                "                                    filter them down (1-4); stills only\n"
                "  --exposure <v>                    exposure multiplier\n"
                "  --shadow-debug                    tint each shadow cascade\n"
                "  --no-shadows --no-bloom --no-ssao --no-fog --no-clouds --no-ibl\n"
                "  --no-probes                       sky-only reflections, no local probes\n"
                "  --dump-probes <dir>               write each reflection probe as a face strip\n"
                "  --no-traffic --no-pedestrians --no-vegetation --no-overlay\n"
                "  --sun <elevation> <azimuth>       sun position in degrees\n"
                "  --night                           civil twilight, street lights on\n"
                "  --dump-settings                   print the settings JSON and exit\n"
                "  --help                            this text\n",
                settings_.seed);
            return false;
        }
        else if (arg == "--preset")
        {
            const char* value = next(i);
            if (value == nullptr) { std::fprintf(stderr, "--preset needs a value\n"); return false; }
            const std::string name = value;
            if (name == "low")         settings_.applyPreset(QualityPreset::Low);
            else if (name == "medium") settings_.applyPreset(QualityPreset::Medium);
            else if (name == "high")   settings_.applyPreset(QualityPreset::High);
            else if (name == "ultra")  settings_.applyPreset(QualityPreset::Ultra);
            else
            {
                std::fprintf(stderr, "unknown preset '%s' (low, medium, high, ultra)\n",
                             name.c_str());
                return false;
            }
        }
        else if (arg == "--settings")   { const char* v = next(i); if (v) settingsPath_ = v; }
        else if (arg == "--content")    { const char* v = next(i); if (v) contentDirectory_ = v; }
        else if (arg == "--width")      { const char* v = next(i); if (v) settings_.windowWidth = std::atoi(v); }
        else if (arg == "--height")     { const char* v = next(i); if (v) settings_.windowHeight = std::atoi(v); }
        else if (arg == "--seed")       { const char* v = next(i); if (v) settings_.seed = static_cast<std::uint32_t>(std::strtoul(v, nullptr, 10)); }
        else if (arg == "--viewpoint")  { const char* v = next(i); if (v) startViewpoint_ = std::atoi(v) - 1; }
        else if (arg == "--camera")
        {
            const char* v = next(i);
            float p[5] = {0.0f, 1.80f, 0.0f, 0.0f, 0.0f};
            if (v == nullptr
                || std::sscanf(v, "%f,%f,%f,%f,%f", &p[0], &p[1], &p[2], &p[3], &p[4]) != 5)
            {
                std::fprintf(stderr, "--camera needs x,y,z,yaw,pitch (radians)\n");
                return false;
            }
            cameraOverride_ = true;
            cameraOverrideAt_ = Viewpoint{"Command line", Vector3(p[0], p[1], p[2]), p[3], p[4],
                                          settings_.verticalFovDegrees * 0.0174532925f};
        }
        else if (arg == "--frames")     { const char* v = next(i); if (v) frameBudget_ = std::atoi(v); }
        else if (arg == "--screenshot") { const char* v = next(i); if (v) screenshotPath_ = v; }
        else if (arg == "--supersample") { const char* v = next(i); if (v) supersample_ = std::clamp(std::atoi(v), 1, 4); }
        else if (arg == "--capture")    { const char* v = next(i); if (v) captureDirectory_ = v; }
        else if (arg == "--walkthrough") { const char* v = next(i); if (v) walkDirectory_ = v; }
        else if (arg == "--exposure")  { const char* v = next(i); if (v) settings_.exposure = static_cast<float>(std::atof(v)); }
        else if (arg == "--no-ibl")         settings_.imageBasedLighting = false;
        else if (arg == "--shadow-debug")   settings_.shadowDebugTint = true;
        else if (arg == "--shadow-distance") { const char* v = next(i); if (v) settings_.shadowDistance = static_cast<float>(std::atof(v)); }
        else if (arg == "--cascades")      { const char* v = next(i); if (v) settings_.shadowCascades = std::atoi(v); }
        else if (arg == "--shadow-bias")   { const char* v = next(i); if (v) settings_.shadowDepthBias = static_cast<float>(std::atof(v)); }
        else if (arg == "--dump-shadow")  { const char* v = next(i); if (v) shadowDumpPath_ = v; }
        else if (arg == "--dump-probes")  { const char* v = next(i); if (v) probeDumpPath_ = v; }
        else if (arg == "--no-probes")      settings_.reflectionProbes = false;
        else if (arg == "--no-shadows")     settings_.shadows = false;
        else if (arg == "--no-bloom")       settings_.bloom = false;
        else if (arg == "--no-ssao")        settings_.ssao = false;
        else if (arg == "--no-fog")         settings_.heightFog = false;
        else if (arg == "--no-clouds")      settings_.clouds = false;
        else if (arg == "--no-traffic")     settings_.traffic = false;
        else if (arg == "--no-pedestrians") settings_.pedestrians = false;
        else if (arg == "--no-vegetation")  settings_.vegetation = false;
        else if (arg == "--lineup") { settings_.vehicleLineup = true; }
        else if (arg == "--no-overlay")     settings_.debugOverlay = false;
        else if (arg == "--vsync")          settings_.vsync = true;
        else if (arg == "--no-vsync")       settings_.vsync = false;
        else if (arg == "--sun")
        {
            const char* elevation = next(i);
            const char* azimuth   = next(i);
            if (elevation == nullptr || azimuth == nullptr)
            {
                std::fprintf(stderr, "--sun needs an elevation and an azimuth in degrees\n");
                return false;
            }
            settings_.sunElevationDegrees = static_cast<float>(std::atof(elevation));
            settings_.sunAzimuthDegrees   = static_cast<float>(std::atof(azimuth));
        }
        else if (arg == "--night")
        {
            // One flag rather than three, because the three go together. The
            // sun is 4 degrees below the horizon -- civil twilight, when a
            // street is lit by its own lamps and the sky is still a colour
            // rather than black -- and the exposure follows, because a scene
            // lit at a hundredth of the irradiance needs the aperture opened
            // and no amount of tone mapping substitutes for that. Everything
            // else that changes at night follows from the sun's elevation
            // through `RenderSettings::nightLighting`.
            settings_.sunElevationDegrees = -4.0f;
            // 1.5 against the daylight 0.42: a little under two stops. The sky
            // at civil twilight carries almost nothing -- its ambient is
            // (0.008, 0.001, 0.000) -- so what the frame is actually exposed
            // for is the lamps, the shop windows and the flats above them, and
            // opening up far enough to lift the *pavement* to mid grey turns a
            // night street into an overcast afternoon with the lights on. It
            // was 1.0 when the ambient came from the sky cube; the reflection
            // probes now light the street from what it actually sees at night,
            // which is darker than an open sky, and the half stop follows.
            settings_.exposure            = 1.5f;
            settings_.bloomThreshold      = 0.72f;
            settings_.bloomIntensity      = 0.55f;
        }
        else if (arg == "--dump-settings")
        {
            std::fputs(settings_.toJson().c_str(), stdout);
            return false;
        }
        else
        {
            std::fprintf(stderr, "cna-street: unknown option '%s' (try --help)\n", arg.c_str());
            return false;
        }
    }

    if (!captureDirectory_.empty() || !walkDirectory_.empty())
    {
        // A capture run has no user to look at an overlay, and the overlay would
        // be baked into every screenshot.
        settings_.debugOverlay = false;
    }
    return true;
}

void StreetApplication::loadSettingsFile()
{
    std::string path = settingsPath_;
    if (path.empty())
    {
        const std::filesystem::path candidate =
            std::filesystem::path(CNA_STREET_DEFAULT_ASSET_DIR) / "config" / "render.json";
        if (std::filesystem::exists(candidate)) path = candidate.string();
    }
    if (path.empty()) return;

    std::ifstream file(path);
    if (!file)
    {
        CNA::Logger::Warn("cna-street: could not open settings file '" + path + "'");
        return;
    }
    std::ostringstream contents;
    contents << file.rdbuf();

    std::string error;
    const int applied = settings_.applyJson(contents.str(), error);
    if (applied < 0)
        CNA::Logger::Error("cna-street: settings file '" + path + "' " + error);
    else
        CNA::Logger::Info("cna-street: applied " + std::to_string(applied) + " settings from '"
                          + path + "'");
}

void StreetApplication::Initialize()
{
    // A supersampled still renders into a back buffer N times the size asked
    // for; the file is filtered back down when it is written. Only when a
    // still was asked for: an interactive window at four times its size is
    // not what anyone meant.
    if (supersample_ > 1 && screenshotPath_.empty() && captureDirectory_.empty()) supersample_ = 1;
    graphics_->setPreferredBackBufferWidthProperty(settings_.windowWidth * supersample_);
    graphics_->setPreferredBackBufferHeightProperty(settings_.windowHeight * supersample_);
    graphics_->setSynchronizeWithVerticalRetraceProperty(settings_.vsync);
    graphics_->setPreferMultiSamplingProperty(settings_.multiSample > 0);
    graphics_->ApplyChanges();

    getWindowProperty().setTitleProperty("cna-street -- a city street built with CNA");
    setIsMouseVisibleProperty(true);
    setIsFixedTimeStepProperty(false);

    Game::Initialize();
}

void StreetApplication::LoadContent()
{
    Game::LoadContent();
    if (contentLoaded_) return;
    contentLoaded_ = true;

    GraphicsDevice& device = getGraphicsDeviceProperty();
    const auto& viewport = device.getViewportProperty();
    const int width  = viewport.getWidthProperty();
    const int height = viewport.getHeightProperty();

    materials_ = std::make_unique<MaterialLibrary>(&device);

    // If a content build has been run, load the surfaces from it instead of
    // generating them. A missing content root is not an error and not a warning:
    // it is how the demo runs out of a fresh clone, and it costs start-up time
    // rather than anything visible.
    const std::filesystem::path contentRoot =
        contentDirectory_.empty()
            ? std::filesystem::path(CNA_STREET_DEFAULT_ASSET_DIR) / "content"
            : std::filesystem::path(contentDirectory_);
    if (std::filesystem::is_directory(contentRoot)
        && !std::filesystem::is_empty(contentRoot))
    {
        content_ = std::make_unique<Microsoft::Xna::Framework::Content::ContentManager>(
            nullptr, contentRoot.string());
        content_->setGraphicsDevice(device);
        materials_->setContentSource(content_.get());
        CNA::Logger::Info("cna-street: content root " + contentRoot.string());
    }

    models_ = std::make_unique<ModelLibrary>(device, *materials_);
    models_->setContentSource(content_.get());

    renderer_  = std::make_unique<SceneRenderer>(device, *materials_);
    renderer_->resize(width, height);
    renderer_->initialise(settings_);
    // The shadow-by-name breakdown costs a hash-map insert per shadow draw
    // call, which a `--frames` profiling run should pay for and an ordinary
    // one flying the camera should not.
    renderer_->setShadowReportEnabled(frameBudget_ > 0);

    // The overlay before the scene, not after: it owns the font and the sprite
    // batch the loading screen draws with, and the loading screen is the whole
    // point of building it early. Twenty seconds of scene generation and seven
    // of probe capture used to happen behind a black window, which from
    // outside is a program that has hung.
    overlay_ = std::make_unique<DebugOverlay>(device);
    overlay_->build();

    scene_ = std::make_unique<CityScene>(device, *renderer_, *materials_, *models_);
    if (content_ != nullptr) scene_->setContentRoot(contentRoot.string());
    if (screenshotPath_.empty() && captureDirectory_.empty() && walkDirectory_.empty())
    {
        // Only when there is a window somebody is looking at. A capture run
        // presents nothing and a present per stage would just cost it time.
        float stageAt = 0.0f;
        scene_->setProgressReporter([this, &stageAt, width, height](const std::string& what,
                                                                   float fraction) {
            stageAt = fraction;
            overlay_->drawLoading(what, fraction, width, height);
        });
        renderer_->setBakeProgress([this, &stageAt, width, height](float fraction) {
            // The bake is the last four per cent of the bar and seven seconds
            // of the wall clock, so it gets its own sweep through them.
            overlay_->drawLoading("capturing reflection probes",
                                  stageAt + (1.0f - stageAt) * fraction, width, height);
        });
    }
    scene_->build(settings_);
    scene_->setProgressReporter(nullptr);
    renderer_->setBakeProgress(nullptr);

    camera_.setPerspective(MathHelper::ToRadians(settings_.verticalFovDegrees),
                           static_cast<float>(width) / static_cast<float>(std::max(1, height)),
                           settings_.nearPlane, settings_.farPlane);

    controller_.setCamera(&camera_);
    controller_.setMoveSpeed(settings_.moveSpeed);
    controller_.setMouseSensitivity(settings_.mouseSensitivity);
    controller_.setInvertY(settings_.invertY);
    controller_.setViewpoints(scene_->viewpoints());
    controller_.setGroundProbe([this](float x, float z) { return scene_->groundHeight(x, z); });
    controller_.setCollisionProbe([this](const Vector3& point) { return scene_->isSolid(point); });
    controller_.setEscapeProbe(
        [this](const Vector3& point) { return scene_->pushOutOfSolids(point); });

    const std::vector<Viewpoint>& viewpoints = scene_->viewpoints();
    if (!viewpoints.empty())
    {
        const int index = std::clamp(startViewpoint_, 0, static_cast<int>(viewpoints.size()) - 1);
        controller_.setHome(viewpoints[static_cast<std::size_t>(index)]);
    }
    if (cameraOverride_) controller_.setHome(cameraOverrideAt_);
    controller_.setCinematicPath(viewpoints, 8.0f);
}

void StreetApplication::handleHotkeys(const KeyboardState& keyboard, const KeyboardState& previous)
{
    if (Pressed(keyboard, previous, Keys::F1)) settings_.debugOverlay = !settings_.debugOverlay;
    if (Pressed(keyboard, previous, Keys::F2))
    {
        settings_.shadows = !settings_.shadows;
        renderer_->applySettings(settings_);
    }
    if (Pressed(keyboard, previous, Keys::F3))
    {
        settings_.ssao = !settings_.ssao;
        renderer_->applySettings(settings_);
    }
    if (Pressed(keyboard, previous, Keys::F4))
    {
        settings_.bloom = !settings_.bloom;
        renderer_->applySettings(settings_);
    }
    if (Pressed(keyboard, previous, Keys::F5))
    {
        settings_.heightFog = !settings_.heightFog;
        renderer_->applySettings(settings_);
    }
    if (Pressed(keyboard, previous, Keys::F6))
    {
        settings_.clouds = !settings_.clouds;
        renderer_->sky().updateSun(settings_);
        probesStale_ = true;
    }
    if (Pressed(keyboard, previous, Keys::F9))
    {
        captureScreenshot("cna-street-" + std::to_string(framesDrawn_) + ".png");
    }
    // The sun can be walked round the sky, which is the fastest way to judge
    // whether the lighting is holding up.
    const float step = keyboard.IsKeyDown(Keys::LeftShift) ? 4.0f : 1.0f;
    bool sunMoved = false;
    if (keyboard.IsKeyDown(Keys::OemOpenBrackets))
    {
        settings_.sunAzimuthDegrees -= step;
        sunMoved = true;
    }
    if (keyboard.IsKeyDown(Keys::OemCloseBrackets))
    {
        settings_.sunAzimuthDegrees += step;
        sunMoved = true;
    }
    if (keyboard.IsKeyDown(Keys::OemMinus))
    {
        settings_.sunElevationDegrees = std::max(2.0f, settings_.sunElevationDegrees - step);
        sunMoved = true;
    }
    if (keyboard.IsKeyDown(Keys::OemPlus))
    {
        settings_.sunElevationDegrees = std::min(86.0f, settings_.sunElevationDegrees + step);
        sunMoved = true;
    }
    if (sunMoved) renderer_->sky().updateSun(settings_);
    // The probes are pictures of the street under the sun that was; they follow
    // it when it stops moving, not while it is being dragged.
    if (sunMoved) probesStale_ = true;
    else if (probesStale_)
    {
        probesStale_ = false;
        renderer_->rebakeReflectionProbes(settings_);
    }
}

void StreetApplication::Update(GameTime& gameTime)
{
    Game::Update(gameTime);

    // A capture or a one-shot screenshot advances the clock by a fixed step
    // rather than by however long the last frame took. Without it the sky's
    // clouds and the traffic are wherever wall-clock time left them, two runs of
    // --capture produce two different pictures, and the screenshot comparison
    // that scripts/check-screenshots.sh is built on means nothing: a quarter of
    // the pixels in a view of the sky differed between runs of an unchanged
    // build.
    const bool deterministic = !captureDirectory_.empty() || !screenshotPath_.empty()
                               || !walkDirectory_.empty();
    const float dt = deterministic
                         ? 1.0f / 60.0f
                         : static_cast<float>(
                               gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());
    elapsedSeconds_ += dt;

    const KeyboardState keyboard = Keyboard::GetState();
    const auto mouse = Mouse::GetState();

    if (keyboard.IsKeyDown(Keys::Escape) && previousKeyboard_.IsKeyDown(Keys::Escape)
        && !controller_.isMouseCaptured() && keyboard.IsKeyDown(Keys::LeftShift))
    {
        Exit();
    }
    if (Pressed(keyboard, previousKeyboard_, Keys::Q) && keyboard.IsKeyDown(Keys::LeftControl))
        Exit();

    handleHotkeys(keyboard, previousKeyboard_);

    if (captureDirectory_.empty() && walkDirectory_.empty())
        controller_.update(dt, keyboard, previousKeyboard_, mouse, previousMouse_);

    if (scene_ != nullptr) scene_->update(dt, settings_);
    if (!walkDirectory_.empty()) runWalkthrough(dt);

    previousKeyboard_ = keyboard;
    previousMouse_    = mouse;
}

void StreetApplication::Draw(const GameTime& gameTime)
{
    (void)gameTime;
    GraphicsDevice& device = getGraphicsDeviceProperty();

    if (renderer_ == nullptr || scene_ == nullptr)
    {
        device.Clear(Color::Black);
        return;
    }

    if (!captureDirectory_.empty()) runCaptureScript();
    if (!walkDirectory_.empty() && walkSettle_ > 0 && --walkSettle_ == 0
        && walkLeg_ > 0 && walkLeg_ <= static_cast<int>(walkRoute_.size()))
    {
        std::filesystem::create_directories(walkDirectory_);
        char index[16];
        std::snprintf(index, sizeof(index), "%02d", walkLeg_);
        screenshotPath_ = (std::filesystem::path(walkDirectory_)
                           / (std::string(index) + "-"
                              + SanitiseFileName(walkRoute_[static_cast<std::size_t>(walkLeg_ - 1)]
                                                     .name)
                              + ".png")).string();
    }

    renderer_->beginFrame();
    scene_->submit(settings_, camera_.position());
    renderer_->render(camera_, settings_, elapsedSeconds_);

    if (settings_.debugOverlay && overlay_ != nullptr)
        overlay_->draw(*renderer_, *scene_, camera_, controller_, settings_, gameTime);

    ++framesDrawn_;
    recordFrame();

    if (!shadowDumpPath_.empty() && framesDrawn_ >= 3)
    {
        renderer_->dumpShadowAtlas(shadowDumpPath_);
        shadowDumpPath_.clear();
    }
    if (!probeDumpPath_.empty())
    {
        renderer_->dumpReflectionProbes(probeDumpPath_);
        probeDumpPath_.clear();
    }
    if (!screenshotPath_.empty() && framesDrawn_ >= 3)
    {
        captureScreenshot(screenshotPath_);
        screenshotPath_.clear();
        // A one-shot --screenshot run is finished here; a --capture or a
        // --walkthrough run has more to do and ends when its script says so.
        if (frameBudget_ == 0 && captureDirectory_.empty() && walkDirectory_.empty()) Exit();
    }
    if (frameBudget_ > 0 && framesDrawn_ >= frameBudget_)
    {
        reportProfile();
        Exit();
    }
}

void StreetApplication::recordFrame()
{
    if (frameBudget_ <= 0 || framesDrawn_ <= kProfileWarmup) return;
    const SceneRenderer::Stats& stats = renderer_->stats();
    profile_.frameMs.push_back(stats.frameMs);
    profile_.cullMs    += static_cast<double>(stats.cullMs);
    profile_.shadowMs  += static_cast<double>(stats.shadowMs);
    profile_.prepassMs += static_cast<double>(stats.prepassMs);
    profile_.skyMs     += static_cast<double>(stats.skyMs);
    profile_.opaqueMs  += static_cast<double>(stats.opaqueMs);
    profile_.postMs    += static_cast<double>(stats.postMs);
    profile_.draws       += stats.drawCalls;
    profile_.shadowDraws += stats.shadowDrawCalls;
    profile_.triangles   += static_cast<long long>(stats.triangles);
    ++profile_.samples;

    profile_.vehicleDraws         += stats.vehicleDrawCalls;
    profile_.driverDraws          += stats.driverDrawCalls;
    profile_.skinnedDraws         += stats.skinnedDrawCalls;
    profile_.characterShadowDraws += stats.characterShadowDrawCalls;
    profile_.vehicleTriangles     += static_cast<long long>(stats.vehicleTriangles);
    profile_.characterTriangles   += static_cast<long long>(stats.characterTriangles);

    if (profile_.cascades.size() < stats.cascades.size())
        profile_.cascades.resize(stats.cascades.size());
    for (std::size_t i = 0; i < stats.cascades.size(); ++i)
    {
        profile_.cascades[i].draws     += static_cast<double>(stats.cascades[i].draws);
        profile_.cascades[i].triangles += static_cast<double>(stats.cascades[i].triangles);
        profile_.cascades[i].radius    += static_cast<double>(stats.cascades[i].radius);
        profile_.cascades[i].split      = stats.cascades[i].split;
    }

    // Only a frame that actually carried a GPU result counts toward the GPU
    // averages: a query lands a frame or two after its range closed, so the
    // first settled frames report -1 and averaging those in would make every
    // stage look cheaper than it is.
    if (stats.gpuOpaqueMs >= 0.0)
    {
        profile_.gpuShadowMs  += std::max(stats.gpuShadowMs, 0.0);
        profile_.gpuPrepassMs += std::max(stats.gpuPrepassMs, 0.0);
        profile_.gpuSkyMs     += std::max(stats.gpuSkyMs, 0.0);
        profile_.gpuOpaqueMs  += stats.gpuOpaqueMs;
        profile_.gpuPostMs    += std::max(stats.gpuPostMs, 0.0);
        ++profile_.gpuSamples;
        if (profile_.postPassMs.size() < stats.gpuPostPasses.size())
            profile_.postPassMs.resize(stats.gpuPostPasses.size());
        for (std::size_t i = 0; i < stats.gpuPostPasses.size(); ++i)
        {
            profile_.postPassMs[i].first = stats.gpuPostPasses[i].first;
            profile_.postPassMs[i].second += stats.gpuPostPasses[i].second;
        }
    }
}

void StreetApplication::reportProfile()
{
    if (profile_.samples <= 0) return;
    std::vector<float> sorted = profile_.frameMs;
    std::sort(sorted.begin(), sorted.end());
    const auto at = [&](double q) {
        const std::size_t i = static_cast<std::size_t>(
            q * static_cast<double>(sorted.size() - 1) + 0.5);
        return sorted[i];
    };
    const double n = static_cast<double>(profile_.samples);
    double mean = 0.0;
    for (const float ms : sorted) mean += static_cast<double>(ms);
    mean /= n;

    const auto fixed = [](double value, int places) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(places) << value;
        return out.str();
    };

    CNA::Logger::Info("cna-street: frame profile over " + std::to_string(profile_.samples)
                      + " settled frames at "
                      + std::to_string(getGraphicsDeviceProperty().getViewportProperty().getWidthProperty())
                      + "x"
                      + std::to_string(getGraphicsDeviceProperty().getViewportProperty().getHeightProperty()));
    CNA::Logger::Info("cna-street:   mean " + fixed(mean, 2) + " ms ("
                      + fixed(1000.0 / std::max(mean, 0.001), 1) + " fps)"
                      + "  median " + fixed(at(0.5), 2)
                      + "  p95 " + fixed(at(0.95), 2)
                      + "  min " + fixed(sorted.front(), 2)
                      + "  max " + fixed(sorted.back(), 2) + " ms");
    CNA::Logger::Info("cna-street:   cull " + fixed(profile_.cullMs / n, 2)
                      + "  shadow " + fixed(profile_.shadowMs / n, 2)
                      + "  prepass " + fixed(profile_.prepassMs / n, 2)
                      + "  sky " + fixed(profile_.skyMs / n, 2)
                      + "  opaque " + fixed(profile_.opaqueMs / n, 2)
                      + "  post " + fixed(profile_.postMs / n, 2) + " ms");
    CNA::Logger::Info("cna-street:   "
                      + std::to_string(profile_.draws / profile_.samples) + " draws, "
                      + std::to_string(profile_.shadowDraws / profile_.samples) + " shadow draws, "
                      + std::to_string(profile_.triangles / profile_.samples)
                      + " triangles per frame");

    // The other clock. A CPU stage time is how long the driver took to accept
    // the work; a GPU stage time is how long the hardware took to do it. Which
    // of the two dominates decides what to optimise, and until this pass this
    // project only had the first.
    if (profile_.gpuSamples > 0)
    {
        const double g = static_cast<double>(profile_.gpuSamples);
        const double gpuTotal = (profile_.gpuShadowMs + profile_.gpuPrepassMs + profile_.gpuSkyMs
                                 + profile_.gpuOpaqueMs + profile_.gpuPostMs) / g;
        CNA::Logger::Info("cna-street:   GPU  shadow " + fixed(profile_.gpuShadowMs / g, 2)
                          + "  prepass " + fixed(profile_.gpuPrepassMs / g, 2)
                          + "  sky " + fixed(profile_.gpuSkyMs / g, 2)
                          + "  opaque " + fixed(profile_.gpuOpaqueMs / g, 2)
                          + "  post " + fixed(profile_.gpuPostMs / g, 2)
                          + "  = " + fixed(gpuTotal, 2) + " ms of a "
                          + fixed(mean, 2) + " ms frame ("
                          + fixed(100.0 * gpuTotal / std::max(mean, 0.001), 0) + "%)");
        if (!profile_.postPassMs.empty())
        {
            std::ostringstream passes;
            passes << "cna-street:   GPU post passes ";
            for (const auto& pass : profile_.postPassMs)
                passes << pass.first << " " << fixed(pass.second / g, 2) << "  ";
            CNA::Logger::Info(passes.str());
        }
    }
    else
    {
        CNA::Logger::Info("cna-street:   GPU timing unavailable on this renderer");
    }

    if (!profile_.cascades.empty())
    {
        CNA::Logger::Info("cna-street:   shadow cascades, per frame:");
        for (std::size_t i = 0; i < profile_.cascades.size(); ++i)
        {
            const auto& cascade = profile_.cascades[i];
            std::ostringstream line;
            line << "cna-street:     cascade " << i << "  to "
                 << std::fixed << std::setprecision(1) << cascade.split << " m, fit radius "
                 << cascade.radius / n << " m: " << std::setprecision(0)
                 << static_cast<double>(cascade.draws) / n << " draws, "
                 << static_cast<double>(cascade.triangles) / n << " triangles";
            CNA::Logger::Info(line.str());
        }
    }

    CNA::Logger::Info("cna-street:   content in frame -- vehicles "
                      + std::to_string(profile_.vehicleDraws / profile_.samples) + " draws / "
                      + std::to_string(profile_.vehicleTriangles / profile_.samples) + " tris"
                      + ", drivers " + std::to_string(profile_.driverDraws / profile_.samples)
                      + " draws"
                      + ", characters " + std::to_string(profile_.skinnedDraws / profile_.samples)
                      + " draws / "
                      + std::to_string(profile_.characterTriangles / profile_.samples) + " tris"
                      + ", character shadow proxies "
                      + std::to_string(profile_.characterShadowDraws / profile_.samples)
                      + " draws");

    // And where the scene's weight actually is. A frame time says it got
    // slower; this says which batch did it.
    const std::vector<SceneRenderer::BatchCost> all = renderer_->costReport(0);
    long long sceneTriangles = 0;
    long long shadowTriangles = 0;
    for (const SceneRenderer::BatchCost& cost : all)
    {
        sceneTriangles += cost.triangles;
        if (cost.castsShadow) shadowTriangles += cost.triangles;
    }
    CNA::Logger::Info("cna-street:   " + std::to_string(sceneTriangles) + " triangles in "
                      + std::to_string(all.size()) + " batch families, "
                      + std::to_string(shadowTriangles) + " of them shadow casting");
    const auto dump = [](const char* heading, const std::vector<SceneRenderer::BatchCost>& list) {
        CNA::Logger::Info(std::string("cna-street:   ") + heading);
        for (const SceneRenderer::BatchCost& cost : list)
        {
            std::ostringstream line;
            line << "cna-street:     " << std::setw(4) << cost.batches << " draws  "
                 << std::setw(5) << cost.copies << " copies  " << std::setw(9) << cost.triangles
                 << " tris  cull " << std::fixed << std::setprecision(0) << cost.cullDistance
                 << " m" << (cost.castsShadow ? "  casts" : "       ") << "  " << cost.name;
            CNA::Logger::Info(line.str());
        }
    };
    dump("heaviest batch families as registered, by triangle:", renderer_->costReport(14));
    dump("most expensive families in the last frame, by draw call:",
         renderer_->visibleReport(16));
    dump("the shadow pass's own triangles, by family, last frame:",
         renderer_->shadowReport(14));
}

void StreetApplication::runCaptureScript()
{
    const std::vector<Viewpoint>& viewpoints = scene_->viewpoints();
    if (viewpoints.empty()) { Exit(); return; }

    if (captureSettle_ == 0)
    {
        if (captureIndex_ >= static_cast<int>(viewpoints.size()))
        {
            CNA::Logger::Info("cna-street: capture complete");
            Exit();
            return;
        }
        controller_.applyViewpoint(viewpoints[static_cast<std::size_t>(captureIndex_)]);
        // Three frames per viewpoint: the first binds the new camera, the second
        // gives the temporal parts of the pipeline a previous frame to work
        // from, and the third is the one that gets written.
        captureSettle_ = 3;
    }

    --captureSettle_;
    if (captureSettle_ == 0)
    {
        const Viewpoint& viewpoint = viewpoints[static_cast<std::size_t>(captureIndex_)];
        std::filesystem::create_directories(captureDirectory_);
        char index[16];
        std::snprintf(index, sizeof(index), "%02d", captureIndex_ + 1);
        const std::string path = (std::filesystem::path(captureDirectory_)
                                  / (std::string(index) + "-" + SanitiseFileName(viewpoint.name)
                                     + ".png")).string();
        // The screenshot is taken *after* this frame is drawn, so schedule it
        // for the end of Draw rather than taking it here.
        screenshotPath_ = path;
        ++captureIndex_;
    }
}

void StreetApplication::buildWalkthrough()
{
    // A route through the things a still cannot check. Every leg is aimed
    // from the scene itself rather than from typed-in coordinates, so a
    // change to the layout moves the route with it.
    walkRoute_.clear();
    if (scene_ == nullptr) return;
    const TrafficSystem& traffic = scene_->traffic();
    const std::vector<TrafficSystem::Solid> solids = traffic.solids();
    const std::vector<Vehicle>& fleet = traffic.vehicles();

    const float footway = -(M::kMainCarriagewayWidth * 0.5f + M::kMainSidewalkWidth * 0.5f);
    const float kerbLane = -(M::kMainCarriagewayWidth * 0.5f - M::kParkingLaneWidth * 0.5f);
    const float travelLane = -(M::kMainCarriagewayWidth * 0.5f - M::kParkingLaneWidth
                               - M::kMainLaneWidth * 0.5f);

    // 1. Down the footway, which is the control: a walk that should not be
    //    stopped by anything.
    walkRoute_.push_back(WalkLeg{"down the footway", Vector3(footway, 0.0f, 24.0f),
                                 Vector3(footway, 0.0f, 52.0f), 0.0f, 22.0f, 1.4f,
                                 "walks the whole way"});

    // 2. Straight at the nearest parked car on this side. A car has to be a
    //    wall; before this pass the camera walked through it.
    std::size_t nearest = fleet.size();
    float best = 1e9f;
    for (std::size_t i = 0; i < fleet.size(); ++i)
    {
        if (!fleet[i].parked) continue;
        if (solids[i].centre.X > 0.0f) continue;
        const float away = std::fabs(solids[i].centre.Y - 40.0f);
        if (away < best) { best = away; nearest = i; }
    }
    if (nearest < fleet.size())
    {
        const Vector2 at = solids[nearest].centre;
        walkRoute_.push_back(WalkLeg{"into a parked car",
                                     Vector3(footway, 0.0f, at.Y),
                                     Vector3(at.X, 0.0f, at.Y), 0.0f, 6.0f, 1.4f,
                                     "stopped short of the bodywork"});
        walkRoute_.push_back(WalkLeg{"along the kerb past the parked cars",
                                     Vector3(footway + 1.0f, 0.0f, at.Y - 16.0f),
                                     Vector3(footway + 1.0f, 0.0f, at.Y + 16.0f), 0.0f, 26.0f,
                                     1.4f, "walks the row without entering a car"});
    }

    // 3. Standing in the travel lane while the traffic comes. The camera
    //    cannot walk into a moving car; a moving car can drive into it, and
    //    what must not happen is that it takes the camera with it.
    walkRoute_.push_back(WalkLeg{"standing in the traffic lane",
                                 Vector3(travelLane, 0.0f, 46.0f),
                                 Vector3(travelLane, 0.0f, 24.0f), 0.0f, 26.0f, 0.0f,
                                 "pushed clear, never carried"});

    // 4. From the kerb, at the traffic. This is the frame that shows whether
    //    a moving car has somebody in it and whether it faces the way it is
    //    going -- the two things that cannot be checked from a parked one.
    walkRoute_.push_back(WalkLeg{"watching the traffic from the kerb",
                                 Vector3(footway + 1.1f, 0.0f, 34.0f),
                                 Vector3(travelLane, 0.0f, 22.0f), 0.0f, 18.0f, 0.0f,
                                 "cars face their travel, with drivers in them"});

    // 5. At the crossing while the light cycles, which is where the crowd
    //    piled into one body volume.
    // Standing back from the kerb the crossing starts at, looking at the
    // place where a queue forms: the corner where the two crossings meet.
    walkRoute_.push_back(WalkLeg{"at the crossing",
                                 Vector3(footway - 1.6f, 0.0f, 11.5f),
                                 Vector3(footway + 0.4f, 0.0f, 3.0f), 0.0f, 40.0f, 0.0f,
                                 "a queue, not a heap"});
    CNA::Logger::Info("cna-street: walkthrough -- " + std::to_string(walkRoute_.size()) + " legs");
}

void StreetApplication::runWalkthrough(float deltaSeconds)
{
    if (scene_ == nullptr) return;
    if (walkRoute_.empty())
    {
        buildWalkthrough();
        if (walkRoute_.empty()) { Exit(); return; }
        controller_.setMode(CameraMode::Walk);
        walkLeg_ = 0;
        walkTime_ = -1.0f;   // "not started yet"
    }

    // Every moving vehicle, every step: does its drawn body face the way it
    // is travelling? This is the check the backwards Mini would have failed
    // for a whole pass.
    {
        const TrafficSystem& traffic = scene_->traffic();
        const std::vector<Vehicle>& fleet = traffic.vehicles();
        for (const Vehicle& vehicle : fleet)
        {
            if (vehicle.parked || vehicle.speed < 1.0f) continue;
            const Matrix world = vehicle.transform(traffic.lanes());
            // The body's own forward, from the transform the renderer uses.
            const Vector3 nose = Vector3::TransformNormal(Vector3(0.0f, 0.0f, 1.0f), world);
            Vector2 travel(0.0f, 0.0f);
            if (vehicle.inTurn)
            {
                Vehicle later = vehicle;
                later.turnPhase = std::min(1.0f, vehicle.turnPhase + 0.02f);
                const Vector2 a = vehicle.groundPosition(traffic.lanes());
                const Vector2 b = later.groundPosition(traffic.lanes());
                travel = Vector2(b.X - a.X, b.Y - a.Y);
            }
            else
                travel = traffic.lanes()[static_cast<std::size_t>(vehicle.lane)].direction;
            const float length = std::sqrt(travel.X * travel.X + travel.Y * travel.Y);
            if (length < 1e-4f) continue;
            const float dot = (nose.X * travel.X + nose.Z * travel.Y) / length;
            const float degrees = std::acos(std::clamp(dot, -1.0f, 1.0f)) * 180.0f
                                  / MathHelper::Pi;
            ++walkHeadingSamples_;
            if (degrees > walkWorstHeading_)
            {
                walkWorstHeading_ = degrees;
                walkWorstVehicle_ = VehicleFactory::name(vehicle.type);
            }
        }
    }

    if (walkLeg_ >= static_cast<int>(walkRoute_.size()))
    {
        // The last leg's screenshot is still two frames out; let it be taken.
        if (walkSettle_ > 0 || !screenshotPath_.empty()) return;
        // The report, and out.
        CNA::Logger::Info("cna-street: walkthrough results");
        for (const WalkLeg& leg : walkRoute_)
            CNA::Logger::Info(
                "cna-street:   " + leg.name + " -- asked " + std::to_string(leg.wanted)
                + " m, moved " + std::to_string(leg.travelled) + " m, blocked on "
                + std::to_string(leg.blocked) + " steps, closest to a car "
                + std::to_string(leg.closest) + " m, inside one on "
                + std::to_string(leg.inside) + " steps, pushed out "
                + std::to_string(leg.pushed) + " m; expected: " + leg.expectation);
        CNA::Logger::Info("cna-street:   worst heading error over "
                          + std::to_string(walkHeadingSamples_) + " moving-vehicle samples: "
                          + std::to_string(walkWorstHeading_) + " degrees ("
                          + walkWorstVehicle_ + ")");
        Exit();
        return;
    }

    WalkLeg& leg = walkRoute_[static_cast<std::size_t>(walkLeg_)];
    if (walkTime_ < 0.0f)
    {
        // Start this leg. Three frames to settle before the camera moves, so
        // the screenshot at the end of the previous one is clean.
        camera_.setPosition(Vector3(leg.from.X, Metrics::kEyeHeight, leg.from.Z));
        const Vector3 look = leg.towards - leg.from;
        camera_.setOrientation(std::fabs(look.X) + std::fabs(look.Z) > 1e-3f
                                   ? std::atan2(look.X, -look.Z)
                                   : leg.yaw,
                               -0.03f);
        controller_.setMode(CameraMode::Walk);
        walkTime_ = 0.0f;
        walkSideStep_ = 0.0f;
        return;
    }

    const Vector3 before = camera_.position();
    Vector3 wish(0.0f, 0.0f, 0.0f);
    const Vector3 look(leg.towards.X - before.X, 0.0f, leg.towards.Z - before.Z);
    const float remaining = std::sqrt(look.X * look.X + look.Z * look.Z);
    if (leg.pace > 0.0f && remaining > 0.15f)
    {
        const Vector3 ahead(look.X / remaining, 0.0f, look.Z / remaining);
        // A person who walks into a tree steps round it. Without this the
        // scripted walker stands against the first lamp column for the rest
        // of the leg, which measures the column and not the street.
        if (walkSideStep_ > 0.0f)
        {
            const Vector3 aside(ahead.Z, 0.0f, -ahead.X);
            wish = (ahead * 0.35f + aside * (walkSideStep_ > 0.6f ? 1.0f : -1.0f))
                   * (leg.pace * deltaSeconds);
            walkSideStep_ = std::max(0.0f, walkSideStep_ - deltaSeconds);
        }
        else
            wish = ahead * (leg.pace * deltaSeconds);
    }
    leg.wanted += std::sqrt(wish.X * wish.X + wish.Z * wish.Z);

    controller_.walkStep(deltaSeconds, wish);

    const Vector3 after = camera_.position();
    const float moved = std::sqrt((after.X - before.X) * (after.X - before.X)
                                  + (after.Z - before.Z) * (after.Z - before.Z));
    leg.travelled += moved;
    const float asked = std::sqrt(wish.X * wish.X + wish.Z * wish.Z);
    if (asked > 1e-4f && moved < asked * 0.5f)
    {
        ++leg.blocked;
        // Step round it for the next second, to the left of the way it is
        // going for a while and then to the right, so a walker wedged in a
        // corner gets out of it.
        if (walkSideStep_ <= 0.0f)
            walkSideStep_ = walkTime_ - std::floor(walkTime_ / 6.0f) * 6.0f < 3.0f ? 1.0f : 0.5f;
    }
    if (asked < 1e-4f) leg.pushed += moved;

    // How close it came, and whether it was ever inside.
    const Vector3 body(after.X, after.Y - Metrics::kEyeHeight + 0.95f, after.Z);
    if (scene_->traffic().blocks(body, 0.0f)) ++leg.inside;
    for (const TrafficSystem::Solid& solid : scene_->traffic().solids())
    {
        const float dx = body.X - solid.centre.X;
        const float dz = body.Z - solid.centre.Y;
        const float s = std::sin(solid.heading), c = std::cos(solid.heading);
        const float across = std::fabs(dx * c - dz * s) - solid.halfWidth;
        const float along = std::fabs(dx * s + dz * c) - solid.halfLength;
        leg.closest = std::min(leg.closest, std::max(std::max(across, along), 0.0f));
    }

    walkTime_ += deltaSeconds;
    if (walkTime_ > leg.seconds)
    {
        ++walkLeg_;
        // The shot is taken in *this* frame's Draw, before the next Update
        // teleports the camera to the next leg's start. Two frames of settle
        // photographed the leg after the one it was labelled with.
        walkSettle_ = 1;
        walkTime_ = -1.0f;   // the next leg starts from its own beginning
    }
}

void StreetApplication::captureScreenshot(const std::string& path)
{
    GraphicsDevice& device = getGraphicsDeviceProperty();
    try
    {
        const auto& viewport = device.getViewportProperty();
        const int width  = viewport.getWidthProperty();
        const int height = viewport.getHeightProperty();
        const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        std::vector<Color> pixels(count, Color::Transparent);
        device.GetBackBufferData(pixels.data(), static_cast<int>(count));

        std::vector<std::uint8_t> rgba(count * 4u);
        for (std::size_t i = 0; i < count; ++i)
        {
            rgba[i * 4 + 0] = static_cast<std::uint8_t>(pixels[i].getRProperty());
            rgba[i * 4 + 1] = static_cast<std::uint8_t>(pixels[i].getGProperty());
            rgba[i * 4 + 2] = static_cast<std::uint8_t>(pixels[i].getBProperty());
            rgba[i * 4 + 3] = 255;
        }
        // The supersample resolve: every output pixel is the mean of its
        // N x N block, taken in linear light so a thin bright edge over a
        // dark ground averages to what a camera would have recorded rather
        // than to the darker value that averaging encoded bytes gives.
        int outWidth = width, outHeight = height;
        if (supersample_ > 1)
        {
            const int n = supersample_;
            outWidth  = width / n;
            outHeight = height / n;
            static float toLinear[256];
            static bool tableReady = false;
            if (!tableReady)
            {
                for (int i = 0; i < 256; ++i)
                {
                    const float c = static_cast<float>(i) / 255.0f;
                    toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
                }
                tableReady = true;
            }
            std::vector<std::uint8_t> small(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * 4u);
            const float weight = 1.0f / static_cast<float>(n * n);
            for (int y = 0; y < outHeight; ++y)
                for (int x = 0; x < outWidth; ++x)
                {
                    float sum[3] = {0.0f, 0.0f, 0.0f};
                    for (int dy = 0; dy < n; ++dy)
                        for (int dx = 0; dx < n; ++dx)
                        {
                            const std::size_t at = (static_cast<std::size_t>(y * n + dy) * static_cast<std::size_t>(width)
                                                    + static_cast<std::size_t>(x * n + dx)) * 4u;
                            for (int c = 0; c < 3; ++c) sum[c] += toLinear[rgba[at + static_cast<std::size_t>(c)]];
                        }
                    const std::size_t out = (static_cast<std::size_t>(y) * static_cast<std::size_t>(outWidth)
                                             + static_cast<std::size_t>(x)) * 4u;
                    for (int c = 0; c < 3; ++c)
                    {
                        const float lin = sum[c] * weight;
                        const float enc = lin <= 0.0031308f ? lin * 12.92f
                                                            : 1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f;
                        small[out + static_cast<std::size_t>(c)] =
                            static_cast<std::uint8_t>(std::clamp(enc * 255.0f + 0.5f, 0.0f, 255.0f));
                    }
                    small[out + 3] = 255;
                }
            rgba.swap(small);
        }
        Texture2D shot = Texture2D::CreateFromPixels(device, outWidth, outHeight, rgba);
        shot.SaveAsPng(path);
        CNA::Logger::Info("cna-street: wrote " + path);
    }
    catch (const System::NotSupportedException&)
    {
        CNA::Logger::Error("cna-street: this renderer cannot read the back buffer, so '" + path
                           + "' was not written");
    }
}

}  // namespace CnaStreet
