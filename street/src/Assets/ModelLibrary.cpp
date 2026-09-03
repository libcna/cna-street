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

}  // namespace CnaStreet
