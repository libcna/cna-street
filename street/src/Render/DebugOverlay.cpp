// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/DebugOverlay.hpp"

#include "CnaStreet/Assets/Canvas.hpp"
#include "CnaStreet/Render/Camera.hpp"
#include "CnaStreet/Render/CameraController.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"
#include "CnaStreet/Render/RenderSettings.hpp"
#include "CnaStreet/Render/SceneRenderer.hpp"
#include "CnaStreet/Scene/CityScene.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <optional>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace CnaStreet {

namespace {

/// Atlas geometry. One 32x32 cell per glyph, sixteen to a row: 95 printable
/// ASCII glyphs fit in six rows, and the whole atlas is one 512x256 texture.
constexpr int kAtlasSize   = 512;
constexpr int kCell        = 32;
constexpr int kColumns     = 16;
constexpr int kFirstChar   = 32;
constexpr int kLastChar    = 126;
constexpr int kGlyphPixels = 22;   ///< em height inside the cell
constexpr int kGlyphInsetX = 3;
constexpr int kGlyphInsetY = 4;
constexpr int kLineSpacing = 25;

std::string Format(const char* format, ...)
{
    char buffer[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    return std::string(buffer);
}

}  // namespace

DebugOverlay::DebugOverlay(GraphicsDevice& device) : device_(device)
{
    frameHistory_.assign(90, 16.0f);
}

DebugOverlay::~DebugOverlay() = default;

void DebugOverlay::build()
{
    // The atlas is drawn with the same stroke font the road signs use, at a
    // size chosen so the strokes land near whole pixels. Baking vector glyphs
    // here rather than shipping a bitmap means the overlay is legible on a
    // 4K display and there is no font file in the repository at all.
    Assets::Canvas canvas(kAtlasSize, 0.0f, 0.0f, 0.0f, 0.0f);
    const float white[3] = {1.0f, 1.0f, 1.0f};
    const float em = static_cast<float>(kGlyphPixels) / static_cast<float>(kAtlasSize);

    std::vector<Rectangle> glyphBounds;
    std::vector<Rectangle> cropping;
    std::vector<SharpRuntime::charcs> characters;
    std::vector<Vector3> kerning;

    for (int code = kFirstChar; code <= kLastChar; ++code)
    {
        const int index = code - kFirstChar;
        const int column = index % kColumns;
        const int row    = index / kColumns;
        const int px = column * kCell + kGlyphInsetX;
        const int py = row * kCell + kGlyphInsetY;

        const std::string glyph(1, static_cast<char>(code));
        const float advance = canvas.measureText(glyph, em) * static_cast<float>(kAtlasSize);
        canvas.text(glyph, static_cast<float>(px) / static_cast<float>(kAtlasSize),
                    static_cast<float>(py) / static_cast<float>(kAtlasSize), em,
                    2.0f / static_cast<float>(kAtlasSize), white);

        glyphBounds.emplace_back(column * kCell, row * kCell, kCell, kCell);
        cropping.emplace_back(0, 0, kCell, kCell);
        characters.push_back(static_cast<SharpRuntime::charcs>(code));
        // XNA advances by kerning.X + kerning.Y + kerning.Z; putting the whole
        // advance in Y keeps the left and right bearings at zero, which is right
        // for a monoline stroke font with no side bearings of its own.
        kerning.emplace_back(0.0f, std::max(advance, 4.0f), 0.0f);
    }

    // Text is a mask, not a colour: the atlas is white with an alpha coverage,
    // and SpriteBatch tints it. Uploading it as linear rather than sRGB keeps
    // the anti-aliased edges from being crushed.
    const std::vector<Color> pixels = canvas.image().toColors(false);
    Texture2D atlas(device_, kAtlasSize, kAtlasSize);
    atlas.SetData(pixels.data(), static_cast<int>(pixels.size()));

    font_ = std::make_unique<SpriteFont>(atlas, std::move(glyphBounds), std::move(cropping),
                                         std::move(characters), kLineSpacing, 0.0f,
                                         std::move(kerning),
                                         std::optional<SharpRuntime::charcs>(u'?'));

    panel_ = std::make_unique<Texture2D>(device_, 1, 1);
    const Color solid = Color::White;
    panel_->SetData(&solid, 1);

    batch_ = std::make_unique<SpriteBatch>(device_);
    ready_ = true;
}

void DebugOverlay::drawPanel(int x, int y, int width, int height, float alpha)
{
    batch_->Draw(*panel_, Rectangle(x, y, width, height), std::optional<Rectangle>(),
                 Color(0, 0, 0, static_cast<int>(alpha * 255.0f)));
}

std::string DebugOverlay::summary(const SceneRenderer& renderer) const
{
    const SceneRenderer::Stats& stats = renderer.stats();
    return Format("%.1f fps  %.2f ms  %d draws  %zu tris",
                  smoothedMs_ > 0.0f ? 1000.0 / static_cast<double>(smoothedMs_) : 0.0,
                  static_cast<double>(smoothedMs_), stats.drawCalls, stats.triangles);
}

void DebugOverlay::draw(const SceneRenderer& renderer, const CityScene& scene, const Camera& camera,
                        const CameraController& controller, const RenderSettings& settings,
                        const GameTime& gameTime)
{
    if (!ready_) return;

    const float frameMs = static_cast<float>(
        gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty()) * 1000.0f;
    frameHistory_[historyIndex_] = frameMs;
    historyIndex_ = (historyIndex_ + 1) % frameHistory_.size();
    // An exponential average for the headline number and the raw history for the
    // graph: a headline that jumps every frame is unreadable, and an average
    // that hides a spike is useless.
    smoothedMs_ = smoothedMs_ * 0.92f + frameMs * 0.08f;

    const SceneRenderer::Stats& stats = renderer.stats();
    const CityScene::BuildStats& build = scene.buildStats();
    const Vector3 position = camera.position();
    const Vector3 forward  = camera.forward();

    std::vector<std::string> lines;
    lines.push_back(Format("cna-street %s   %s renderer   %dx%d", CNA_STREET_VERSION,
                           CNA_STREET_RENDERER_NAME, static_cast<int>(settings.windowWidth),
                           static_cast<int>(settings.windowHeight)));
    lines.push_back(Format("%.1f fps   %.2f ms cpu   cull %.2f  shadow %.2f  opaque %.2f  post %.2f",
                           smoothedMs_ > 0.0f ? 1000.0 / static_cast<double>(smoothedMs_) : 0.0,
                           static_cast<double>(smoothedMs_), static_cast<double>(stats.cullMs),
                           static_cast<double>(stats.shadowMs),
                           static_cast<double>(stats.opaqueMs),
                           static_cast<double>(stats.postMs)));
    lines.push_back(Format("draws %d (+%d shadow, %d instanced)   tris %zu",
                           stats.drawCalls, stats.shadowDrawCalls, stats.instancedDrawCalls,
                           stats.triangles));
    lines.push_back(Format("visible %d/%d batches   %d/%d instances   %d post passes",
                           stats.visibleItems, stats.totalItems, stats.visibleInstances,
                           stats.totalInstances, stats.postPasses));
    lines.push_back(Format("camera %.1f %.1f %.1f   dir %.2f %.2f %.2f   fov %.0f   %s",
                           static_cast<double>(position.X), static_cast<double>(position.Y),
                           static_cast<double>(position.Z), static_cast<double>(forward.X),
                           static_cast<double>(forward.Y), static_cast<double>(forward.Z),
                           static_cast<double>(MathHelper::ToDegrees(camera.verticalFov())),
                           controller.modeName()));
    lines.push_back(Format("sun %.0f deg elev  %.0f deg az   exposure %.2f   %s   %s",
                           static_cast<double>(settings.sunElevationDegrees),
                           static_cast<double>(settings.sunAzimuthDegrees),
                           static_cast<double>(settings.exposure),
                           settings.shadows ? "shadows on" : "shadows off",
                           stats.usedSceneTarget ? "hdr target" : "direct to back buffer"));
    lines.push_back(Format("scene %d plots  %d batches  %zu tris  %zu MiB geometry  %zu textures "
                           "(%zu MiB)   built in %.2f s",
                           build.plots, build.staticBatches, build.triangles,
                           renderer.geometryBytes() / (1024u * 1024u),
                           scene.materialsConst().textureCount(),
                           scene.materialsConst().textureBytes() / (1024u * 1024u),
                           static_cast<double>(build.buildSeconds)));
    lines.push_back("F1 overlay  F2 shadows  F3 ssao  F4 bloom  F5 fog  F6 clouds  F9 screenshot"
                    "   [ ] sun  - = elevation");
    lines.push_back("WASD move  mouse look  Q/E down/up  shift fast  ctrl slow  tab walk/fly  "
                    "c cinematic  1-8 viewpoints  r reset");

    for (const std::string& limitation : renderer.limitations())
        lines.push_back("! " + limitation);

    const int lineHeight = kLineSpacing;
    const int panelHeight = static_cast<int>(lines.size()) * lineHeight + 16;

    device_.setDepthStencilStateProperty(DepthStencilState::None);
    batch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &SamplerState::LinearClamp,
                  &DepthStencilState::None, &RasterizerState::CullNone);
    drawPanel(8, 8, 980, panelHeight, 0.52f);

    int y = 16;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const bool warning = !lines[i].empty() && lines[i][0] == '!';
        const Color colour = warning ? Color(255, 190, 110, 255)
                                     : (i == 0 ? Color(190, 226, 255, 255)
                                               : Color(235, 238, 242, 255));
        batch_->DrawString(*font_, lines[i], Vector2(18.0f, static_cast<float>(y)), colour);
        y += lineHeight;
    }

    // Frame-time graph: 90 frames, one column each, scaled so 33 ms fills it.
    const int graphX = 1000, graphY = 8, graphW = static_cast<int>(frameHistory_.size()) * 3;
    const int graphH = 64;
    drawPanel(graphX, graphY, graphW + 12, graphH + 12, 0.52f);
    for (std::size_t i = 0; i < frameHistory_.size(); ++i)
    {
        const std::size_t sample = (historyIndex_ + i) % frameHistory_.size();
        const float ms = std::clamp(frameHistory_[sample], 0.0f, 33.0f);
        const int height = std::max(1, static_cast<int>(ms / 33.0f * static_cast<float>(graphH)));
        const Color colour = ms > 20.0f ? Color(230, 120, 90, 220)
                                        : (ms > 12.0f ? Color(228, 200, 110, 220)
                                                      : Color(130, 210, 150, 220));
        batch_->Draw(*panel_,
                     Rectangle(graphX + 6 + static_cast<int>(i) * 3, graphY + 6 + graphH - height,
                               2, height),
                     std::optional<Rectangle>(), colour);
    }

    batch_->End();
}

}  // namespace CnaStreet
