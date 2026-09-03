// SPDX-License-Identifier: MIT
#include "CnaStreet/StreetApplication.hpp"

#include "CnaStreet/Render/DebugOverlay.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::KeyboardState;
using Microsoft::Xna::Framework::Input::Keys;
using Microsoft::Xna::Framework::Input::Mouse;

namespace CnaStreet {

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
                "  --width <n> --height <n>          window size\n"
                "  --seed <n>                        procedural seed (default: %u)\n"
                "  --viewpoint <n>                   start at named viewpoint n (1-based)\n"
                "  --camera x,y,z,yaw,pitch          start at an explicit camera (radians)\n"
                "  --frames <n>                      render n frames and exit\n"
                "  --screenshot <file.png>           write one frame and exit\n"
                "  --capture <dir>                   write every viewpoint into dir and exit\n"
                "  --exposure <v>                    exposure multiplier\n"
                "  --shadow-debug                    tint each shadow cascade\n"
                "  --no-shadows --no-bloom --no-ssao --no-fog --no-clouds --no-ibl\n"
                "  --no-traffic --no-pedestrians --no-vegetation --no-overlay\n"
                "  --sun <elevation> <azimuth>       sun position in degrees\n"
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
        else if (arg == "--capture")    { const char* v = next(i); if (v) captureDirectory_ = v; }
        else if (arg == "--exposure")  { const char* v = next(i); if (v) settings_.exposure = static_cast<float>(std::atof(v)); }
        else if (arg == "--no-ibl")         settings_.imageBasedLighting = false;
        else if (arg == "--shadow-debug")   settings_.shadowDebugTint = true;
        else if (arg == "--shadow-distance") { const char* v = next(i); if (v) settings_.shadowDistance = static_cast<float>(std::atof(v)); }
        else if (arg == "--cascades")      { const char* v = next(i); if (v) settings_.shadowCascades = std::atoi(v); }
        else if (arg == "--shadow-bias")   { const char* v = next(i); if (v) settings_.shadowDepthBias = static_cast<float>(std::atof(v)); }
        else if (arg == "--dump-shadow")  { const char* v = next(i); if (v) shadowDumpPath_ = v; }
        else if (arg == "--no-shadows")     settings_.shadows = false;
        else if (arg == "--no-bloom")       settings_.bloom = false;
        else if (arg == "--no-ssao")        settings_.ssao = false;
        else if (arg == "--no-fog")         settings_.heightFog = false;
        else if (arg == "--no-clouds")      settings_.clouds = false;
        else if (arg == "--no-traffic")     settings_.traffic = false;
        else if (arg == "--no-pedestrians") settings_.pedestrians = false;
        else if (arg == "--no-vegetation")  settings_.vegetation = false;
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
        else if (arg == "--dump-settings")
        {
            loadSettingsFile();
            std::fputs(settings_.toJson().c_str(), stdout);
            return false;
        }
        else
        {
            std::fprintf(stderr, "cna-street: unknown option '%s' (try --help)\n", arg.c_str());
            return false;
        }
    }

    loadSettingsFile();
    if (!captureDirectory_.empty())
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
    graphics_->setPreferredBackBufferWidthProperty(settings_.windowWidth);
    graphics_->setPreferredBackBufferHeightProperty(settings_.windowHeight);
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

    materials_ = std::make_unique<MaterialLibrary>(device);
    renderer_  = std::make_unique<SceneRenderer>(device, *materials_);
    renderer_->resize(width, height);
    renderer_->initialise(settings_);

    scene_ = std::make_unique<CityScene>(device, *renderer_, *materials_);
    scene_->build(settings_);

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

    const std::vector<Viewpoint>& viewpoints = scene_->viewpoints();
    if (!viewpoints.empty())
    {
        const int index = std::clamp(startViewpoint_, 0, static_cast<int>(viewpoints.size()) - 1);
        controller_.setHome(viewpoints[static_cast<std::size_t>(index)]);
    }
    if (cameraOverride_) controller_.setHome(cameraOverrideAt_);
    controller_.setCinematicPath(viewpoints, 8.0f);

    overlay_ = std::make_unique<DebugOverlay>(device);
    overlay_->build();
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
}

void StreetApplication::Update(GameTime& gameTime)
{
    Game::Update(gameTime);

    const float dt = static_cast<float>(
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

    if (captureDirectory_.empty())
        controller_.update(dt, keyboard, previousKeyboard_, mouse, previousMouse_);

    if (scene_ != nullptr) scene_->update(dt, settings_);

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

    renderer_->beginFrame();
    scene_->submit(settings_);
    renderer_->render(camera_, settings_, elapsedSeconds_);

    if (settings_.debugOverlay && overlay_ != nullptr)
        overlay_->draw(*renderer_, *scene_, camera_, controller_, settings_, gameTime);

    ++framesDrawn_;

    if (!shadowDumpPath_.empty() && framesDrawn_ >= 3)
    {
        renderer_->dumpShadowAtlas(shadowDumpPath_);
        shadowDumpPath_.clear();
    }
    if (!screenshotPath_.empty() && framesDrawn_ >= 3)
    {
        captureScreenshot(screenshotPath_);
        screenshotPath_.clear();
        // A one-shot --screenshot run is finished here; a --capture run has more
        // viewpoints to walk and ends when the script says so.
        if (frameBudget_ == 0 && captureDirectory_.empty()) Exit();
    }
    if (frameBudget_ > 0 && framesDrawn_ >= frameBudget_) Exit();
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
        Texture2D shot = Texture2D::CreateFromPixels(device, width, height, rgba);
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
