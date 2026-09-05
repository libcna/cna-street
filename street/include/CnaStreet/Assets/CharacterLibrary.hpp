// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Geometry/SkinnedMeshData.hpp"
#include "CnaStreet/Render/Material.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CnaStreet {

class MaterialLibrary;

/**
 * @brief People authored outside the generator, in this project's own
 * character format.
 *
 * The crowd's figures are built by `CharacterFactory` from swept tubes, and
 * from four metres they read as mannequins. A rigged human under a licence
 * this repository can carry does not exist ready-made, but the MakeHuman base
 * mesh, its targets and its system asset pack -- skins, clothes, shoes, hair,
 * eyes -- are CC0, and `scripts/blender-people.py` assembles them with MPFB in
 * Blender and writes each person as a JSON header beside a binary of vertices
 * already in the renderer's skinned layout, weighted onto the *same*
 * nineteen-bone skeleton the generator builds, with its joints where MakeHuman's
 * rig has them. So an imported person drops into the crowd exactly where a
 * generated one stood: the same `SkinnedPbrEffect`, the same walk and idle
 * clips `CharacterFactory::clips` builds against the skeleton, the same
 * distance switch to a far copy, the same rigid stand-in for the shadow pass.
 *
 * Its own format rather than glTF, because an imported *skinned* glTF part
 * still draws nothing through CNA's skinned path (docs/cna-findings.md
 * GLTF-208) while the generator's own skinned meshes draw fine; feeding the
 * authored figure through the path that works is the whole trick. Nothing
 * here parses glTF: the Blender script already did the reading, and what it
 * wrote is a memory image of what `SkinnedGpuMesh` uploads.
 *
 * A person that is not there is not an error. The derived files exist only
 * where Blender and MPFB have run, and the generated figure stands in for
 * every variant without one, exactly as generated props stand in for
 * unfetched scans.
 */
class CharacterLibrary
{
public:
    explicit CharacterLibrary(MaterialLibrary& materials);

    /// Where the compiled content is: the JSON and binaries are read from
    /// here, and the textures by logical name through the material library.
    void setContentRoot(const std::string& root);

    struct Part
    {
        const Material* material = nullptr;
        Geometry::SkinnedMeshData mesh;
        /// body, clothes, hair, eyes or eyebrows, as the script classed it.
        std::string kind;
    };
    struct Person
    {
        std::string name;
        /// As authored, feet on y = 0; the crowd scales each copy to its own
        /// stature.
        float height = 1.75f;
        Geometry::Skeleton skeleton;
        std::vector<Part> near;
        std::vector<Part> far;
        [[nodiscard]] bool empty() const { return near.empty(); }
    };

    /// Loads `<root>/<name>.json` and its binaries, or returns nullptr. Cached.
    [[nodiscard]] const Person* load(const std::string& name);

    [[nodiscard]] std::size_t loadedCount() const { return loaded_; }
    [[nodiscard]] const std::vector<std::string>& failures() const { return failures_; }

private:
    MaterialLibrary& materials_;
    std::string root_;
    std::unordered_map<std::string, std::unique_ptr<Person>> cache_;
    std::vector<std::string> failures_;
    std::size_t loaded_ = 0;
};

}  // namespace CnaStreet
