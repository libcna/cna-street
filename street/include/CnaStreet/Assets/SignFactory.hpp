// SPDX-License-Identifier: MIT
#pragma once

#include "CnaStreet/Assets/Image.hpp"

#include <cstdint>
#include <string>

namespace CnaStreet::Assets {

/// The sign faces the street carries. These are **original designs** in the
/// visual language of continental European signage — the shape/colour grammar
/// (red-ringed white disc for a prohibition, red-bordered white triangle for a
/// warning, blue rectangle for information) is a functional convention, and the
/// pictograms are drawn here rather than traced from any jurisdiction's official
/// artwork, which would bring a licence with it.
enum class SignFace
{
    SpeedLimit30,
    NoEntry,
    NoParking,
    GiveWay,
    PriorityRoad,
    PedestrianCrossing,
    ChildrenWarning,
    OneWay,
    ParkingArea,
    DeadEnd,
    Count
};

class SignFactory
{
public:
    /// A sign face on its retroreflective sheeting. The image is square with the
    /// sign centred and transparent surround, so the same texture works on a
    /// disc, a triangle or a rectangle blank.
    [[nodiscard]] static SurfaceMaps face(SignFace face, int size, std::uint32_t seed);

    /// A street-name plate: white capitals on the deep blue enamel plate used
    /// across much of continental Europe.
    [[nodiscard]] static SurfaceMaps streetPlate(const std::string& name, int width, int height,
                                                 std::uint32_t seed);

    /// A shop fascia: the name in light letters on a painted board.
    [[nodiscard]] static SurfaceMaps shopFascia(const std::string& name, const float boardColour[3],
                                                const float letterColour[3], int width, int height,
                                                std::uint32_t seed);

    /// A vehicle registration plate.
    [[nodiscard]] static SurfaceMaps licencePlate(const std::string& registration, int width,
                                                  int height);

    [[nodiscard]] static const char* faceName(SignFace face);
};

}  // namespace CnaStreet::Assets
