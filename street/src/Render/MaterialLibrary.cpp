// SPDX-License-Identifier: MIT
#include "CnaStreet/Render/MaterialLibrary.hpp"

#include "CnaStreet/Assets/SignFactory.hpp"
#include "CnaStreet/Assets/TextureFactory.hpp"
#include "CnaStreet/Core/Rng.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <filesystem>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CnaStreet::Assets::Image;
using CnaStreet::Assets::SignFace;
using CnaStreet::Assets::SignFactory;
using CnaStreet::Assets::SurfaceMaps;
using CnaStreet::Assets::TextureFactory;
using Microsoft::Xna::Framework::Content::ContentManager;

namespace CnaStreet {

namespace {

/// Texture budget. Surfaces the camera can put its nose against get the most
/// pixels; a bollard's paint gets the fewest. Guessing these uniformly is how a
/// scene ends up spending 200 MB on things nobody looks at.
constexpr int kLarge  = 512;
constexpr int kMedium = 256;
constexpr int kSmall  = 128;
constexpr int kSign   = 256;

float ToLinear(float s)
{
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

Vector3 Srgb(int r, int g, int b)
{
    return Vector3(ToLinear(static_cast<float>(r) / 255.0f),
                   ToLinear(static_cast<float>(g) / 255.0f),
                   ToLinear(static_cast<float>(b) / 255.0f));
}

void Rgb(float out[3], const Vector3& v) { out[0] = v.X; out[1] = v.Y; out[2] = v.Z; }

}  // namespace

MaterialLibrary::MaterialLibrary(GraphicsDevice* device) : device_(device)
{
    materials_.resize(static_cast<std::size_t>(MaterialId::Count));
}

void MaterialLibrary::setContentSource(ContentManager* content) { content_ = content; }

void MaterialLibrary::setBakeDirectory(const std::string& directory)
{
    bakeDirectory_ = directory;
    if (!bakeDirectory_.empty()) std::filesystem::create_directories(bakeDirectory_);
}

MaterialLibrary::~MaterialLibrary() = default;

const Material& MaterialLibrary::get(MaterialId id) const
{
    return materials_[static_cast<std::size_t>(id)];
}

Material& MaterialLibrary::mutableGet(MaterialId id)
{
    return materials_[static_cast<std::size_t>(id)];
}

std::size_t MaterialLibrary::materialCount() const
{
    return materials_.size() + derived_.size();
}

const Material* MaterialLibrary::derive(const std::string& name, MaterialId base,
                                        const Vector2& uvScale, const Vector2& uvOffset)
{
    const auto existing = derived_.find(name);
    if (existing != derived_.end()) return existing->second.get();

    auto material = std::make_unique<Material>(get(base));
    material->name     = name;
    material->uvScale  = uvScale;
    material->uvOffset = uvOffset;
    Material* raw = material.get();
    derived_.emplace(name, std::move(material));
    return raw;
}

const Material* MaterialLibrary::deriveTinted(const std::string& name, MaterialId base,
                                              const Vector3& baseColour, const Vector3& emissive)
{
    const auto existing = derived_.find(name);
    if (existing != derived_.end()) return existing->second.get();

    auto material = std::make_unique<Material>(get(base));
    material->name           = name;
    material->baseColour     = baseColour;
    material->emissiveFactor = emissive;
    Material* raw = material.get();
    derived_.emplace(name, std::move(material));
    return raw;
}

const Material* MaterialLibrary::add(const std::string& name, const Assets::SurfaceMaps& maps,
                                     Material material)
{
    const auto existing = derived_.find(name);
    if (existing != derived_.end()) return existing->second.get();

    material.name     = name;
    material.albedo   = upload(maps.albedo, true, name + ".albedo");
    material.normal   = upload(maps.normal, false, name + ".normal");
    material.orm      = upload(maps.orm, false, name + ".orm");
    material.emissive = maps.emissive.empty() ? nullptr
                                              : upload(maps.emissive, true, name + ".emissive");
    auto owned = std::make_unique<Material>(material);
    Material* raw = owned.get();
    derived_.emplace(name, std::move(owned));
    return raw;
}

const Material* MaterialLibrary::find(const std::string& name) const
{
    const auto existing = derived_.find(name);
    return existing == derived_.end() ? nullptr : existing->second.get();
}

Texture2D* MaterialLibrary::upload(const Image& image, bool srgb, const std::string& name)
{
    if (image.empty()) return nullptr;

    // A full mip chain, always. Without one, a road surface at a grazing angle
    // aliases into a shimmer that no amount of anti-aliasing afterwards can fix,
    // because the detail was already lost when the texel was sampled.
    if (device_ == nullptr) return nullptr;
    auto texture = std::make_unique<Texture2D>(*device_, image.width(), image.height(), true,
                                               SurfaceFormat::Color);
    Image level = image;
    int index = 0;
    while (true)
    {
        const std::vector<Color> pixels = level.toColors(srgb);
        texture->SetData(index, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        textureBytes_ += pixels.size() * 4u;
        if (level.width() <= 1 && level.height() <= 1) break;
        if (index + 1 >= texture->getLevelCountProperty()) break;
        level = level.downsampled();
        ++index;
    }

    Texture2D* raw = texture.get();
    textures_.push_back(std::move(texture));
    CNA::Logger::Debug("cna-street: uploaded texture '" + name + "' "
                       + std::to_string(image.width()) + "x" + std::to_string(image.height()));
    return raw;
}

Texture2D* MaterialLibrary::load(const std::string& asset)
{
    if (content_ == nullptr) return nullptr;
    try
    {
        auto texture = std::make_unique<Texture2D>(content_->Load<Texture2D>(asset));
        if (texture->getWidthProperty() <= 0) return nullptr;
        textureBytes_ += static_cast<std::size_t>(texture->getWidthProperty())
                         * static_cast<std::size_t>(texture->getHeightProperty()) * 4u;
        textures_.push_back(std::move(texture));
        return textures_.back().get();
    }
    catch (const std::exception&)
    {
        // A missing asset is the normal case for a tree that has never had a
        // content build run in it, and it is not an error: the surface is
        // generated instead. Anything else the loader throws is treated the same
        // way for the same reason -- a broken .cnb should cost start-up time,
        // not the whole street.
        return nullptr;
    }
}

bool MaterialLibrary::installFromContent(Material& material, const std::string& name)
{
    Texture2D* albedo = load(name + ".albedo");
    if (albedo == nullptr) return false;
    Texture2D* normal = load(name + ".normal");
    Texture2D* orm    = load(name + ".orm");
    if (normal == nullptr || orm == nullptr) return false;

    material.albedo   = albedo;
    material.normal   = normal;
    material.orm      = orm;
    material.emissive = load(name + ".emissive");   // optional
    return true;
}

void MaterialLibrary::bake(const std::string& name, const SurfaceMaps& maps)
{
    const std::filesystem::path root(bakeDirectory_);
    maps.albedo.writePng((root / (name + ".albedo.png")).string(), true);
    maps.normal.writePng((root / (name + ".normal.png")).string(), false);
    maps.orm.writePng((root / (name + ".orm.png")).string(), false);
    if (maps.hasEmissive())
        maps.emissive.writePng((root / (name + ".emissive.png")).string(), true);
}

void MaterialLibrary::install(MaterialId id, const std::string& name,
                              const std::function<SurfaceMaps()>& generate, Material material)
{
    material.name = name;

    if (!bakeDirectory_.empty())
    {
        bake(name, generate());
        materials_[static_cast<std::size_t>(id)] = std::move(material);
        return;
    }

    if (installFromContent(material, name))
    {
        ++loadedCount_;
        materials_[static_cast<std::size_t>(id)] = std::move(material);
        return;
    }

    const SurfaceMaps maps = generate();
    material.albedo = upload(maps.albedo, true, name + ".albedo");
    material.normal = upload(maps.normal, false, name + ".normal");
    material.orm    = upload(maps.orm, false, name + ".orm");
    if (maps.hasEmissive()) material.emissive = upload(maps.emissive, true, name + ".emissive");
    ++generatedCount_;
    materials_[static_cast<std::size_t>(id)] = std::move(material);
}

void MaterialLibrary::build(std::uint32_t seed)
{
    Rng rng = Rng::derive(seed, "materials");
    const std::uint32_t s = static_cast<std::uint32_t>(rng.intRange(1, 1 << 28));

    auto pbr = [](float roughness, float metallic) {
        Material m;
        m.roughness = roughness;
        m.metallic  = metallic;
        return m;
    };

    // ---------------------------------------------------------------------
    // Highway surfaces
    // ---------------------------------------------------------------------
    {
        // The road is the largest single surface in the picture and the one the
        // camera gets closest to, so its normal is dialled back further still:
        // at full strength the grazing view down the street shimmered.
        Material asphalt = pbr(0.88f, 0.0f);
        asphalt.normalScale = 0.7f;
        install(MaterialId::AsphaltMain, "asphalt-main",
                [&] { return TextureFactory::asphalt(kLarge, s + 1u, 0.55f); }, asphalt);
        asphalt.roughness = 0.90f;
        install(MaterialId::AsphaltSide, "asphalt-side",
                [&] { return TextureFactory::asphalt(kLarge, s + 2u, 0.32f); }, asphalt);
        asphalt.roughness = 0.92f;
        install(MaterialId::AsphaltWorn, "asphalt-worn",
                [&] { return TextureFactory::asphalt(kLarge, s + 3u, 0.92f); }, asphalt);
    }

    {
        Material track = pbr(0.58f, 0.0f);
        track.alphaMode   = AlphaModeEXT::Mask;
        track.alphaCutoff = 0.5f;
        track.castsShadow = false;
        track.writesDepth = false;
        install(MaterialId::WheelTrack, "wheel-track",
                [&] { return TextureFactory::wheelTrack(kMedium, s + 61u); }, track);
    }

    {
        Material paint = pbr(0.66f, 0.0f);
        // Masked rather than blended: the worn-through parts of a line are holes
        // in the paint, and a blended decal over asphalt would need sorting for
        // no visual gain.
        paint.alphaMode   = AlphaModeEXT::Mask;
        paint.alphaCutoff = 0.35f;
        paint.castsShadow = false;
        paint.writesDepth = false;
        install(MaterialId::RoadMarking, "road-marking",
                [&] { return TextureFactory::roadPaint(kMedium, s + 4u, 0.45f); }, paint);
    }

    install(MaterialId::ConcretePaving, "concrete-paving",
            [&] { return TextureFactory::concretePaving(kLarge, s + 5u); }, pbr(0.80f, 0.0f));
    install(MaterialId::TactilePaving, "tactile-paving",
            [&] { return TextureFactory::tactilePaving(kMedium, s + 6u); }, pbr(0.82f, 0.0f));
    install(MaterialId::GraniteSetts, "granite-setts",
            [&] { return TextureFactory::graniteSetts(kLarge, s + 7u); }, pbr(0.62f, 0.0f));
    install(MaterialId::GraniteKerb, "granite-kerb", [&] { return TextureFactory::graniteKerb(kMedium, s + 8u); },
            pbr(0.64f, 0.0f));

    {
        Material iron = pbr(0.72f, 0.7f);
        install(MaterialId::ManholeIron, "manhole-iron",
                [&] { return TextureFactory::manholeCover(kMedium, s + 9u); }, iron);
        // The gully grate shares the ironwork, differing only in its tint.
        Material grate = materials_[static_cast<std::size_t>(MaterialId::ManholeIron)];
        grate.name       = "drain-grate";
        grate.baseColour = Vector3(0.62f, 0.60f, 0.58f);
        materials_[static_cast<std::size_t>(MaterialId::DrainGrate)] = grate;
    }

    install(MaterialId::Grass, "grass", [&] { return TextureFactory::grass(kMedium, s + 10u); }, pbr(0.92f, 0.0f));
    {
        const Vector3 soil = Srgb(74, 60, 46);
        float c[3];
        Rgb(c, soil);
        install(MaterialId::Soil, "soil", [&] { return TextureFactory::flat(8, c, 0.95f, 0.0f); }, pbr(0.95f, 0.0f));
    }

    // ---------------------------------------------------------------------
    // Façades. The render is generated *white* and tinted per material: the
    // pattern of a lime render does not depend on what colour it was painted, so
    // seven façade colours cost one 512² texture set rather than seven.
    // ---------------------------------------------------------------------
    install(MaterialId::BrickRed, "brick-red", [&] { return TextureFactory::brick(kLarge, s + 11u, 0.08f, 0.55f); },
            pbr(0.86f, 0.0f));
    install(MaterialId::BrickBuff, "brick-buff",
            [&] { return TextureFactory::brick(kLarge, s + 12u, 0.85f, 0.40f); }, pbr(0.86f, 0.0f));
    {
        Material engineering = materials_[static_cast<std::size_t>(MaterialId::BrickRed)];
        engineering.name       = "brick-engineering";
        engineering.baseColour = Vector3(0.52f, 0.46f, 0.46f);
        engineering.roughness  = 0.66f;
        materials_[static_cast<std::size_t>(MaterialId::BrickEngineering)] = engineering;
    }

    {
        const float whiteRgb[3] = {1.0f, 1.0f, 1.0f};
        install(MaterialId::RenderCream, "render",
                [&] { return TextureFactory::plaster(kLarge, s + 13u, whiteRgb, 0.60f); },
                pbr(0.88f, 0.0f));

        struct Tint { MaterialId id; const char* name; Vector3 colour; };
        // A real street's palette: the ochres and creams of lime render, one
        // sage green, one dusty blue, one terracotta. Deliberately desaturated —
        // saturated façade colours are the fastest way to make a street cartoonish.
        const Tint tints[] = {
            {MaterialId::RenderCream,      "render-cream",      Srgb(214, 202, 176)},
            {MaterialId::RenderOchre,      "render-ochre",      Srgb(202, 168, 114)},
            {MaterialId::RenderSage,       "render-sage",       Srgb(166, 174, 152)},
            {MaterialId::RenderGrey,       "render-grey",       Srgb(176, 174, 168)},
            {MaterialId::RenderTerracotta, "render-terracotta", Srgb(190, 140, 116)},
            {MaterialId::RenderBlue,       "render-blue",       Srgb(158, 172, 182)},
            {MaterialId::RenderWhite,      "render-white",      Srgb(228, 224, 214)},
        };
        const Material base = materials_[static_cast<std::size_t>(MaterialId::RenderCream)];
        for (const Tint& tint : tints)
        {
            Material m   = base;
            m.name       = tint.name;
            m.baseColour = tint.colour;
            materials_[static_cast<std::size_t>(tint.id)] = m;
        }
    }

    install(MaterialId::Ashlar, "ashlar", [&] { return TextureFactory::ashlar(kLarge, s + 14u, 0.55f); },
            pbr(0.82f, 0.0f));
    {
        Material panel = pbr(0.74f, 0.0f);
        panel.baseColour = Vector3(0.72f, 0.72f, 0.70f);
        install(MaterialId::ConcretePanel, "concrete-panel",
                [&] { return TextureFactory::concretePaving(kLarge, s + 15u); }, panel);
        // Precast cladding is a large flat panel, not paving: keep the aggregate
        // and weathering but scale the joints out of sight with a large tile.
        materials_[static_cast<std::size_t>(MaterialId::ConcretePanel)].roughness = 0.70f;

        // The district beyond the modelled frontage. Six façades as *images* of
        // a storey rather than as geometry: at 200 m a modelled reveal is a
        // sub-pixel and a painted one is indistinguishable, but the difference
        // between having windows and not having them is the whole picture --
        // the first version's blank massing was the loudest thing in every
        // aerial shot.
        struct ContextFacade { MaterialId id; const char* name; Vector3 wall; int bays; };
        const ContextFacade contextFacades[] = {
            {MaterialId::ContextFacade0, "context-facade-0", Srgb(206, 199, 184), 4},
            {MaterialId::ContextFacade1, "context-facade-1", Srgb(178, 172, 166), 5},
            {MaterialId::ContextFacade2, "context-facade-2", Srgb(150, 108, 84),  4},
            {MaterialId::ContextFacade3, "context-facade-3", Srgb(196, 186, 160), 6},
            {MaterialId::ContextFacade4, "context-facade-4", Srgb(168, 174, 170), 5},
            {MaterialId::ContextFacade5, "context-facade-5", Srgb(214, 208, 198), 4},
        };
        for (const ContextFacade& facade : contextFacades)
        {
            Material material = pbr(0.72f, 0.0f);
            float wall[3];
            Rgb(wall, facade.wall);
            const int bays = facade.bays;
            const std::uint32_t facadeSeed =
                s + 90u + static_cast<std::uint32_t>(&facade - contextFacades);
            install(facade.id, facade.name,
                    [&, bays, facadeSeed] {
                        float local[3] = {wall[0], wall[1], wall[2]};
                        return TextureFactory::facadeGrid(kMedium, facadeSeed, bays, local);
                    },
                    material);
        }
    }

    install(MaterialId::RoofTile, "roof-tile", [&] { return TextureFactory::roofTile(kMedium, s + 16u); },
            pbr(0.80f, 0.0f));
    install(MaterialId::RoofFelt, "roof-felt", [&] { return TextureFactory::roofFelt(kMedium, s + 17u); },
            pbr(0.88f, 0.0f));
    install(MaterialId::RoofZinc, "roof-zinc", [&] { return TextureFactory::sheetMetal(kMedium, s + 18u); },
            pbr(0.50f, 0.85f));

    // ---------------------------------------------------------------------
    // Glazing
    // ---------------------------------------------------------------------
    {
        Material glass = pbr(0.06f, 0.0f);
        glass.alphaMode   = AlphaModeEXT::Blend;
        glass.alpha       = 0.34f;
        glass.baseColour  = Vector3(0.72f, 0.78f, 0.76f);
        glass.ior         = 1.52f;
        glass.specular    = 1.0f;
        glass.castsShadow = false;   // a pane casting an opaque shadow is a tell
        glass.writesDepth = false;
        install(MaterialId::Glazing, "glazing", [&] { return TextureFactory::windowGlass(kMedium, s + 19u); },
                glass);

        Material shop = materials_[static_cast<std::size_t>(MaterialId::Glazing)];
        shop.name       = "shop-glazing";
        shop.alpha      = 0.24f;      // large, clean, frequently cleaned panes
        shop.roughness  = 0.035f;
        shop.baseColour = Vector3(0.80f, 0.84f, 0.83f);
        materials_[static_cast<std::size_t>(MaterialId::ShopGlazing)] = shop;
    }
    {
        Material interior = pbr(0.90f, 0.0f);
        interior.emissiveFactor = Vector3(1.0f, 1.0f, 1.0f);
        install(MaterialId::Interior, "interior",
                [&] { return TextureFactory::interiorAtlas(kLarge, s + 20u); }, interior);

        // Shop fittings: the painted MDF and powder-coated steel that shelving,
        // counters and display plinths are actually made of. Pale, because a
        // shop interior is a bright box and dark fittings behind glass vanish.
        Material fitting = pbr(0.62f, 0.0f);
        fitting.baseColour = Srgb(212, 209, 203);
        float fittingRgb[3];
        Rgb(fittingRgb, fitting.baseColour);
        install(MaterialId::ShopFitting, "shop-fitting",
                [&] { return TextureFactory::paintedMetal(kSmall, s + 61u, fittingRgb, 0.62f); },
                fitting);

        // The product on the shelves. One noisy, saturated surface for all of
        // it: at the distance a shop window is looked into, stock is colour and
        // nothing else, and giving each shop its own palette would cost sixteen
        // textures to say something nobody can resolve.
        Material stock = pbr(0.68f, 0.0f);
        install(MaterialId::ShopStock, "shop-stock",
                [&] { return TextureFactory::shopStock(kSmall, s + 62u); }, stock);

        // A lit ceiling panel. Emissive rather than a real light: a punctual
        // light per shop would be forty of them in the clustered set to
        // illuminate a room nobody stands in, and what the street actually sees
        // is the *brightness* of the soffit through the glass.
        Material ceiling = pbr(0.55f, 0.0f);
        ceiling.baseColour     = Vector3(0.92f, 0.90f, 0.86f);
        ceiling.emissiveFactor = Vector3(2.30f, 2.10f, 1.80f);
        float ceilingRgb[3];
        Rgb(ceilingRgb, ceiling.baseColour);
        install(MaterialId::ShopCeilingLight, "shop-ceiling-light",
                [&] { return TextureFactory::flat(8, ceilingRgb, 0.55f, 0.0f); }, ceiling);

        // The room itself: walls, floor, joinery and the dark of a screen.
        // Separate entries rather than the façade's own materials, and the
        // reason is not colour -- it is that a shop interior is an enclosed box
        // whose shadow nothing outside can ever see, and whose geometry is
        // invisible past the width of the street. Sharing a material with the
        // wall in front of it would mean drawing a wall of shelving into the
        // cascade atlas from two hundred metres.
        Material shopWall = pbr(0.86f, 0.0f);
        shopWall.baseColour  = Srgb(196, 194, 188);
        shopWall.castsShadow = false;
        const float shopWallRgb[3] = {1.0f, 1.0f, 1.0f};
        install(MaterialId::ShopWall, "shop-wall",
                [&] { return TextureFactory::plaster(kMedium, s + 63u, shopWallRgb, 0.55f); },
                shopWall);

        Material shopFloor = pbr(0.55f, 0.0f);
        shopFloor.baseColour  = Srgb(168, 164, 158);
        shopFloor.castsShadow = false;
        install(MaterialId::ShopFloor, "shop-floor",
                [&] { return TextureFactory::concretePaving(kMedium, s + 64u); }, shopFloor);

        Material shopTimber = pbr(0.52f, 0.0f);
        shopTimber.castsShadow = false;
        install(MaterialId::ShopTimber, "shop-timber",
                [&] { return TextureFactory::hardwood(kSmall, s + 65u); }, shopTimber);

        Material shopScreen = pbr(0.16f, 0.0f);
        shopScreen.baseColour  = Srgb(16, 17, 20);
        shopScreen.castsShadow = false;
        float screenRgb[3];
        Rgb(screenRgb, shopScreen.baseColour);
        install(MaterialId::ShopScreen, "shop-screen",
                [&] { return TextureFactory::flat(8, screenRgb, 0.16f, 0.0f); }, shopScreen);

        materials_[static_cast<std::size_t>(MaterialId::ShopFitting)].castsShadow = false;
        materials_[static_cast<std::size_t>(MaterialId::ShopStock)].castsShadow = false;
        materials_[static_cast<std::size_t>(MaterialId::ShopCeilingLight)].castsShadow = false;
    }

    // ---------------------------------------------------------------------
    // Joinery and paint. One neutral painted-metal texture drives every painted
    // surface in the city; the colour is the material's, not the texture's.
    // ---------------------------------------------------------------------
    {
        const float whiteRgb[3] = {1.0f, 1.0f, 1.0f};
        install(MaterialId::FrameWhite, "painted", [&] { return TextureFactory::paintedMetal(kSmall, s + 21u,
                                                                                whiteRgb, 0.38f); },
                pbr(0.38f, 0.0f));
        const Material painted = materials_[static_cast<std::size_t>(MaterialId::FrameWhite)];

        struct Tint { MaterialId id; const char* name; Vector3 colour; float roughness; float metal; };
        const Tint tints[] = {
            {MaterialId::FrameWhite,        "frame-white",        Srgb(232, 230, 224), 0.34f, 0.0f},
            {MaterialId::FrameDark,         "frame-dark",         Srgb(58, 60, 62),    0.36f, 0.0f},
            {MaterialId::FrameBronze,       "frame-bronze",       Srgb(96, 80, 60),    0.30f, 0.7f},
            {MaterialId::DoorGreen,         "door-green",         Srgb(38, 68, 54),    0.30f, 0.0f},
            {MaterialId::DoorRed,           "door-red",           Srgb(112, 40, 38),   0.30f, 0.0f},
            {MaterialId::DoorBlue,          "door-blue",          Srgb(44, 62, 92),    0.30f, 0.0f},
            {MaterialId::ShopFascia,        "shop-fascia",        Srgb(48, 44, 44),    0.44f, 0.0f},
            {MaterialId::Awning,            "awning",             Srgb(126, 52, 46),   0.72f, 0.0f},
            {MaterialId::PaintedSteelDark,  "steel-dark",         Srgb(52, 54, 56),    0.42f, 0.05f},
            {MaterialId::PaintedSteelGrey,  "steel-grey",         Srgb(126, 128, 128), 0.44f, 0.05f},
            {MaterialId::PaintedSteelGreen, "steel-green",        Srgb(42, 62, 52),    0.42f, 0.05f},
            {MaterialId::PaintedSteelBlack, "steel-black",        Srgb(28, 28, 30),    0.40f, 0.05f},
            {MaterialId::GalvanisedSteel,   "galvanised",         Srgb(158, 160, 162), 0.52f, 0.80f},
            // Cast-and-machined wheel alloy, not mill-finish sheet: at 0.34
            // roughness this was a mirror, and under a bright sky every parked
            // car had two white discs where its wheels should be.
            {MaterialId::Aluminium,         "wheel-alloy",        Srgb(138, 140, 143), 0.44f, 0.85f},
            {MaterialId::SignalHousing,     "signal-housing",     Srgb(38, 40, 40),    0.46f, 0.05f},
            {MaterialId::HydrantRed,        "hydrant-red",        Srgb(140, 32, 30),   0.40f, 0.0f},
            {MaterialId::BinBody,           "bin-body",           Srgb(62, 66, 64),    0.48f, 0.10f},
            {MaterialId::CabinetGrey,       "cabinet-grey",       Srgb(150, 150, 146), 0.52f, 0.05f},
            {MaterialId::SignBack,          "sign-back",          Srgb(148, 150, 150), 0.40f, 0.60f},
            {MaterialId::CarTrim,           "car-trim",           Srgb(34, 34, 36),    0.36f, 0.30f},
        };
        for (const Tint& tint : tints)
        {
            Material m   = painted;
            m.name       = tint.name;
            m.baseColour = tint.colour;
            m.roughness  = tint.roughness;
            m.metallic   = tint.metal;
            materials_[static_cast<std::size_t>(tint.id)] = m;
        }
    }

    install(MaterialId::DoorOak, "door-oak", [&] { return TextureFactory::hardwood(kSmall, s + 22u); },
            pbr(0.46f, 0.0f));
    install(MaterialId::Timber, "timber", [&] { return TextureFactory::hardwood(kSmall, s + 23u); },
            pbr(0.52f, 0.0f));

    // ---------------------------------------------------------------------
    // Signal lenses. Emissive, and the emissive factor is what the traffic-light
    // state machine animates, so the material starts dark.
    // ---------------------------------------------------------------------
    {
        struct Lens { MaterialId id; const char* name; Vector3 colour; };
        const Lens lenses[] = {
            {MaterialId::LensRed,       "lens-red",        Srgb(196, 34, 26)},
            {MaterialId::LensAmber,     "lens-amber",      Srgb(226, 148, 22)},
            {MaterialId::LensGreen,     "lens-green",      Srgb(30, 176, 96)},
            {MaterialId::LensWalkRed,   "lens-walk-red",   Srgb(202, 44, 34)},
            {MaterialId::LensWalkGreen, "lens-walk-green", Srgb(38, 182, 104)},
        };
        for (const Lens& lens : lenses)
        {
            float c[3];
            Rgb(c, lens.colour);
            Material m = pbr(0.18f, 0.0f);
            m.baseColour     = lens.colour * 0.25f;
            m.emissiveFactor = Vector3::Zero;   // lit by the signal controller
            install(lens.id, lens.name, [&] { return TextureFactory::flat(8, c, 0.18f, 0.0f); }, m);
        }
        float glassRgb[3] = {0.85f, 0.85f, 0.82f};
        Material lamp = pbr(0.12f, 0.0f);
        lamp.baseColour     = Vector3(0.86f, 0.86f, 0.82f);
        lamp.emissiveFactor = Vector3::Zero;
        install(MaterialId::LampGlass, "lamp-glass", [&] { return TextureFactory::flat(8, glassRgb, 0.12f, 0.0f); },
                lamp);
    }

    // ---------------------------------------------------------------------
    // Sign faces
    // ---------------------------------------------------------------------
    {
        struct Face { MaterialId id; SignFace face; };
        const Face faces[] = {
            {MaterialId::SignFaceProhibition, SignFace::SpeedLimit30},
            {MaterialId::SignFaceWarning,     SignFace::ChildrenWarning},
            {MaterialId::SignFaceInformation, SignFace::PedestrianCrossing},
            {MaterialId::SignFaceParking,     SignFace::ParkingArea},
            {MaterialId::SignFacePriority,    SignFace::PriorityRoad},
        };
        for (const Face& face : faces)
        {
            Material m = pbr(0.32f, 0.0f);
            m.alphaMode   = AlphaModeEXT::Mask;
            m.alphaCutoff = 0.5f;
            m.doubleSided = false;
            install(face.id, std::string("sign-") + SignFactory::faceName(face.face),
                    [&] { return SignFactory::face(face.face, kSign, s + 40u); }, m);
        }
        // Two plates, because the two streets have two names and the lettering
        // is baked into the texture. Everything else about them is identical, so
        // this is the one case in the catalogue where two textures really are
        // needed rather than one texture and two tints.
        Material plate = pbr(0.30f, 0.0f);
        install(MaterialId::SignFaceStreetName, "sign-street-name",
                [&] { return SignFactory::streetPlate("LINDENSTRASSE", kSign, kSign / 4, s + 41u); }, plate);
        install(MaterialId::SignFaceStreetNameSide, "sign-street-name-side",
                [&] { return SignFactory::streetPlate("MARKTGASSE", kSign, kSign / 4, s + 42u); }, plate);
    }

    // ---------------------------------------------------------------------
    // Vehicles
    // ---------------------------------------------------------------------
    {
        const float whiteRgb[3] = {1.0f, 1.0f, 1.0f};
        Material body = pbr(0.16f, 0.0f);
        // White so the per-instance tint decides the colour of each car.
        body.baseColour = Vector3::One;
        install(MaterialId::CarBody, "car-body",
                [&] { return TextureFactory::carPaint(kMedium, s + 24u, whiteRgb, 0.6f); }, body);

        // Tinted, and much less transparent than a shop window. Car glazing
        // transmits about a quarter of the light at the angles a street is seen
        // from, and the rest of what reaches the eye is reflection -- so alpha
        // 0.52 with a pale texture behind it produced a fleet of greenhouses.
        Material glass = pbr(0.05f, 0.0f);
        glass.alphaMode   = AlphaModeEXT::Blend;
        glass.alpha       = 0.80f;
        glass.baseColour  = Vector3(0.055f, 0.062f, 0.068f);
        glass.ior         = 1.52f;
        glass.specular    = 1.0f;
        glass.castsShadow = false;
        glass.writesDepth = false;
        install(MaterialId::CarGlass, "car-glass",
                [&] { return TextureFactory::vehicleGlass(kSmall, s + 25u); }, glass);

        float tyreRgb[3] = {0.026f, 0.026f, 0.028f};
        install(MaterialId::CarTyre, "car-tyre", [&] { return TextureFactory::flat(8, tyreRgb, 0.92f, 0.0f); },
                pbr(0.92f, 0.0f));

        Material rear = pbr(0.22f, 0.0f);
        rear.baseColour     = Srgb(150, 22, 20);
        rear.emissiveFactor = Vector3(0.35f, 0.02f, 0.01f);
        float rearRgb[3];
        Rgb(rearRgb, rear.baseColour);
        install(MaterialId::CarLightRear, "car-light-rear",
                [&] { return TextureFactory::flat(8, rearRgb, 0.22f, 0.0f); }, rear);

        Material front = pbr(0.10f, 0.0f);
        front.baseColour = Vector3(0.82f, 0.84f, 0.86f);
        float frontRgb[3];
        Rgb(frontRgb, front.baseColour);
        install(MaterialId::CarLightFront, "car-light-front",
                [&] { return TextureFactory::flat(8, frontRgb, 0.10f, 0.0f); }, front);

        // Everything you see *through* the glass. A car's cabin is a dark
        // grey-brown, and its exact colour matters far less than the fact that
        // there is something there: with a glazed greenhouse and no interior a
        // parked car is a coloured shell with the building behind showing
        // through it.
        Material interior = pbr(0.86f, 0.0f);
        interior.baseColour = Srgb(28, 27, 29);
        float interiorRgb[3];
        Rgb(interiorRgb, interior.baseColour);
        install(MaterialId::CarInterior, "car-interior",
                [&] { return TextureFactory::flat(8, interiorRgb, 0.86f, 0.0f); }, interior);

        Material underbody = pbr(0.95f, 0.0f);
        underbody.baseColour = Srgb(20, 20, 21);
        float underRgb[3];
        Rgb(underRgb, underbody.baseColour);
        install(MaterialId::CarUnderbody, "car-underbody",
                [&] { return TextureFactory::flat(8, underRgb, 0.95f, 0.0f); }, underbody);

        // A brake disc seen through the spokes: rusty-dark iron, half metal.
        Material brake = pbr(0.62f, 0.55f);
        brake.baseColour = Srgb(52, 48, 46);
        float brakeRgb[3];
        Rgb(brakeRgb, brake.baseColour);
        install(MaterialId::CarBrake, "car-brake",
                [&] { return TextureFactory::flat(8, brakeRgb, 0.62f, 0.55f); }, brake);

        Material plate = pbr(0.36f, 0.0f);
        install(MaterialId::LicencePlate, "licence-plate",
                [&] { return SignFactory::licencePlate("B MX 4271", kSmall * 2, kSmall / 2); }, plate);
    }

    // ---------------------------------------------------------------------
    // People
    // ---------------------------------------------------------------------
    {
        const float whiteRgb[3] = {1.0f, 1.0f, 1.0f};
        Material skin = pbr(0.54f, 0.0f);
        skin.baseColour = Vector3::One;   // tinted per pedestrian
        install(MaterialId::Skin, "skin", [&] { return TextureFactory::skin(kSmall, s + 26u, whiteRgb); }, skin);

        Material cloth = pbr(0.90f, 0.0f);
        cloth.baseColour = Vector3::One;
        install(MaterialId::Clothing, "clothing",
                [&] { return TextureFactory::fabric(kSmall, s + 27u, whiteRgb); }, cloth);
    }

    // ---------------------------------------------------------------------
    // Vegetation
    // ---------------------------------------------------------------------
    install(MaterialId::Bark, "bark", [&] { return TextureFactory::bark(kMedium, s + 28u); }, pbr(0.90f, 0.0f));
    {
        Material leaves = pbr(0.82f, 0.0f);
        // Alpha-masked and double-sided: a leaf card seen from behind must still
        // be a leaf, and blending would need per-triangle sorting inside a crown.
        leaves.alphaMode   = AlphaModeEXT::Mask;
        leaves.alphaCutoff = 0.42f;
        leaves.doubleSided = true;
        install(MaterialId::Foliage, "foliage", [&] { return TextureFactory::foliageCard(kMedium, s + 29u); },
                leaves);

        Material hedge = materials_[static_cast<std::size_t>(MaterialId::Foliage)];
        hedge.name       = "hedge";
        hedge.baseColour = Vector3(0.72f, 0.86f, 0.66f);
        materials_[static_cast<std::size_t>(MaterialId::Hedge)] = hedge;
    }

    CNA::Logger::Info("cna-street: material library built -- "
                      + std::to_string(textures_.size()) + " textures, "
                      + std::to_string(textureBytes_ / (1024u * 1024u)) + " MiB");
}

}  // namespace CnaStreet
