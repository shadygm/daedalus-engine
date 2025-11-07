#pragma once
// Suppress warnings in third-party code.
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtx/string_cast.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
DISABLE_WARNINGS_POP()
#include <ostream>

namespace glm {

inline std::ostream& operator<<(std::ostream& stream, const glm::vec2& obj)
{
    return stream << glm::to_string(obj);
}

inline std::ostream& operator<<(std::ostream& stream, const glm::vec3& obj)
{
    return stream << glm::to_string(obj);
}

inline std::ostream& operator<<(std::ostream& stream, const glm::uvec3& obj)
{
    return stream << glm::to_string(obj);
}

inline std::ostream& operator<<(std::ostream& stream, const glm::uvec4& obj)
{
    return stream << glm::to_string(obj);
}

inline std::ostream& operator<<(std::ostream& stream, const glm::vec4& obj)
{
    return stream << glm::to_string(obj);
}

inline std::ostream& operator<<(std::ostream& stream, const glm::mat4x4& obj)
{
    return stream << glm::to_string(obj);
}

}
