// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Core/Rng.hpp"
#include "CnaStreet/Scene/CityLayout.hpp"
#include "CnaStreet/Scene/GeometryCollector.hpp"

#include "Microsoft/Xna/Framework/Vector3.hpp"

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
    enum class Kind { ShopFascia, StreetPlate, HouseNumber, Awning } kind = Kind::ShopFascia;
    int plotIndex = 0;
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
    BuildingBuilder(const MaterialLibrary& materials, const CityLayout& layout);

    /// Builds one plot into the collector. Anchors found along the way are
    /// appended to @p anchors.
    void build(const Plot& plot, int plotIndex, GeometryCollector& collector, Rng& rng,
               std::vector<FacadeAnchor>& anchors);

    /// The palette a plot's `colourIndex` selects from.
    [[nodiscard]] static int renderColourCount();

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
                     std::vector<FacadeAnchor>& anchors, bool primary);
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
                        std::vector<FacadeAnchor>& anchors, std::vector<Opening>& openings);
    /// The room behind a shopfront. Without it the glazing is a hole straight
    /// through the building to the sky beyond, because the rear elevation's only
    /// face points outward and is back-face culled from inside.
    void buildShopInterior(const FacadeFrame& frame, float u0, float u1, float floor, float ceiling,
                           const Palette& palette, GeometryCollector& collector, Rng& rng);
    void buildEntrance(const FacadeFrame& frame, float u, float sillHeight,
                       const Palette& palette, GeometryCollector& collector, Rng& rng,
                       std::vector<Opening>& openings);
    void buildBalcony(const FacadeFrame& frame, float u, float v, float width,
                      const Palette& palette, GeometryCollector& collector, Rng& rng);
    void buildRoof(const Plot& plot, const Palette& palette, GeometryCollector& collector,
                   Rng& rng);
    void buildRainwaterGoods(const Plot& plot, const FacadeFrame& frame, const Palette& palette,
                             GeometryCollector& collector);

    const MaterialLibrary& materials_;
    const CityLayout&      layout_;
};

}  // namespace CnaStreet
