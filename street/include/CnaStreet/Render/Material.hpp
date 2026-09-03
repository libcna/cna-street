// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Graphics {
    class Texture2D;
}

namespace CnaStreet {

/**
 * @brief A surface, in the glTF metallic-roughness sense.
 *
 * Maps directly onto what `PbrEffect` can express, because inventing a material
 * model of our own and then translating it would only add a place for the two
 * to disagree. Textures are borrowed pointers owned by `MaterialLibrary`.
 *
 * `orm` is bound as *both* the occlusion map and the metallic-roughness map:
 * that is the glTF ORM packing (R occlusion, G roughness, B metallic) and
 * `PbrEffect` reads exactly those channels from each.
 */
struct Material
{
    std::string name;

    Microsoft::Xna::Framework::Graphics::Texture2D* albedo   = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D* normal   = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D* orm      = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D* emissive = nullptr;

    Microsoft::Xna::Framework::Vector3 baseColour{1.0f, 1.0f, 1.0f};
    Microsoft::Xna::Framework::Vector3 emissiveFactor{0.0f, 0.0f, 0.0f};
    float metallic  = 0.0f;
    float roughness = 1.0f;
    float alpha     = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    /// Index of refraction, KHR_materials_ior. 1.5 is window glass, 1.45 most
    /// paints and plastics, 1.33 water.
    float ior = 1.5f;
    /// KHR_materials_specular strength for dielectrics.
    float specular = 1.0f;

    Microsoft::Xna::Framework::Graphics::AlphaModeEXT alphaMode =
        Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Opaque;
    float alphaCutoff = 0.5f;
    bool  doubleSided = false;

    /// Applied through `TextureTransformEXT` (KHR_texture_transform). Mesh UVs
    /// already carry the physical tiling, so this is for atlas addressing —
    /// picking one room out of the interior atlas for one window.
    Microsoft::Xna::Framework::Vector2 uvScale{1.0f, 1.0f};
    Microsoft::Xna::Framework::Vector2 uvOffset{0.0f, 0.0f};

    /// Whether geometry with this material is written into the shadow map.
    /// Glass and painted markings are not: a pane of glass casting an opaque
    /// shadow is one of the most obvious tells there is.
    bool castsShadow = true;
    /// A cap, in metres, on how far this surface keeps casting. 0 means "as far
    /// as whatever registered it says".
    ///
    /// One number per *batch* is not enough, because a building is one batch
    /// set and it contains both the mass -- whose shadow crosses the whole
    /// street and must survive to the far cascade -- and sixty window frames,
    /// whose shadows are a 20 mm line that is under a texel wide by the second
    /// cascade. Publishing them together means either losing the building's
    /// shadow or rasterising a hundred thousand triangles of joinery into every
    /// cascade to draw nothing. This says which is which.
    float shadowDistance = 0.0f;
    /// Excluded from the depth/normal prepass as well as the shadow pass.
    bool writesDepth = true;

    [[nodiscard]] bool isBlended() const
    {
        return alphaMode == Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Blend;
    }
    [[nodiscard]] bool isMasked() const
    {
        return alphaMode == Microsoft::Xna::Framework::Graphics::AlphaModeEXT::Mask;
    }
};

}  // namespace CnaStreet
