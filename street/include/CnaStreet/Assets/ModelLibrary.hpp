// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Render/Material.hpp"

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Microsoft::Xna::Framework::Content {
    class ContentManager;
}
namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class Model;
    class ModelMeshPart;
    struct SkinningData;
}

namespace CnaStreet {

class GpuMesh;
class MaterialLibrary;
class SkinnedGpuMesh;

/**
 * @brief Models imported from glTF, through CNA's own content pipeline.
 *
 * The build compiles each source `.glb` with `cna_tool_gltf_to_cnb` — the same
 * shared glTF interpretation the framework's own `.cnj` path uses — and this
 * loads the result with `ContentManager::Load<Model>` at start-up. Nothing here
 * parses glTF: the point of the exercise is that the *framework's* importer is
 * what reads the file, so a defect in it is a defect this application can
 * report rather than one it works around privately.
 *
 * What comes back is a `Model` whose mesh parts carry `PbrEffect`s with the
 * material glTF declared. That is exactly the material model the rest of this
 * scene already uses, so the parts are translated into the application's own
 * `Material` and drawn by the ordinary renderer — with the same shadows, the
 * same image-based lighting and the same culling as everything else. The
 * alternative, drawing each model through its own effect, would have imported
 * geometry lit differently from the street it stands in.
 *
 * A missing model is not an error. The source assets are fetched rather than
 * committed (see `assets/external/manifest.json`), so a checkout that has not
 * run `scripts/fetch-assets.sh` simply has no imported props, exactly as a
 * checkout with no compiled content simply generates its textures.
 */
class ModelLibrary
{
public:
    ModelLibrary(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                 MaterialLibrary& materials);
    ~ModelLibrary();

    ModelLibrary(const ModelLibrary&) = delete;
    ModelLibrary& operator=(const ModelLibrary&) = delete;

    void setContentSource(Microsoft::Xna::Framework::Content::ContentManager* content);

    /// One drawable piece of an imported model, already in the application's
    /// own material vocabulary.
    struct Part
    {
        const Material* material = nullptr;
        const GpuMesh*  mesh     = nullptr;
        /// The scene-graph transform of the node this part hangs from.
        Microsoft::Xna::Framework::Matrix bone =
            Microsoft::Xna::Framework::Matrix::getIdentityProperty();
        /// The name of the mesh this part came from, as the file named it. A
        /// scanned prop often ships its variants side by side in one file --
        /// a fresh hydrant and an aged one, a clean bin and a rusted one --
        /// and the name is how a caller takes one and leaves the other.
        std::string node;
    };

    struct Imported
    {
        std::string name;
        std::vector<Part> parts;
        Microsoft::Xna::Framework::BoundingBox bounds;
        /// Height of the model's bounding box, so a caller can scale it to a
        /// real-world size without knowing what units it was authored in.
        [[nodiscard]] float height() const { return bounds.Max.Y - bounds.Min.Y; }
        [[nodiscard]] bool empty() const { return parts.empty(); }
        /// Every part's triangles added up. The Khronos sample set is a set of
        /// *material* showcases -- one of these plants is 54 000 triangles in a
        /// single part, authored to be filmed on a turntable -- and a caller
        /// standing one on a 40 cm plinth behind glass needs to know that before
        /// it agrees to.
        [[nodiscard]] int triangleCount() const;
    };

    /// One imported rig: the mesh parts a skin drives, its skeleton and its
    /// clips, in the form `AnimationPlayer` and `SkinnedPbrEffect` want.
    ///
    /// This is the end-to-end demonstration the project exists to make. CNA's
    /// own importer read the glTF, CNA's content compiler wrote the `.cnb`,
    /// `ContentManager::Load<Model>` read it back, and what comes out is a
    /// `SkinningData` with a bind pose, an inverse bind pose, a hierarchy and
    /// named clips -- the same type this application fills in by hand for its
    /// generated people, driving the same effect through the same draw path.
    /// Nothing here parses glTF and nothing here poses a skeleton: it borrows
    /// the framework's.
    struct ImportedRig
    {
        std::string name;
        struct SkinnedPart
        {
            const Material*       material = nullptr;
            const SkinnedGpuMesh* mesh     = nullptr;
        };
        std::vector<SkinnedPart> parts;
        Microsoft::Xna::Framework::Graphics::SkinningData* skinning = nullptr;
        Microsoft::Xna::Framework::BoundingBox bounds;
        /// The clip names the file brought with it, in file order.
        std::vector<std::string> clips;
        [[nodiscard]] float height() const { return bounds.Max.Y - bounds.Min.Y; }
        [[nodiscard]] bool empty() const { return parts.empty() || skinning == nullptr; }
    };

    /// Loads @p asset and returns its first skin, or nullptr when the file has
    /// none. Cached alongside the rigid load.
    [[nodiscard]] const ImportedRig* loadRig(const std::string& asset);

    /// Loads @p asset from the content root, or returns nullptr. Cached: asking
    /// twice costs one load.
    [[nodiscard]] const Imported* load(const std::string& asset);

    /// A transform that stands the model upright on y = 0, centred on x/z, and
    /// scaled so it is @p metres tall. Imported assets are authored at wildly
    /// different scales -- an avocado and a sofa are both "one unit" in
    /// somebody's editor -- and a shop window full of things at their authored
    /// size is a shop window full of nonsense.
    [[nodiscard]] static Microsoft::Xna::Framework::Matrix fitTo(const Imported& model,
                                                                 float metres);

    [[nodiscard]] std::size_t loadedCount() const { return loaded_; }
    [[nodiscard]] std::size_t triangleCount() const { return triangles_; }
    /// How many material maps arrived with one mip level rather than a
    /// chain, over every model loaded. Zero when the content build compiled
    /// them all; see docs/cna-findings.md GLTF-206.
    [[nodiscard]] int singleLevelMaps() const { return singleLevelMaps_; }
    [[nodiscard]] const std::vector<std::string>& failures() const { return failures_; }

private:
    Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
    MaterialLibrary& materials_;
    Microsoft::Xna::Framework::Content::ContentManager* content_ = nullptr;

    /// The loaded `Model`s, kept alive because the parts borrow their buffers.
    std::vector<std::unique_ptr<Microsoft::Xna::Framework::Graphics::Model>> models_;
    std::vector<std::unique_ptr<GpuMesh>> meshes_;
    std::vector<std::unique_ptr<SkinnedGpuMesh>> skinnedMeshes_;
    /// The open `Model` behind each cached name, so a later skin lookup does
    /// not have to guess which of the loaded models it belongs to.
    std::unordered_map<std::string, Microsoft::Xna::Framework::Graphics::Model*> modelByName_;
    std::unordered_map<std::string, std::unique_ptr<Imported>> cache_;
    std::unordered_map<std::string, std::unique_ptr<ImportedRig>> rigs_;
    std::vector<std::string> failures_;
    std::size_t loaded_ = 0;
    std::size_t triangles_ = 0;
    int singleLevelMaps_ = 0;
};

}  // namespace CnaStreet
