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
