// SPDX-License-Identifier: MIT
#include "CnaStreet/Scene/GeometryCollector.hpp"

#include "CnaStreet/Render/Material.hpp"

#include <cmath>

namespace CnaStreet {

int GeometryCollector::regionKeyFor(float x, float z)
{
    // Offset before the divide so that negative coordinates do not fold onto
    // positive ones, and pack the two axes into one int. The 512 stride is far
    // wider than the district, so no two cells can collide.
    const int cx = static_cast<int>(std::floor((x + 4096.0f) / kCellSize));
    const int cz = static_cast<int>(std::floor((z + 4096.0f) / kCellSize));
    return cx * 512 + cz;
}

void GeometryCollector::setRegion(float x, float z)
{
    current_ = regionKeyFor(x, z);
}

void GeometryCollector::setRegionKey(int key)
{
    current_ = key;
}

Geometry::MeshBuilder& GeometryCollector::builder(const Material* material)
{
    for (Entry& entry : entries_)
        if (entry.material == material && entry.region == current_) return entry.builder;

    entries_.push_back(Entry{material, current_, Geometry::MeshBuilder{}});
    return entries_.back().builder;
}

std::vector<GeometryCollector::Batch> GeometryCollector::take()
{
    std::vector<Batch> batches;
    batches.reserve(entries_.size());
    for (Entry& entry : entries_)
    {
        if (entry.builder.mesh().empty()) continue;
        batches.push_back(Batch{entry.material, entry.region, entry.builder.take()});
    }
    entries_.clear();
    return batches;
}

std::size_t GeometryCollector::triangleCount() const
{
    std::size_t total = 0;
    for (const Entry& entry : entries_) total += entry.builder.triangleCount();
    return total;
}

}  // namespace CnaStreet
