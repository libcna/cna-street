// SPDX-License-Identifier: MIT
#include "CnaStreet/Core/Rng.hpp"

namespace CnaStreet {

Rng Rng::derive(std::uint32_t seed, std::string_view stream)
{
    // FNV-1a over the stream name, mixed with the master seed. Deliberately a
    // named hash rather than "seed + n": inserting a new subsystem in the middle
    // of the list would otherwise renumber every stream after it and change the
    // whole city for a change that added nothing.
    std::uint32_t hash = 2166136261u;
    for (const char c : stream)
    {
        hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
        hash *= 16777619u;
    }
    hash ^= seed + 0x9E3779B9u + (hash << 6) + (hash >> 2);
    return Rng(hash);
}

}  // namespace CnaStreet
