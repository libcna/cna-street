// SPDX-License-Identifier: MIT
#pragma once

#include "System/Random.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace CnaStreet {

/**
 * @brief Seeded, reproducible randomness for procedural placement.
 *
 * Every variation in the city — plot widths, façade colours, which shop gets
 * blinds, where the cracks in the asphalt run, which car parks where — comes
 * through one of these. The same seed always produces the same street, which is
 * what makes a screenshot comparable to yesterday's and a bug reproducible.
 *
 * The generator itself is sharp-runtime's `System::Random`, i.e. .NET's
 * subtractive-lagged-Fibonacci generator, chosen deliberately over
 * `std::mt19937`: its sequence is defined by the runtime rather than by the
 * standard library implementation, so the same seed gives the same street on
 * every platform and compiler. `std::uniform_real_distribution` makes no such
 * promise, which is exactly the kind of difference that turns a golden
 * screenshot into a false failure on someone else's machine.
 */
class Rng
{
public:
    explicit Rng(std::uint32_t seed) : random_(static_cast<int>(seed & 0x7FFFFFFFu)) {}

    /// A derived generator, so a subsystem can draw without disturbing the
    /// sequence any other subsystem sees. Deriving by name keeps the streams
    /// stable when an unrelated system is added between them.
    [[nodiscard]] static Rng derive(std::uint32_t seed, std::string_view stream);

    /// Uniform in [0,1).
    [[nodiscard]] float unit() { return static_cast<float>(random_.NextDouble()); }
    /// Uniform in [min,max).
    [[nodiscard]] float range(float min, float max) { return min + unit() * (max - min); }
    /// Uniform in [-half,+half).
    [[nodiscard]] float signed_(float half) { return range(-half, half); }
    /// Uniform integer in [min,max].
    [[nodiscard]] int intRange(int min, int max) { return random_.Next(min, max + 1); }
    /// True with the given probability.
    [[nodiscard]] bool chance(float probability) { return unit() < probability; }
    /// Uniform index into a container of the given size.
    [[nodiscard]] std::size_t index(std::size_t size)
    {
        return size == 0 ? 0 : static_cast<std::size_t>(random_.Next(static_cast<int>(size)));
    }
    /// One of the listed values.
    template <typename T>
    [[nodiscard]] const T& pick(const std::vector<T>& values)
    {
        return values[index(values.size())];
    }
    /// Approximately normal, by averaging three uniforms. Good enough for
    /// scattering, and bounded, which a real Gaussian is not.
    [[nodiscard]] float aboutOne(float spread)
    {
        return 1.0f + ((unit() + unit() + unit()) / 3.0f - 0.5f) * 2.0f * spread;
    }

private:
    System::Random random_;
};

}  // namespace CnaStreet
