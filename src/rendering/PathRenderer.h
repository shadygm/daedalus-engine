// SPDX-License-Identifier: MIT
#pragma once

#include "util/BezierPath.h"

#include <framework/shader.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>

class PathRenderer {
public:
    PathRenderer() = default;
    ~PathRenderer();

    void initialize(const std::filesystem::path& shaderDirectory);
    void shutdown();

    void updateGeometry(const BezierPath& path, std::uint64_t pathVersion);
    void drawCurve(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& color) const;
    void drawControlPoints(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& color, float pointSize = 6.0f) const;
    void drawTangents(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& color) const;

private:
    void ensureResources();
    void uploadBuffer(GLuint vbo, const std::vector<glm::vec3>& data) const;

    Shader m_shader;
    std::filesystem::path m_shaderDirectory;

    GLuint m_curveVao { 0 };
    GLuint m_curveVbo { 0 };
    GLsizei m_curveVertexCount { 0 };

    GLuint m_controlVao { 0 };
    GLuint m_controlVbo { 0 };
    GLsizei m_controlVertexCount { 0 };

    GLuint m_tangentVao { 0 };
    GLuint m_tangentVbo { 0 };
    GLsizei m_tangentVertexCount { 0 };

    std::uint64_t m_cachedPathVersion { 0 };
};
