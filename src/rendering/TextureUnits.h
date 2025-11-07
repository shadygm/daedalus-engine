// SPDX-License-Identifier: MIT
#pragma once

#include <framework/opengl_includes.h>

#include <cassert>

namespace TextureUnits {

// Per-material texture units: rebound for every draw call.
constexpr GLuint Material_Albedo    = 0;
constexpr GLuint Material_Normal    = 1;
constexpr GLuint Material_MetallicRoughness = 2;
constexpr GLuint Material_AO        = 3;
constexpr GLuint Material_Emissive  = 4;
constexpr GLuint Material_Count     = 5;

// Reserved environment / IBL units. These must remain bound to their
// environment textures for the duration of the frame and must NOT be
// modified by passes other than EnvironmentManager and ShadingStage.
constexpr GLuint Env_Irradiance = 24;
constexpr GLuint Env_Prefilter  = 25;
constexpr GLuint Env_BRDF       = 26;
constexpr GLuint Env_Skybox     = 27;

constexpr bool isEnvUnit(GLuint unit)
{
    return unit >= Env_Irradiance && unit <= Env_Skybox;
}

inline void assertNotEnvUnit(GLuint unit)
{
    assert((unit < Env_Irradiance || unit > Env_Skybox) &&
    "Attempted to bind to reserved environment texture unit (24..27)");
}

} // namespace TextureUnits
