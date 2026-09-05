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

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

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
/// The carriageway alone, at four times the pixels of everything else.
///
/// The road is the largest surface in every frame and the one the camera gets
/// closest to, and at 512 over a six-metre tile it had 1.2 cm per texel -- so
/// the 8-16 mm aggregate that asphalt *is* could not be drawn at all, whatever
/// the generator did, and what survived was the coarse noise on top of it. A
/// 1024 map on a five-metre tile is 0.49 cm per texel: a 3 cm chip is six
/// texels across and the surface finally reads as asphalt rather than as dark
/// grey with weather on it. It costs about 24 MiB, spent where a sixth of the
/// pixels in the picture are.
constexpr int kRoad   = 1024;
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

    material.name = name;
    // Only a map that was handed over replaces what the material already
    // carries. An imported model arrives with its textures already on the
    // GPU, borrowed off the effect the importer built, and passes empty maps;
    // the first version of this overwrote every one of those pointers with
    // the null an empty upload returns, so every imported prop in the scene
    // drew as flat white and its 4K texture set was loaded for nothing.
    if (!maps.albedo.empty())   material.albedo   = upload(maps.albedo, true, name + ".albedo");
    if (!maps.normal.empty())   material.normal   = upload(maps.normal, false, name + ".normal");
    if (!maps.orm.empty())      material.orm      = upload(maps.orm, false, name + ".orm");
    if (!maps.emissive.empty()) material.emissive = upload(maps.emissive, true, name + ".emissive");
    auto owned = std::make_unique<Material>(material);
    Material* raw = owned.get();
    derived_.emplace(name, std::move(owned));
    return raw;
}

const Material* MaterialLibrary::addFromContent(const std::string& name, const std::string& albedo,
                                                const std::string& normal, Material material)
{
    const auto existing = derived_.find(name);
    if (existing != derived_.end()) return existing->second.get();
    material.name   = name;
    material.albedo = albedo.empty() ? nullptr : load(albedo);
    if (material.albedo == nullptr) return nullptr;
    material.normal = normal.empty() ? nullptr : load(normal);
    material.orm    = nullptr;
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

void MaterialLibrary::measureNominals(const std::string& name, const Assets::Image& orm)
{
    double sumG = 0.0, sumB = 0.0;
    int    samples = 0;
    for (int y = 0; y < orm.height(); y += 2)
        for (int x = 0; x < orm.width(); x += 2)
        {
            const float* px = orm.at(x, y);
            sumG += static_cast<double>(px[1]);
            sumB += static_cast<double>(px[2]);
            ++samples;
        }
    if (samples == 0) return;
    nominalRoughness_[name] = static_cast<float>(sumG / samples);
    nominalMetallic_[name]  = static_cast<float>(sumB / samples);
}

void MaterialLibrary::writeNominals(const std::string& directory) const
{
    // The nominals travel with the compiled content, because the whole point of
    // the content root is that the generators are never run -- and without them
    // a content-backed start-up would silently keep the squared factors that
    // normaliseFactors exists to remove. One line per surface, written by the
    // same code that wrote the images so the two cannot disagree.
    std::ofstream out(std::filesystem::path(directory) / "surfaces.txt");
    if (!out) return;
    out << "# cna-street surface nominals: name <tab> mean ORM roughness <tab> mean ORM\n"
           "# metalness, as written by the generator. MaterialLibrary divides each\n"
           "# material's declared factor by these so that PbrEffect's factor-times-map\n"
           "# product averages what the material asked for. Regenerated by the content\n"
           "# build; delete it and the app warns rather than rendering the wrong gloss.\n";
    std::vector<std::string> names;
    names.reserve(nominalRoughness_.size());
    for (const auto& entry : nominalRoughness_) names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    for (const std::string& name : names)
    {
        const auto metal = nominalMetallic_.find(name);
        out << name << '\t' << nominalRoughness_.at(name) << '\t'
            << (metal == nominalMetallic_.end() ? 0.0f : metal->second) << '\n';
    }
}

void MaterialLibrary::loadNominals()
{
    if (nominalsLoaded_) return;
    nominalsLoaded_ = true;
    if (content_ == nullptr) return;
    std::ifstream in(std::filesystem::path(content_->getRootDirectoryProperty())
                     / "surfaces.txt");
    if (!in)
    {
        CNA::Logger::Warn("cna-street: the content root has no surfaces.txt, so the roughness "
                          "and metalness factors cannot be normalised against their maps; "
                          "rebuild the content target");
        return;
    }
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string name;
        float roughness = 1.0f, metallic = 0.0f;
        if (!(fields >> name >> roughness >> metallic)) continue;
        nominalRoughness_[name] = roughness;
        nominalMetallic_[name]  = metallic;
    }
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
        const SurfaceMaps maps = generate();
        measureNominals(name, maps.orm);
        bake(name, maps);
        materials_[static_cast<std::size_t>(id)] = std::move(material);
        return;
    }

    if (installFromContent(material, name))
    {
        ++loadedCount_;
        loadAuthored();
        const auto authored = authored_.find(name);
        if (authored != authored_.end())
        {
            // A scanned set. Its roughness and metalness maps carry measured
            // values, so the factors are 1 and the map is the answer; the
            // catalogue's declared roughness described the generator this scan
            // replaced. The UV scale maps the geometry's tile onto the scan's
            // physical size, and a scan that was not neutralised for tinting
            // carries its own colour, so the tint comes off.
            material.roughness = 1.0f;
            material.metallic  = 1.0f;
            material.uvScale   = authored->second.uvScale;
            if (!authored->second.keepTint) material.baseColour = Vector3::One;
            ++authoredInstalled_;
            materials_[static_cast<std::size_t>(id)] = std::move(material);
            return;
        }
        // The maps came from the content root, so there is no CPU-side image to
        // measure; the nominals came with them.
        loadNominals();
        applyNominals(material);
        materials_[static_cast<std::size_t>(id)] = std::move(material);
        return;
    }

    const SurfaceMaps maps = generate();
    measureNominals(name, maps.orm);
    applyNominals(material);
    material.albedo = upload(maps.albedo, true, name + ".albedo");
    material.normal = upload(maps.normal, false, name + ".normal");
    material.orm    = upload(maps.orm, false, name + ".orm");
    if (maps.hasEmissive()) material.emissive = upload(maps.emissive, true, name + ".emissive");
    ++generatedCount_;
    materials_[static_cast<std::size_t>(id)] = std::move(material);
}

void MaterialLibrary::applyNominals(Material& material)
{
    // `PbrEffect` computes `roughness = map.g * roughnessFactor` and
    // `metalness = map.b * metallicFactor`, which is glTF's rule and the right
    // one. This catalogue was violating it everywhere, and in the most
    // plausible way there is: the generator is handed the surface's roughness,
    // writes it into the map with its own variation around it, and then the
    // material declares the *same number* as the factor. The product is the
    // square. Asphalt asked for 0.88 twice and got 0.76; painted metal asked
    // for 0.38 twice and got 0.16, which is a gloss lacquer, and every lamp
    // post, bin, bollard, sign back and window frame in the city was lacquered.
    // A road sign asked for 0.32 and got 0.12. A wheel track asked for 0.58 and
    // got 0.39, which is why the carriageway looked wet.
    //
    // Metalness was worse, because it failed silently in the other direction: a
    // material that declares itself metal over a map whose blue channel is zero
    // comes out a dielectric however emphatic the declaration. Every galvanised
    // post and every alloy wheel in the scene was plastic, and a car's metallic
    // basecoat had its flake multiplied out by a factor of zero.
    //
    // So: the declaration is the intent, and the map is where the variation
    // lives. This divides the declared value by what the generator actually
    // wrote, so the product *averages* the declared number and keeps every bit
    // of the map's spatial detail in proportion. Both readings of the old code
    // agree on this being the answer -- it is what the author meant either way.
    const auto roughness = nominalRoughness_.find(material.name);
    const auto metalness = nominalMetallic_.find(material.name);
    if (roughness == nominalRoughness_.end()) return;

    const float meanG = roughness->second;
    const float meanB = metalness == nominalMetallic_.end() ? 0.0f : metalness->second;

    if (meanG > 1.0e-3f) material.roughness /= meanG;
    if (material.metallic > 1.0e-3f)
    {
        if (meanB > 1.0e-3f) material.metallic /= meanB;
        else
            // A declaration with nothing in the map to scale. Left alone rather
            // than quietly forced, and said out loud, because the fix belongs
            // in the generator: write the metalness the surface has and let the
            // factor pick a value off it.
            CNA::Logger::Warn("cna-street: material '" + material.name + "' declares metalness "
                              + std::to_string(material.metallic)
                              + " but its ORM map carries none; it will render as a dielectric");
    }
}

void MaterialLibrary::loadAuthored()
{
    if (authoredLoaded_) return;
    authoredLoaded_ = true;
    if (content_ == nullptr) return;
    std::ifstream in(std::filesystem::path(content_->getRootDirectoryProperty())
                     / "authored.txt");
    if (!in) return;   // no scanned surfaces in this content build, which is fine
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string name;
        float u = 1.0f, v = 1.0f;
        int keepTint = 0;
        if (!(fields >> name >> u >> v)) continue;
        fields >> keepTint;
        Authored entry;
        entry.uvScale  = Vector2(u, v);
        entry.keepTint = keepTint != 0;
        authored_[name] = entry;
    }
    if (!authored_.empty())
        CNA::Logger::Info("cna-street: the content root carries "
                          + std::to_string(authored_.size()) + " scanned surface(s)");
}

bool MaterialLibrary::isAuthored(const std::string& name) const
{
    const auto found = authored_.find(name);
    return found != authored_.end();
}

float MaterialLibrary::nominalRoughnessOf(const std::string& name) const
{
    const auto found = nominalRoughness_.find(name);
    return found == nominalRoughness_.end() ? 1.0f : std::max(found->second, 1.0e-3f);
}

float MaterialLibrary::nominalMetallicOf(const std::string& name) const
{
    const auto found = nominalMetallic_.find(name);
    return found == nominalMetallic_.end() ? 1.0f : std::max(found->second, 1.0e-3f);
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
                [&] { return TextureFactory::asphalt(kRoad, s + 1u, 0.55f); }, asphalt);
        asphalt.roughness = 0.90f;
        install(MaterialId::AsphaltSide, "asphalt-side",
                [&] { return TextureFactory::asphalt(kLarge, s + 2u, 0.32f); }, asphalt);
        asphalt.roughness = 0.92f;
        install(MaterialId::AsphaltWorn, "asphalt-worn",
                [&] { return TextureFactory::asphalt(kRoad, s + 3u, 0.92f); }, asphalt);
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
        // Engineering brick: the red brick's texture again, darkened by its
        // tint, when generated -- and a scan of its own when the content root
        // has one, which is why it is installed rather than copied.
        Material engineering = pbr(0.66f, 0.0f);
        engineering.baseColour = Vector3(0.52f, 0.46f, 0.46f);
        install(MaterialId::BrickEngineering, "brick-engineering",
                [&] { return TextureFactory::brick(kLarge, s + 11u, 0.08f, 0.55f); }, engineering);
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

        // Joinery stops casting at 55 m.
        //
        // The window frames are the heaviest single family in the scene -- a
        // hundred thousand triangles of mitred sections and glazing bars across
        // nineteen elevations -- and a frame's shadow is a 20 mm line, worth
        // having on the reveal beside it and under a shadow texel by the second
        // cascade. The wall it sits in still casts to the full distance, so the
        // street stays in the shade of its buildings.
        //
        // Worth being honest about how much this buys, which is: very little
        // here, six draw calls. One plot's frames are *one batch* spanning a
        // whole elevation, and a distance test against a batch takes the
        // nearest corner of it -- so an elevation running from twenty metres to
        // sixty is kept whole, at sixty metres' worth of triangles, because
        // twenty metres of it is close. Per-batch distance culling can only be
        // as fine as the batches, and these are coarse on purpose: the
        // alternative is a draw call per window. The cap earns its place on the
        // plots at the far end of the street rather than on the near ones, and
        // it is the right *mechanism* -- but the frame joinery is not where the
        // shadow pass's time goes, and pretending otherwise would be the kind
        // of claim this file is meant not to make.
        for (const MaterialId id : {MaterialId::FrameWhite, MaterialId::FrameDark,
                                    MaterialId::FrameBronze, MaterialId::CarTrim})
            materials_[static_cast<std::size_t>(id)].shadowDistance = 55.0f;
    }

    {
        // Weathering decals: soft-edged, so blended rather than masked, and
        // never a shadow caster or a depth writer -- they are a film on the
        // wall, not a thing in front of it.
        Material grime = pbr(0.96f, 0.0f);
        grime.alphaMode   = AlphaModeEXT::Blend;
        grime.alpha       = 0.62f;
        grime.castsShadow = false;
        grime.writesDepth = false;
        install(MaterialId::FacadeGrime, "facade-grime",
                [&] { return TextureFactory::grimeDecals(kMedium, s + 68u); }, grime);
    }

    install(MaterialId::Ashlar, "ashlar", [&] { return TextureFactory::ashlar(kLarge, s + 14u, 0.55f); },
            pbr(0.82f, 0.0f));
    {
        Material panel = pbr(0.74f, 0.0f);
        panel.baseColour = Vector3(0.72f, 0.72f, 0.70f);
        install(MaterialId::ConcretePanel, "concrete-panel",
                [&] { return TextureFactory::concretePaving(kLarge, s + 15u, 3.0f); }, panel);
        // Precast cladding is a large flat panel, not paving: keep the aggregate
        // and weathering but scale the joints out of sight with a large tile.
        // Divided by what the generator wrote, because this is set *after*
        // install and so bypasses the normalisation there.
        if (!isAuthored("concrete-panel"))
            materials_[static_cast<std::size_t>(MaterialId::ConcretePanel)].roughness =
                0.70f / nominalRoughnessOf("concrete-panel");

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
        // Glass is a reflection layer over whatever stands behind it, and it
        // is blended as one: `out = reflection + behind * (1 - alpha)`, see
        // `Material::premultipliedBlend`. So the base colour here is the tint
        // of the *reflection layer* -- near black, because clear glass has no
        // diffuse of its own and the grime on it is what little there is --
        // and the alpha is how much the pane blocks of the room behind it,
        // not how much it shows of itself. The version this replaces had a
        // pale (0.72, 0.78, 0.76) base at alpha 0.34 under the ordinary blend,
        // which painted a milky filter over every interior and multiplied the
        // Fresnel reflection by 0.34 into invisibility: not one window on the
        // street reflected anything.
        Material glass = pbr(0.06f, 0.0f);
        glass.alphaMode   = AlphaModeEXT::Blend;
        glass.premultipliedBlend = true;
        glass.alpha       = 0.22f;
        glass.baseColour  = Vector3(0.050f, 0.056f, 0.054f);
        glass.ior         = 1.52f;
        glass.specular    = 1.0f;
        glass.castsShadow = false;   // a pane casting an opaque shadow is a tell
        glass.writesDepth = false;
        install(MaterialId::Glazing, "glazing", [&] { return TextureFactory::windowGlass(kMedium, s + 19u); },
                glass);

        Material shop = materials_[static_cast<std::size_t>(MaterialId::Glazing)];
        shop.name       = "shop-glazing";
        // Large, clean, frequently cleaned panes: they block almost nothing
        // and their reflection is a mirror of the street.
        shop.alpha      = 0.14f;
        // Divided by the glazing generator's own value: this material shares
        // that surface's textures and never passes through normaliseFactors.
        shop.roughness  = 0.035f / nominalRoughnessOf("glazing");
        shop.baseColour = Vector3(0.030f, 0.034f, 0.033f);
        materials_[static_cast<std::size_t>(MaterialId::ShopGlazing)] = shop;
    }
    {
        // How much sky a surface inside a shop can see, written into the
        // occlusion channel of its ORM map.
        //
        // A room behind a shopfront is enclosed, and this renderer has no
        // room-scale occlusion: SSAO is a contact effect measured in
        // centimetres, and the cascades only carry what the sun casts. So an
        // interior surface takes the *whole* sky ambient and the whole IBL, as
        // though the walls around it were not there, and a shop interior comes
        // out the same brightness as the pavement outside it. That was the
        // single loudest thing about the first version of these rooms: a wall
        // of milky white behind every pane of glass.
        //
        // Occlusion is exactly the right lever. `PbrEffect` multiplies both
        // ambient terms by it and leaves direct light alone, so a low value
        // says "this face sees very little sky" without touching the ceiling
        // strip that is actually lighting the room.
        //
        // The fractions are graded by how deep into the room each surface
        // lives, which is the nearest a tiling material can get to the gradient
        // a real shopfront has: the plinth and the front fittings sit a metre
        // behind a wall of glass and see a great deal of sky, the rear wall
        // sees almost none. A retail interior is also *lit*, brightly and on
        // purpose, so none of these is as dark as a domestic room behind the
        // same glass would be.
        // The second half of the same problem is *colour*. Having correctly
        // decided that a shop interior sees almost no sky, the room is then lit
        // by what little sky it does see -- and this street's sky ambient is
        // (0.26, 0.40, 0.59), strongly blue. Every packet on every shelf came
        // out a shade of navy, and the hue wheel the stock texture goes to the
        // trouble of drawing was invisible under it.
        //
        // A real shop is lit by warm fluorescent tubes and lit brightly. The
        // ceiling strips here are emissive geometry, which glows but emits
        // nothing, and `PbrEffect` carries one punctual light per draw -- not
        // thirty-nine. So the room's light is *baked*, which is what a light map
        // is and has always been: each interior surface gets an emissive map
        // that is a copy of its own albedo, and a warm emissive factor scaled
        // by how much of the ceiling that surface can see. `factor * albedo` is
        // exactly "warm light bouncing off this colour", so a red packet stays
        // red and a green one green, and the stock reads as assorted packaging
        // instead of as a wall of denim.
        const auto enclose = [](Assets::SurfaceMaps maps, float sky, float lit) {
            maps.orm.forEach([sky](int, int, float* px) { px[0] *= sky; });
            if (lit > 0.0f)
            {
                maps.emissive = maps.albedo;
                // Opaque: the alpha of an emissive map is not read, and leaving
                // the albedo's mask in it is a trap for whoever reads this next.
                maps.emissive.forEach([](int, int, float* px) { px[3] = 1.0f; });
            }
            return maps;
        };
        /// The warm white of a retail fluorescent, as an emissive multiplier.
        const auto shopLight = [](float strength) {
            return Vector3(1.00f, 0.93f, 0.80f) * strength;
        };

        Material interior = pbr(0.90f, 0.0f);
        interior.emissiveFactor = Vector3(1.0f, 1.0f, 1.0f);
        install(MaterialId::Interior, "interior",
                [&] { return TextureFactory::interiorAtlas(kLarge, s + 20u); }, interior);

        // Shop fittings: the painted MDF and powder-coated steel that shelving,
        // counters and display plinths are actually made of. Pale, because a
        // shop interior is a bright box and dark fittings behind glass vanish.
        Material fitting = pbr(0.62f, 0.0f);
        fitting.baseColour = Srgb(212, 209, 203);
        // A shelf unit stands under the tubes and takes the most of them.
        fitting.emissiveFactor = shopLight(0.24f);
        float fittingRgb[3];
        Rgb(fittingRgb, fitting.baseColour);
        install(MaterialId::ShopFitting, "shop-fitting",
                [&] {
                    return enclose(TextureFactory::paintedMetal(kSmall, s + 61u, fittingRgb, 0.62f),
                                   0.34f, 1.0f);
                },
                fitting);

        // The product on the shelves. One noisy, saturated surface for all of
        // it: at the distance a shop window is looked into, stock is colour and
        // nothing else, and giving each shop its own palette would cost sixteen
        // textures to say something nobody can resolve.
        Material stock = pbr(0.68f, 0.0f);
        // A third less glow than the first version: lit stock through glass
        // in daylight read as a wall of confetti, and product is darker than
        // the wall behind it, not brighter.
        stock.emissiveFactor = shopLight(0.20f);
        install(MaterialId::ShopStock, "shop-stock",
                [&] { return enclose(TextureFactory::shopStock(kMedium, s + 62u), 0.36f, 1.0f); }, stock);

        // A lit ceiling panel. Emissive rather than a real light: a punctual
        // light per shop would be forty of them in the clustered set to
        // illuminate a room nobody stands in, and what the street actually sees
        // is the *brightness* of the soffit through the glass.
        Material ceiling = pbr(0.55f, 0.0f);
        ceiling.baseColour     = Vector3(0.92f, 0.90f, 0.86f);
        // Bright enough to read as a lit tube through the glass, not so
        // bright that the bloom turns the soffit into a white bar: at 1.75
        // every shop on the street was a light box first and a room second.
        ceiling.emissiveFactor = Vector3(1.20f, 1.08f, 0.90f);
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
        shopWall.baseColour  = Srgb(178, 174, 166);
        shopWall.emissiveFactor = shopLight(0.10f);
        shopWall.castsShadow = false;
        const float shopWallRgb[3] = {1.0f, 1.0f, 1.0f};
        // Painted plasterboard, not exterior render. The plaster generator's
        // mottle and its rain streaks are right on a wall that has weather on
        // it and wrong on a lit interior, where at this brightness they read as
        // flies on the paintwork.
        install(MaterialId::ShopWall, "shop-wall",
                [&] {
                    return enclose(TextureFactory::paintedMetal(kMedium, s + 63u, shopWallRgb,
                                                                0.86f),
                                   0.20f, 1.0f);
                },
                shopWall);

        Material shopFloor = pbr(0.55f, 0.0f);
        shopFloor.baseColour  = Srgb(168, 164, 158);
        shopFloor.emissiveFactor = shopLight(0.11f);
        shopFloor.castsShadow = false;
        install(MaterialId::ShopFloor, "shop-floor",
                [&] { return enclose(TextureFactory::concretePaving(kMedium, s + 64u, 8.0f), 0.22f, 1.0f); },
                shopFloor);

        Material shopTimber = pbr(0.52f, 0.0f);
        shopTimber.emissiveFactor = shopLight(0.15f);
        shopTimber.castsShadow = false;
        install(MaterialId::ShopTimber, "shop-timber",
                [&] { return enclose(TextureFactory::hardwood(kSmall, s + 65u), 0.28f, 1.0f); },
                shopTimber);

        Material shopScreen = pbr(0.16f, 0.0f);
        shopScreen.baseColour  = Srgb(16, 17, 20);
        shopScreen.castsShadow = false;
        float screenRgb[3];
        Rgb(screenRgb, shopScreen.baseColour);
        install(MaterialId::ShopScreen, "shop-screen",
                [&] { return TextureFactory::flat(8, screenRgb, 0.16f, 0.0f); }, shopScreen);

        // Posters on the back wall, lit like the wall they hang on.
        Material poster = pbr(0.52f, 0.0f);
        poster.emissiveFactor = shopLight(0.13f);
        poster.castsShadow = false;
        install(MaterialId::ShopPoster, "shop-poster",
                [&] { return enclose(TextureFactory::posters(kMedium, s + 66u), 0.22f, 1.0f); },
                poster);

        // A roller blind, drawn down behind the glass of a shop that is shut.
        // Pale, matte and lit from the street rather than from inside.
        Material blind = pbr(0.82f, 0.0f);
        blind.baseColour = Srgb(198, 190, 176);
        blind.castsShadow = false;
        const float blindRgb[3] = {1.0f, 1.0f, 1.0f};
        install(MaterialId::ShopBlind, "shop-blind",
                [&] { return TextureFactory::fabric(kSmall, s + 67u, blindRgb); }, blind);

        materials_[static_cast<std::size_t>(MaterialId::ShopFitting)].castsShadow = false;
        materials_[static_cast<std::size_t>(MaterialId::ShopStock)].castsShadow = false;
        materials_[static_cast<std::size_t>(MaterialId::ShopCeilingLight)].castsShadow = false;
        // Nothing in a room is lit by the sun. See `Material::sunlit`.
        for (const MaterialId id : {MaterialId::ShopFitting, MaterialId::ShopStock,
                                    MaterialId::ShopCeilingLight, MaterialId::ShopWall,
                                    MaterialId::ShopFloor, MaterialId::ShopTimber,
                                    MaterialId::ShopScreen, MaterialId::ShopPoster})
            materials_[static_cast<std::size_t>(id)].sunlit = false;
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
        const float paintedRoughness = nominalRoughnessOf("painted");
        const float paintedMetallic  = nominalMetallicOf("painted");

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
            // Every one of these shares the painted-metal surface and differs
            // only in colour, roughness and metalness, so each has to divide by
            // what that generator wrote -- they are copies, not installs.
            m.roughness  = tint.roughness / paintedRoughness;
            m.metallic   = tint.metal / paintedMetallic;
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
        // Reflection plus attenuated cabin, like every other pane in the
        // scene. Tinted automotive glass blocks about half of what is behind
        // it at street angles; the rest of what reaches the eye is the
        // reflection, now at its full Fresnel strength.
        glass.premultipliedBlend = true;
        glass.alpha       = 0.55f;
        glass.baseColour  = Vector3(0.016f, 0.018f, 0.020f);
        glass.ior         = 1.52f;
        glass.specular    = 1.0f;
        glass.castsShadow = false;
        glass.writesDepth = false;
        install(MaterialId::CarGlass, "car-glass",
                [&] { return TextureFactory::vehicleGlass(kSmall, s + 25u); }, glass);

        float tyreRgb[3] = {0.026f, 0.026f, 0.028f};
        install(MaterialId::CarTyre, "car-tyre", [&] { return TextureFactory::flat(8, tyreRgb, 0.92f, 0.0f); },
                pbr(0.92f, 0.0f));

        // A glossy red lens with a little glow in it even by day, so the
        // cluster reads as a lamp and not as red paint.
        Material rear = pbr(0.12f, 0.0f);
        rear.baseColour     = Srgb(150, 22, 20);
        rear.emissiveFactor = Vector3(0.22f, 0.012f, 0.008f);
        float rearRgb[3];
        Rgb(rearRgb, rear.baseColour);
        install(MaterialId::CarLightRear, "car-light-rear",
                [&] { return TextureFactory::flat(8, rearRgb, 0.12f, 0.0f); }, rear);

        // A headlamp is a clear lens over a chromed reflector bowl: from
        // outside it is dark silver with a bright highlight in it, not a white
        // panel. Half metal at a low roughness, so what it shows is a
        // reflection of the street, and pale (0.82) it was a white sticker.
        Material front = pbr(0.16f, 0.5f);
        front.baseColour = Vector3(0.44f, 0.46f, 0.49f);
        float frontRgb[3];
        Rgb(frontRgb, front.baseColour);
        install(MaterialId::CarLightFront, "car-light-front",
                [&] { return TextureFactory::flat(8, frontRgb, 0.16f, 0.5f); }, front);

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

        // The lamp pools. Blended and depth-less: they are laid on the road and
        // the footway a centimetre above the surface, and a pool that wrote
        // depth would cut a hole in whatever crossed it.
        Material pool = pbr(1.0f, 0.0f);
        pool.alphaMode      = AlphaModeEXT::Blend;
        pool.baseColour     = Vector3::Zero;
        pool.emissiveFactor = Vector3(1.35f, 1.20f, 0.92f);
        pool.castsShadow    = false;
        pool.writesDepth    = false;
        install(MaterialId::LightPool, "light-pool",
                [&] { return TextureFactory::lightPool(kMedium, s + 91u); }, pool);

        Material hedge = materials_[static_cast<std::size_t>(MaterialId::Foliage)];
        hedge.name       = "hedge";
        hedge.baseColour = Vector3(0.72f, 0.86f, 0.66f);
        materials_[static_cast<std::size_t>(MaterialId::Hedge)] = hedge;
    }

    if (!bakeDirectory_.empty()) writeNominals(bakeDirectory_);

    CNA::Logger::Info("cna-street: material library built -- "
                      + std::to_string(textures_.size()) + " textures, "
                      + std::to_string(textureBytes_ / (1024u * 1024u)) + " MiB");
}

}  // namespace CnaStreet
