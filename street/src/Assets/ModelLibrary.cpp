// SPDX-License-Identifier: MIT
#include "CnaStreet/Assets/ModelLibrary.hpp"

#include "CnaStreet/Render/GpuMesh.hpp"
#include "CnaStreet/Render/MaterialLibrary.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "CnaStreet/Render/SkinnedGpuMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"

#include <algorithm>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Microsoft::Xna::Framework::Content::ContentManager;

namespace CnaStreet {

namespace {

/// The bone chain above a mesh, composed. XNA carries a model's scene graph as
/// a bone hierarchy and a mesh hangs from one of them; ignoring it puts every
/// part of a multi-node model at the origin.
Matrix AbsoluteTransform(const ModelMesh& mesh)
{
    Matrix out = Matrix::getIdentityProperty();
    for (const ModelBone* bone = mesh.getParentBoneProperty(); bone != nullptr;
         bone = bone->getParentProperty())
        out = out * bone->getTransformProperty();
    return out;
}

BoundingBox TransformBox(const BoundingBox& box, const Matrix& transform)
{
    Vector3 lo(1e30f, 1e30f, 1e30f);
    Vector3 hi(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < 8; ++i)
    {
        const Vector3 corner((i & 1) ? box.Max.X : box.Min.X, (i & 2) ? box.Max.Y : box.Min.Y,
                             (i & 4) ? box.Max.Z : box.Min.Z);
        const Vector3 p = Vector3::Transform(corner, transform);
        lo = Vector3(std::min(lo.X, p.X), std::min(lo.Y, p.Y), std::min(lo.Z, p.Z));
        hi = Vector3(std::max(hi.X, p.X), std::max(hi.Y, p.Y), std::max(hi.Z, p.Z));
    }
    return BoundingBox(lo, hi);
}

}  // namespace

ModelLibrary::ModelLibrary(GraphicsDevice& device, MaterialLibrary& materials)
    : device_(device), materials_(materials)
{
}

ModelLibrary::~ModelLibrary() = default;

void ModelLibrary::setContentSource(ContentManager* content) { content_ = content; }

const ModelLibrary::Imported* ModelLibrary::load(const std::string& asset)
{
    const auto cached = cache_.find(asset);
    if (cached != cache_.end()) return cached->second->empty() ? nullptr : cached->second.get();

    auto imported = std::make_unique<Imported>();
    imported->name = asset;
    Imported* result = imported.get();
    cache_.emplace(asset, std::move(imported));

    if (content_ == nullptr) return nullptr;

    std::unique_ptr<Model> model;
    try
    {
        model = std::make_unique<Model>(content_->Load<Model>(asset));
    }
    catch (const std::exception& failure)
    {
        // Absent is the normal case for a checkout that has not fetched the
        // external assets, and it is not an error: the scene has a procedural
        // fallback for everything imported. Anything else the loader throws is
        // treated the same way and reported once.
        failures_.push_back(asset + ": " + failure.what());
        return nullptr;
    }

    Vector3 lo(1e30f, 1e30f, 1e30f);
    Vector3 hi(-1e30f, -1e30f, -1e30f);
    int index = 0;

    const ModelMeshCollection& meshes = model->getMeshesProperty();
    for (int m = 0; m < meshes.getCountProperty(); ++m)
    {
        ModelMesh* mesh = meshes[m];
        if (mesh == nullptr) continue;
        const Matrix bone = AbsoluteTransform(*mesh);
        const BoundingSphere sphere = mesh->getBoundingSphereProperty();
        const BoundingBox local(sphere.Center - Vector3(sphere.Radius, sphere.Radius, sphere.Radius),
                                sphere.Center + Vector3(sphere.Radius, sphere.Radius, sphere.Radius));

        const ModelMeshPartCollection& parts = mesh->getMeshPartsProperty();
        for (int p = 0; p < parts.getCountProperty(); ++p)
        {
            ModelMeshPart* part = parts[p];
            if (part == nullptr || part->getPrimitiveCountProperty() <= 0) continue;

            // The material glTF declared, read back off the effect the importer
            // built. Translated into this project's own Material rather than
            // used through its own effect, so an imported prop is lit by the
            // same sun, the same sky and the same shadow map as the shopfront
            // it is standing in.
            Material material;
            material.name = asset + "." + std::to_string(index);
            if (const auto* pbr = dynamic_cast<const PbrEffect*>(part->getEffectProperty()))
            {
                material.baseColour        = pbr->getDiffuseColorProperty();
                material.alpha             = pbr->getAlphaProperty();
                material.metallic          = pbr->getMetallicFactorProperty();
                material.roughness         = pbr->getRoughnessFactorProperty();
                material.emissiveFactor    = pbr->getEmissiveFactorProperty();
                material.albedo            = pbr->getTextureProperty();
                material.normal            = pbr->getNormalMapProperty();
                material.orm               = pbr->getMetallicRoughnessMapProperty();
                material.emissive          = pbr->getEmissiveMapProperty();
                material.normalScale       = pbr->getNormalScaleEXTProperty();
                material.occlusionStrength = pbr->getOcclusionStrengthEXTProperty();
                material.ior               = pbr->getIorEXTProperty();
                material.specular          = pbr->getSpecularFactorEXTProperty();
                material.alphaMode         = pbr->getAlphaModeEXTProperty();
                material.alphaCutoff       = pbr->getAlphaCutoffEXTProperty();
                material.doubleSided       = pbr->getDoubleSidedEXTProperty();
                if (material.alphaMode == AlphaModeEXT::Blend)
                {
                    material.castsShadow = false;
                    material.writesDepth = false;
                }
            }
            else
            {
                // A part the importer gave a stock effect rather than a PBR one:
                // an unlit or dual-texture material. Draw it as a flat diffuse
                // surface rather than dropping it.
                material.roughness = 0.85f;
            }

            const Material* installed = materials_.add(material.name, {}, material);
            if (installed == nullptr) continue;

            try
            {
                auto mesh2 = std::make_unique<GpuMesh>(*part, local, material.name);
                triangles_ += static_cast<std::size_t>(mesh2->triangleCount());
                result->parts.push_back(Part{installed, mesh2.get(), bone});
                meshes_.push_back(std::move(mesh2));
            }
            catch (const std::exception&)
            {
                continue;
            }

            const BoundingBox world = TransformBox(local, bone);
            lo = Vector3(std::min(lo.X, world.Min.X), std::min(lo.Y, world.Min.Y),
                         std::min(lo.Z, world.Min.Z));
            hi = Vector3(std::max(hi.X, world.Max.X), std::max(hi.Y, world.Max.Y),
                         std::max(hi.Z, world.Max.Z));
            ++index;
        }
    }

    if (result->parts.empty())
    {
        failures_.push_back(asset + ": loaded, but has no drawable parts");
        return nullptr;
    }

    result->bounds = BoundingBox(lo, hi);
    modelByName_[asset] = model.get();
    models_.push_back(std::move(model));
    ++loaded_;
    CNA::Logger::Info("cna-street: imported '" + asset + "' -- "
                      + std::to_string(result->parts.size()) + " part(s), "
                      + std::to_string(result->triangleCount()) + " triangles, "
                      + std::to_string(result->height()) + " m tall as authored");
    return result;
}

Matrix ModelLibrary::fitTo(const Imported& model, float metres)
{
    const Vector3 size = model.bounds.Max - model.bounds.Min;
    const float tallest = std::max({size.X, size.Y, size.Z, 1e-4f});
    const float scale = metres / std::max(size.Y > 1e-4f ? size.Y : tallest, 1e-4f);
    const Vector3 centre((model.bounds.Min.X + model.bounds.Max.X) * 0.5f, model.bounds.Min.Y,
                         (model.bounds.Min.Z + model.bounds.Max.Z) * 0.5f);
    return Matrix::CreateTranslation(-centre) * Matrix::CreateScale(scale);
}


int ModelLibrary::Imported::triangleCount() const
{
    int total = 0;
    for (const Part& part : parts)
        if (part.mesh != nullptr) total += part.mesh->triangleCount();
    return total;
}


const ModelLibrary::ImportedRig* ModelLibrary::loadRig(const std::string& asset)
{
    const auto cached = rigs_.find(asset);
    if (cached != rigs_.end()) return cached->second->empty() ? nullptr : cached->second.get();

    auto& slot = rigs_[asset];
    slot = std::make_unique<ImportedRig>();
    slot->name = asset;
    if (content_ == nullptr) return nullptr;

    // The rigid load first: it opens the Model, keeps it alive in `models_` and
    // installs the materials. A skinned file goes through it exactly like an
    // unskinned one -- the parts are the same parts -- so this borrows that
    // work rather than repeating it.
    const Imported* rigid = load(asset);
    if (rigid == nullptr) return nullptr;

    const auto found = modelByName_.find(asset);
    if (found == modelByName_.end() || found->second == nullptr) return nullptr;
    Model* model = found->second;

    // Two places to look, and which one holds the skin depends on how the model
    // was loaded rather than on what is in it.
    //
    // `Model::SkinsEXT` is the unambiguous mapping from every skin to the meshes
    // its palette drives, and it is what the *runtime glTF* loader fills in.
    // A model that came through the content pipeline as a `.cnb` -- which is
    // the whole point of compiling one -- puts its skinning data on
    // `Model::Tag` instead, following the XNA Skinned Model Sample convention,
    // and leaves `SkinsEXT` empty. So the same file, imported the same way,
    // exposes its skeleton through a different API depending on whether it was
    // compiled first, and a caller written against either one finds nothing
    // through the other. See docs/cna-findings.md GLTF-207.
    //
    // Both are checked here, `SkinsEXT` first because when it is populated it
    // says which meshes belong to the skin and `Tag` does not.
    const std::vector<ModelSkinEXT>& skins = model->getSkinsEXTProperty();
    std::vector<ModelMesh*> skinnedMeshes;
    if (!skins.empty() && skins.front().Data != nullptr)
    {
        slot->skinning = skins.front().Data;
        skinnedMeshes  = skins.front().Meshes;
    }
    else if (auto* tagged = dynamic_cast<SkinningData*>(model->getTagProperty()))
    {
        slot->skinning = tagged;
        // Tag says nothing about *which* meshes the palette drives, so every
        // mesh in the model is taken. That is right for a character file and
        // would be wrong for a scene with a rigged prop in it; the assert that
        // matters is the vertex declaration, and a mesh that is not skinned
        // fails to build a SkinnedGpuMesh rather than drawing wrongly.
        const ModelMeshCollection& all = model->getMeshesProperty();
        for (int m = 0; m < all.getCountProperty(); ++m)
            if (all[m] != nullptr) skinnedMeshes.push_back(all[m]);
    }
    if (slot->skinning == nullptr || skinnedMeshes.empty())
    {
        // Said out loud rather than filed away. A rig that quietly fails to
        // load looks exactly like a rig that was never asked for, and the
        // difference matters: this one is the end-to-end demonstration.
        CNA::Logger::Warn("cna-street: '" + asset
                          + "' loaded but carries no skin on SkinsEXT or Tag");
        failures_.push_back(asset + ": loaded, but carries no skin");
        return nullptr;
    }
    for (const auto& clip : slot->skinning->AnimationClips) slot->clips.push_back(clip.first);

    Vector3 lo(1e30f, 1e30f, 1e30f);
    Vector3 hi(-1e30f, -1e30f, -1e30f);
    int index = 0;
    for (ModelMesh* mesh : skinnedMeshes)
    {
        if (mesh == nullptr) continue;
        const BoundingSphere sphere = mesh->getBoundingSphereProperty();
        const Vector3 radius(sphere.Radius, sphere.Radius, sphere.Radius);
        const BoundingBox local(sphere.Center - radius, sphere.Center + radius);
        lo = Vector3(std::min(lo.X, local.Min.X), std::min(lo.Y, local.Min.Y),
                     std::min(lo.Z, local.Min.Z));
        hi = Vector3(std::max(hi.X, local.Max.X), std::max(hi.Y, local.Max.Y),
                     std::max(hi.Z, local.Max.Z));

        const ModelMeshPartCollection& parts = mesh->getMeshPartsProperty();
        for (int p = 0; p < parts.getCountProperty(); ++p)
        {
            ModelMeshPart* part = parts[p];
            if (part == nullptr || part->getPrimitiveCountProperty() <= 0) continue;
            // The material the rigid load already installed for this part, by
            // the name it gave it. Same effect, same lighting, same shadows.
            const Material* material =
                materials_.find(asset + "." + std::to_string(index));
            if (material == nullptr && index < static_cast<int>(rigid->parts.size()))
                material = rigid->parts[static_cast<std::size_t>(index)].material;
            ++index;
            if (material == nullptr) continue;


            try
            {
                auto skinned = std::make_unique<SkinnedGpuMesh>(
                    *part, local, asset + ".skin." + std::to_string(p));
                slot->parts.push_back(ImportedRig::SkinnedPart{material, skinned.get()});
                skinnedMeshes_.push_back(std::move(skinned));
            }
            catch (const std::exception&)
            {
                continue;
            }
        }
    }

    if (slot->parts.empty())
    {
        CNA::Logger::Warn("cna-street: '" + asset + "' has a skin with no drawable parts");
        failures_.push_back(asset + ": has a skin with no drawable parts");
        return nullptr;
    }
    slot->bounds = BoundingBox(lo, hi);

    std::string clipList;
    for (const std::string& clip : slot->clips)
        clipList += (clipList.empty() ? "" : ", ") + clip;
    CNA::Logger::Info("cna-street: imported rig '" + asset + "' -- "
                      + std::to_string(slot->parts.size()) + " skinned part(s), "
                      + std::to_string(slot->skinning->BoneCount) + " bones, clip(s): "
                      + (clipList.empty() ? std::string("none") : clipList));
    return slot.get();
}

}  // namespace CnaStreet
