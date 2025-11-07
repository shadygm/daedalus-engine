// SPDX-License-Identifier: MIT
#include "rendering/PathRenderer.h"

#include <glad/glad.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <vector>

namespace {

constexpr std::size_t kCurveSamples = 256;
constexpr float kMinTangentScale = 0.25f;

[[nodiscard]] glm::mat4 buildMvp(const glm::mat4& view, const glm::mat4& projection)
{
    return projection * view;
}

} // namespace

PathRenderer::~PathRenderer()
{
    shutdown();
}

void PathRenderer::initialize(const std::filesystem::path& shaderDirectory)
{
    m_shaderDirectory = shaderDirectory;

    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, (shaderDirectory / "path_debug.vert").string());
    builder.addStage(GL_FRAGMENT_SHADER, (shaderDirectory / "path_debug.frag").string());
    m_shader = builder.build();

    ensureResources();
}

void PathRenderer::shutdown()
{
    if (m_curveVao) glDeleteVertexArrays(1, &m_curveVao);
    if (m_curveVbo) glDeleteBuffers(1, &m_curveVbo);
    if (m_controlVao) glDeleteVertexArrays(1, &m_controlVao);
    if (m_controlVbo) glDeleteBuffers(1, &m_controlVbo);
    if (m_tangentVao) glDeleteVertexArrays(1, &m_tangentVao);
    if (m_tangentVbo) glDeleteBuffers(1, &m_tangentVbo);

    m_curveVao = m_curveVbo = 0;
    m_controlVao = m_controlVbo = 0;
    m_tangentVao = m_tangentVbo = 0;
    m_curveVertexCount = m_controlVertexCount = m_tangentVertexCount = 0;
    m_cachedPathVersion = 0;
}

void PathRenderer::ensureResources()
{
    if (m_curveVao == 0) {
        glGenVertexArrays(1, &m_curveVao);
        glGenBuffers(1, &m_curveVbo);
        glBindVertexArray(m_curveVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_curveVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void*>(0));
    }
    if (m_controlVao == 0) {
        glGenVertexArrays(1, &m_controlVao);
        glGenBuffers(1, &m_controlVbo);
        glBindVertexArray(m_controlVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_controlVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void*>(0));
    }
    if (m_tangentVao == 0) {
        glGenVertexArrays(1, &m_tangentVao);
        glGenBuffers(1, &m_tangentVbo);
        glBindVertexArray(m_tangentVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_tangentVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), reinterpret_cast<void*>(0));
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void PathRenderer::uploadBuffer(GLuint vbo, const std::vector<glm::vec3>& data) const
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(glm::vec3)), data.empty() ? nullptr : data.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void PathRenderer::updateGeometry(const BezierPath& path, std::uint64_t pathVersion)
{
    if (m_cachedPathVersion == pathVersion)
        return;

    ensureResources();

    if (path.segmentCount() == 0 || path.totalLength() <= 0.0f) {
        m_curveVertexCount = 0;
        m_controlVertexCount = 0;
        m_tangentVertexCount = 0;
        m_cachedPathVersion = pathVersion;
        return;
    }

    std::vector<glm::vec3> curveVertices;
    curveVertices.reserve(kCurveSamples + 1);
    for (std::size_t i = 0; i <= kCurveSamples; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(kCurveSamples);
        curveVertices.push_back(path.sample(u));
    }
    uploadBuffer(m_curveVbo, curveVertices);
    m_curveVertexCount = static_cast<GLsizei>(curveVertices.size());

    std::vector<glm::vec3> controlPoints;
    controlPoints.reserve(path.segmentCount() * 4);
    glm::vec3 lastPoint(0.0f);
    bool hasLast = false;
    for (std::size_t segmentIndex = 0; segmentIndex < path.segmentCount(); ++segmentIndex) {
        const CubicBezier& seg = path.segments()[segmentIndex];
        const glm::vec3 cp[] = { seg.p0, seg.p1, seg.p2, seg.p3 };
        for (const glm::vec3& p : cp) {
            if (!hasLast || glm::length(p - lastPoint) > 1e-4f) {
                controlPoints.push_back(p);
                lastPoint = p;
                hasLast = true;
            }
        }
    }
    uploadBuffer(m_controlVbo, controlPoints);
    m_controlVertexCount = static_cast<GLsizei>(controlPoints.size());

    std::vector<glm::vec3> tangentSegments;
    tangentSegments.reserve((kCurveSamples + 1) * 2);
    const float tangentScale = std::max(kMinTangentScale, path.totalLength() * 0.03f);
    for (std::size_t i = 0; i <= kCurveSamples; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(kCurveSamples);
        const glm::vec3 pos = path.sample(u);
        glm::vec3 tan = path.sampleTangent(u);
        if (glm::length2(tan) > 1e-6f)
            tan = glm::normalize(tan);
        else
            tan = glm::vec3(0.0f, 1.0f, 0.0f);
        tangentSegments.push_back(pos);
        tangentSegments.push_back(pos + tan * tangentScale);
    }
    uploadBuffer(m_tangentVbo, tangentSegments);
    m_tangentVertexCount = static_cast<GLsizei>(tangentSegments.size());

    m_cachedPathVersion = pathVersion;
}

void PathRenderer::drawCurve(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& color) const
{
    if (m_curveVertexCount == 0)
        return;

    const glm::mat4 mvp = buildMvp(view, projection);

    m_shader.bind();
    const GLint mvpLoc = m_shader.getUniformLocation("uMVP");
    if (mvpLoc >= 0) glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
    const GLint colorLoc = m_shader.getUniformLocation("uColor");
    if (colorLoc >= 0) glUniform3fv(colorLoc, 1, &color[0]);
    const GLint pointLoc = m_shader.getUniformLocation("uPointSize");
    if (pointLoc >= 0) glUniform1f(pointLoc, 1.0f);

    glBindVertexArray(m_curveVao);
    glDrawArrays(GL_LINE_STRIP, 0, m_curveVertexCount);
    glBindVertexArray(0);
    glUseProgram(0);
}

void PathRenderer::drawControlPoints(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& color, float pointSize) const
{
    if (m_controlVertexCount == 0)
        return;

    const glm::mat4 mvp = buildMvp(view, projection);

    m_shader.bind();
    const GLint mvpLoc = m_shader.getUniformLocation("uMVP");
    if (mvpLoc >= 0) glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
    const GLint colorLoc = m_shader.getUniformLocation("uColor");
    if (colorLoc >= 0) glUniform3fv(colorLoc, 1, &color[0]);
    const GLint pointLoc = m_shader.getUniformLocation("uPointSize");
    if (pointLoc >= 0) glUniform1f(pointLoc, pointSize);

    glBindVertexArray(m_controlVao);
    glDrawArrays(GL_POINTS, 0, m_controlVertexCount);
    glBindVertexArray(0);
    glUseProgram(0);
}

void PathRenderer::drawTangents(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& color) const
{
    if (m_tangentVertexCount == 0)
        return;

    const glm::mat4 mvp = buildMvp(view, projection);

    m_shader.bind();
    const GLint mvpLoc = m_shader.getUniformLocation("uMVP");
    if (mvpLoc >= 0) glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);
    const GLint colorLoc = m_shader.getUniformLocation("uColor");
    if (colorLoc >= 0) glUniform3fv(colorLoc, 1, &color[0]);
    const GLint pointLoc = m_shader.getUniformLocation("uPointSize");
    if (pointLoc >= 0) glUniform1f(pointLoc, 1.0f);

    glBindVertexArray(m_tangentVao);
    glDrawArrays(GL_LINES, 0, m_tangentVertexCount);
    glBindVertexArray(0);
    glUseProgram(0);
}
