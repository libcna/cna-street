// SPDX-License-Identifier: MIT
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CnaStreet::Atmosphere {

/**
 * @brief Single-scattering sky radiance looking along @p viewDirection.
 *
 * The CPU twin of `cnaSkyRadiance` in the sky shader: Rayleigh and Mie
 * scattering integrated in closed form along the view ray, with the sunlight
 * attenuated along its own path. The sky the camera sees and the environment
 * the street is lit by both come from this one model, so they cannot disagree.
 *
 * It used to be CNA's `AtmosphericSky`; CNA retired its graphics engine layer,
 * and the model now lives here, where the street's shaders already carried it.
 *
 * @param viewDirection The direction looked in; need not be normalised.
 * @param sunTravelDirection The direction sunlight *travels*, i.e. away from the sun.
 * @param turbidity Atmospheric turbidity; 1 is air with no aerosol in it at all.
 * @return Linear radiance.
 */
[[nodiscard]] Microsoft::Xna::Framework::Vector3 radiance(
    const Microsoft::Xna::Framework::Vector3& viewDirection,
    const Microsoft::Xna::Framework::Vector3& sunTravelDirection, float turbidity);

/**
 * @brief The same model as GLSL ES 3.00 function definitions.
 *
 * No `#version` line and no precision qualifier: it is pasted into a fragment
 * program that already has both, ahead of the code that calls
 * `cnaSkyRadiance`. `shaders/sky/sky.vulkan.frag.glsl` carries the identical
 * text for the renderers that take the packaged variants; keep the two in step.
 */
[[nodiscard]] const char* modelGlsl();

}  // namespace CnaStreet::Atmosphere
