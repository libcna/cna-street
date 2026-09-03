// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Geometry/MeshBuilder.hpp"

#include <string>
#include <vector>

namespace CnaStreet {

struct Material;

/**
 * @brief Sorts generated geometry into per-material, per-region batches.
 *
 * Two things have to be true at once for the scene to be both cheap to draw and
 * cheap to cull. Cheap to draw means as few draw calls as possible, which argues
 * for one enormous mesh per material. Cheap to cull means the renderer can
 * discard what is behind the camera, which argues for many small meshes. A road
 * surface batched as one 260 m mesh is one draw call that is always visible and
 * always fully rasterised.
 *
 * The compromise is a spatial grid: geometry is merged per material *within a
 * cell*, so a street of 46 buildings becomes a few hundred batches rather than
 * tens of thousands of draws or a handful of uncullable monsters. The cell size
 * is a little larger than a building plot, which is the natural granularity
 * here.
 */
class GeometryCollector
{
public:
    /// Side of a grid cell in metres.
    static constexpr float kCellSize = 34.0f;

    /// Everything added after this call lands in the cell containing the point.
    void setRegion(float x, float z);
    /// Puts everything in one cell regardless of position — for geometry that is
    /// genuinely one object, like a single building.
    void setRegionKey(int key);
    [[nodiscard]] static int regionKeyFor(float x, float z);

    /// The builder for one material in the current cell, created on demand.
    [[nodiscard]] Geometry::MeshBuilder& builder(const Material* material);

    struct Batch
    {
        const Material* material = nullptr;
        int             region   = 0;
        Geometry::MeshData mesh;
    };

    /// Moves every non-empty batch out. The collector is empty afterwards.
    [[nodiscard]] std::vector<Batch> take();

    [[nodiscard]] std::size_t batchCount() const { return entries_.size(); }
    [[nodiscard]] std::size_t triangleCount() const;

private:
    struct Entry
    {
        const Material* material = nullptr;
        int             region   = 0;
        Geometry::MeshBuilder builder;
    };

    std::vector<Entry> entries_;
    int current_ = 0;
};

}  // namespace CnaStreet
