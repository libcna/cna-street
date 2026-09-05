// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Scene/CityLayout.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <string>
#include <vector>

namespace CnaStreet {

class MaterialLibrary;
struct Material;

/// A façade's own coordinate system. Every feature on an elevation is placed in
/// `(u, v, depth)`: `u` runs along the wall from its left edge seen from
/// outside, `v` runs up from the pavement, and `depth` is out of the wall
/// toward the street. One frame, four elevations, and the window code never has
/// to know which way the building faces.
struct FacadeFrame
{
    Microsoft::Xna::Framework::Vector3 origin{0.0f, 0.0f, 0.0f};
    Microsoft::Xna::Framework::Vector3 right{1.0f, 0.0f, 0.0f};
    Microsoft::Xna::Framework::Vector3 up{0.0f, 1.0f, 0.0f};
    Microsoft::Xna::Framework::Vector3 out{0.0f, 0.0f, 1.0f};
    float width  = 10.0f;
    float height = 16.0f;

    [[nodiscard]] Microsoft::Xna::Framework::Vector3 at(float u, float v, float depth = 0.0f) const
    {
        return origin + right * u + up * v + out * depth;
    }
};

/// A hole in an elevation, in façade coordinates. Collected while the features
/// are placed and used afterwards to build the wall *around* them: a solid wall
/// with a recessed window drawn behind it shows no window at all, which is
/// exactly what the first version of this generator produced.
struct Opening
{
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
};

/// Where a shop sign, a street plate or a house number wants to go, worked out
/// while the façade is generated and used afterwards by the signage pass.
struct FacadeAnchor
{
    Microsoft::Xna::Framework::Vector3 position;
    Microsoft::Xna::Framework::Vector3 normal;
    float width = 1.0f;
    float height = 0.3f;
    /// DoorHead is the centre of a shop door's head, for a camera or a lamp;
    /// AirCon is a spot beside an upper window where a condenser unit hangs.
    enum class Kind { ShopFascia, StreetPlate, HouseNumber, Awning, DoorHead, AirCon }
        kind = Kind::ShopFascia;
    int plotIndex = 0;
};

/// What a shop unit sells. It decides the fittings, the products and the
/// lettering on the fascia, and it is chosen once per plot so the three cannot
/// disagree -- a shoe shop with a bread counter is worse than an empty one.
enum class ShopKind
{
    Bakery,
    Clothing,
    Convenience,
    Electrical,
    Florist,
    Optician,
    Furniture,
    Office,
    Vacant,
    Count
};

/// A place inside a shop window where one authored prop can stand, worked out
/// while the interior is generated. The scene fills these from the imported
/// glTF catalogue if it has one and leaves them empty if it does not.
struct ShopDisplay
{
    /// World transform of the display surface: origin at its centre, +Y up,
    /// +Z out toward the glass.
    Microsoft::Xna::Framework::Matrix stand;
    /// How much room the prop has, in metres.
    float span = 0.5f;
    ShopKind kind = ShopKind::Vacant;
    int plotIndex = 0;
};

/// An authored prop the hero shop wants standing somewhere: which scan, where,
/// and how tall to make it (0 keeps the authored size). Worked out by the
/// interior generator in the room's own frame; the scene loads and places it.
struct HeroProp
{
    std::string asset;
    Microsoft::Xna::Framework::Matrix at;
    float fitMetres = 0.0f;
    /// Fit the width rather than the height: a shelf unit is sized by how
    /// much wall it takes, a cake by how tall it stands.
    bool byWidth = false;
    /// How far this copy is drawn; the room is invisible from across the
    /// street and a croissant from further than the window.
    float cullDistance = 40.0f;
};

/**
 * @brief Turns a plot into a building.
 *
 * The generator is a description of how these buildings are actually put
 * together — plinth, shopfront, string course, window bays, cornice, roof —
 * rather than a box with a texture on it, because at eye level on a footway
 * that is the difference between a street and a corridor. Every window is a real
 * opening: a reveal cut back into the wall, an interior behind it, a frame, and
 * glass in front of the frame. That is what stops a façade of sixty windows
 * reading as sixty stickers.
 */
class BuildingBuilder
{
public:
    /// The library is taken non-const because a shop tints the room materials
    /// to its trade -- a bakery's walls are warm, an electrical shop's are
    /// dark -- and a tint is a derived material the library has to own.
    BuildingBuilder(MaterialLibrary& materials, const CityLayout& layout);

    /// Builds one plot into the collector. Anchors found along the way are
    /// appended to @p anchors, and any window display the shop unit wants to
    /// @p displays.
    /// @p interiors receives everything *behind* the glass. Kept apart from the
    /// building's own geometry because it wants different treatment in every
    /// respect: it is invisible past the width of the street, it can never cast
    /// a shadow anything outside can see, and batching it with the façade would
    /// mean a wall of shelving is drawn whenever the wall in front of it is.
    void build(const Plot& plot, int plotIndex, GeometryCollector& collector,
               GeometryCollector& interiors, Rng& rng, std::vector<FacadeAnchor>& anchors,
               std::vector<ShopDisplay>& displays);

    /// Makes plot @p index the hero shop: a bakery-cafe built as a composed
    /// room rather than a dressed box, whose scanned props are written to
    /// @p props for the scene to stand up. One shop on the street gets this;
    /// it is the one the close viewpoints look into.
    void setHeroShop(int index, std::vector<HeroProp>* props)
    {
        heroPlot_  = index;
        heroProps_ = props;
    }
    [[nodiscard]] static const char* heroShopName() { return "KAFFEEHAUS"; }

    /// The palette a plot's `colourIndex` selects from.
    [[nodiscard]] static int renderColourCount();

    /// What this plot's ground floor sells. Derived from the plot alone, so the
    /// fascia lettering and the fittings behind the glass agree.
    [[nodiscard]] static ShopKind shopKindFor(const Plot& plot, int plotIndex);
    [[nodiscard]] static const char* shopKindName(ShopKind kind);

private:
    struct Palette
    {
        const Material* wall      = nullptr;
        const Material* plinth    = nullptr;
        const Material* trim      = nullptr;   ///< cornices, string courses, sills
        const Material* frame     = nullptr;
        const Material* glass     = nullptr;
        const Material* interior  = nullptr;
        const Material* door      = nullptr;
        const Material* roof      = nullptr;
        const Material* metal     = nullptr;
        const Material* fascia    = nullptr;
        const Material* shopGlass = nullptr;
    };

    [[nodiscard]] Palette paletteFor(const Plot& plot, Rng& rng) const;
    [[nodiscard]] FacadeFrame frameFor(const Plot& plot, Facing facing) const;

    void buildMass(const Plot& plot, const Palette& palette, GeometryCollector& collector,
                   Rng& rng);
    void buildFacade(const Plot& plot, int plotIndex, const FacadeFrame& frame,
                     const Palette& palette, GeometryCollector& collector, Rng& rng,
                     GeometryCollector& interiors, std::vector<FacadeAnchor>& anchors,
                     std::vector<ShopDisplay>& displays, bool primary);
    /// Emits the wall as strips around @p openings. Rows are formed from
    /// openings whose vertical extents overlap, which is what keeps a façade
    /// with balcony doors on one storey and ordinary windows on the next from
    /// producing a wall full of slivers.
    void buildWallPanels(const FacadeFrame& frame, float from, float to,
                         std::vector<Opening> openings, const Palette& palette,
                         GeometryCollector& collector);
    void buildPlainWall(const FacadeFrame& frame, const Palette& palette,
                        GeometryCollector& collector, Rng& rng, bool addWindows);
    void buildWindow(const FacadeFrame& frame, float u, float v, float width, float height,
                     const Palette& palette, GeometryCollector& collector, Rng& rng,
                     bool arched, std::vector<Opening>& openings);
    void buildShopfront(const Plot& plot, int plotIndex, const FacadeFrame& frame,
                        const Palette& palette, GeometryCollector& collector, Rng& rng,
                        GeometryCollector& interiors, std::vector<FacadeAnchor>& anchors,
                        std::vector<ShopDisplay>& displays, std::vector<Opening>& openings);
    /// The room behind a shopfront. Without it the glazing is a hole straight
    /// through the building to the sky beyond, because the rear elevation's only
    /// face points outward and is back-face culled from inside.
    ///
    /// The fittings depend on @p kind, because a shop window is looked *into*
    /// and an empty white box behind glass is the loudest thing on a street.
    void buildShopInterior(const FacadeFrame& frame, float u0, float u1, float floor, float ceiling,
                           ShopKind kind, int plotIndex, GeometryCollector& collector, Rng& rng,
                           std::vector<ShopDisplay>* displays);
    /// The hero bakery-cafe: a deeper room with a serving counter under a
    /// glass display case, shelving of bread, a coffee station, a back door
    /// into a darker store, pendant lamps, and the window dressed with the
    /// things a bakery puts in a window. The scanned props are anchored here
    /// and placed by the scene.
    void buildHeroBakery(const FacadeFrame& frame, float u0, float u1, float floor,
                         float ceiling, int plotIndex, GeometryCollector& collector, Rng& rng,
                         std::vector<ShopDisplay>* displays);
    void buildEntrance(const FacadeFrame& frame, float u, float sillHeight,
                       const Palette& palette, GeometryCollector& collector, Rng& rng,
                       std::vector<Opening>& openings);
    void buildBalcony(const FacadeFrame& frame, float u, float v, float width,
                      const Palette& palette, GeometryCollector& collector, Rng& rng);
    void buildRoof(const Plot& plot, const Palette& palette, GeometryCollector& collector,
                   Rng& rng);
    void buildRainwaterGoods(const Plot& plot, const FacadeFrame& frame, const Palette& palette,
                             GeometryCollector& collector);

    /// Lays a weathering decal a few millimetres over the wall: rain run-off
    /// under a sill, the dirt band at the foot of a wall, the stain beside a
    /// downpipe. `cell` picks one of the four in the atlas; `strength` scales
    /// the chance and the size, and comes from the plot's own weathering so
    /// one building is grubby and its neighbour freshly painted.
    void grimeDecal(const FacadeFrame& frame, float u0, float v0, float u1, float v1, int cell,
                    GeometryCollector& collector, bool flipU = false);

    MaterialLibrary&  materials_;
    const CityLayout& layout_;
    /// The plot being built, for the parts of a building that are decided by
    /// the whole rather than by the elevation: how weathered it is, whether
    /// its shop is open.
    float weathering_ = 0.5f;
    std::uint32_t plotSeed_ = 0;
    /// A flat, post-war or contemporary elevation, whose windows get a
    /// projecting surround rather than a classical sill and architrave.
    bool flatFacade_ = false;
    BuildingStyle style_ = BuildingStyle::Gruenderzeit;
    /// Decided once per plot from its seed, so every elevation agrees:
    /// whether the rendered block carries folding shutters beside its
    /// windows, and whether the masonry block has quoins up its corners.
    bool shutters_ = false;
    bool quoins_ = false;
    /// What a window shows of the room behind it, besides the room: a net
    /// curtain, a roller blind, or a room with the light off. Derived once.
    /// The stream the window dressing draws from, restarted per elevation
    /// from the plot's seed: its own, so that curtains and shutters added
    /// to a window do not re-deal the balconies, the brick colour and every
    /// ornament of every plot after it.
    Rng dressing_{1u};
    const Material* netCurtain_   = nullptr;
    const Material* windowBlind_  = nullptr;
    const Material* interiorDark_ = nullptr;
    int heroPlot_ = -1;
    std::vector<HeroProp>* heroProps_ = nullptr;
};

}  // namespace CnaStreet
