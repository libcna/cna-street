// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Assets/Image.hpp"

#include <cstdint>

namespace CnaStreet::Assets {

/**
 * @brief Generates every surface in the city from noise.
 *
 * There is no downloaded texture anywhere in this project, and that is a
 * deliberate architectural choice rather than a licensing dodge. A generated
 * surface can be produced at any resolution, tiles by construction, has a
 * physically sensible roughness map instead of a guessed one, and — because it
 * comes from a seed — is byte-identical on every machine that builds the repo.
 * The trade is that it must be *authored*: each function below is a description
 * of what the real material looks like, not a call to a noise library.
 *
 * All maps come back as a @ref SurfaceMaps triple in linear light. Sizes are
 * powers of two; the physical size each one covers is a property of the
 * material, not of the image, and lives in `MaterialLibrary`.
 */
class TextureFactory
{
public:
    /// Asphalt wearing course. @p wear from 0 (recently laid) to 1 (tired):
    /// drives crack density, patch count, oil staining and aggregate exposure.
    [[nodiscard]] static SurfaceMaps asphalt(int size, std::uint32_t seed, float wear);

    /// The polished band a tyre leaves in a travel lane: dark, smoother than the
    /// asphalt around it, and alpha-masked so its edge is a diffuse boundary
    /// rather than a stencil cut. Laid as a decal along a lane, because which
    /// way the traffic runs is a property of the road, not of the material.
    [[nodiscard]] static SurfaceMaps wheelTrack(int size, std::uint32_t seed);

    /// Cast concrete paving slabs, 3x3 to the tile.
    [[nodiscard]] static SurfaceMaps concretePaving(int size, std::uint32_t seed);

    /// Small granite setts, as laid in the gutter channel and at crossings.
    [[nodiscard]] static SurfaceMaps graniteSetts(int size, std::uint32_t seed);

    /// Sawn granite kerb stone: one long face, so the pattern runs lengthwise.
    [[nodiscard]] static SurfaceMaps graniteKerb(int size, std::uint32_t seed);

    /// Tactile paving — the blister surface at a dropped kerb.
    [[nodiscard]] static SurfaceMaps tactilePaving(int size, std::uint32_t seed);

    /// Fired clay brick in running bond. @p hue shifts red (0) to buff (1).
    [[nodiscard]] static SurfaceMaps brick(int size, std::uint32_t seed, float hue, float grime);

    /// Painted lime render over masonry. @p colour is the linear base colour.
    [[nodiscard]] static SurfaceMaps plaster(int size, std::uint32_t seed, const float colour[3],
                                             float grime);

    /// Coursed limestone ashlar, as on the older corner blocks.
    [[nodiscard]] static SurfaceMaps ashlar(int size, std::uint32_t seed, float grime);

    /// Clay pantiles.
    [[nodiscard]] static SurfaceMaps roofTile(int size, std::uint32_t seed);

    /// Bitumen membrane on a flat roof, with its gravel ballast.
    [[nodiscard]] static SurfaceMaps roofFelt(int size, std::uint32_t seed);

    /// Zinc/lead sheet — parapet cappings, dormer cheeks, downpipes.
    [[nodiscard]] static SurfaceMaps sheetMetal(int size, std::uint32_t seed);

    /// Painted steel or aluminium. Used for signal housings, poles, railings.
    [[nodiscard]] static SurfaceMaps paintedMetal(int size, std::uint32_t seed,
                                                  const float colour[3], float roughness);

    /// Automotive paint: a smooth, clear-coated colour with faint orange peel.
    [[nodiscard]] static SurfaceMaps carPaint(int size, std::uint32_t seed, const float colour[3],
                                              float metallic);

    /// Window glass: the dirt, streaks and reflection breakup that stop a pane
    /// from reading as a hole.
    [[nodiscard]] static SurfaceMaps windowGlass(int size, std::uint32_t seed);

    /// Automotive glazing: tinted nearly black, with a wiped arc across it.
    /// Not the same surface as a shop window and never was.
    [[nodiscard]] static SurfaceMaps vehicleGlass(int size, std::uint32_t seed);

    /// What is behind the glass. A row-and-column grid of rooms, some lit, some
    /// curtained, some empty — the cheapest convincing interior there is.
    [[nodiscard]] static SurfaceMaps interiorAtlas(int size, std::uint32_t seed);

    /// Painted timber, for doors, shopfronts and benches.
    [[nodiscard]] static SurfaceMaps paintedWood(int size, std::uint32_t seed,
                                                 const float colour[3]);

    /// Oiled hardwood, for bench slats.
    [[nodiscard]] static SurfaceMaps hardwood(int size, std::uint32_t seed);

    /// Tree bark.
    [[nodiscard]] static SurfaceMaps bark(int size, std::uint32_t seed);

    /// An alpha-masked card of leaves, used for the crown canopy.
    [[nodiscard]] static SurfaceMaps foliageCard(int size, std::uint32_t seed);

    /// Mown grass and the soil under it.
    [[nodiscard]] static SurfaceMaps grass(int size, std::uint32_t seed);

    /// Clothing fabric.
    [[nodiscard]] static SurfaceMaps fabric(int size, std::uint32_t seed, const float colour[3]);

    /// Skin.
    [[nodiscard]] static SurfaceMaps skin(int size, std::uint32_t seed, const float colour[3]);

    /// Road marking paint on asphalt, worn by traffic. Alpha is the mask.
    [[nodiscard]] static SurfaceMaps roadPaint(int size, std::uint32_t seed, float wear);

    /// A cast-iron manhole cover, complete with its pattern.
    [[nodiscard]] static SurfaceMaps manholeCover(int size, std::uint32_t seed);

    /// A flat, uniform surface. For details too small to need a texture, so they
    /// can still share the one material path instead of having a second one.
    [[nodiscard]] static SurfaceMaps flat(int size, const float colour[3], float roughness,
                                          float metallic);

    /// A 2x2 white pixel, the fallback when a map is missing.
    [[nodiscard]] static Image white(int size = 4);
};

}  // namespace CnaStreet::Assets
