// SPDX-License-Identifier: MIT

#include "rendering/ShadowManager.h"

#include "mesh/MeshManager.h"
#include "mesh/MeshInstance.h"
#include "terrain/ProceduralFloor.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace {

constexpr glm::vec4 kShadowBorderColor(1.0f, 1.0f, 1.0f, 1.0f);
constexpr int kCubeFaceCount = 6;

[[nodiscard]] glm::vec3 sanitizeDirection(const glm::vec3& dir)
{
    glm::vec3 result = dir;
    if (glm::length(result) < 1e-4f)
        result = glm::vec3(0.0f, -1.0f, 0.0f);
    return glm::normalize(result);
}

} // namespace

ShadowManager::ShadowManager()
{
    m_shadow2DTextures.fill(0);
    m_shadowCubeTextures.fill(0);
    m_shadow2DResolutions.fill(0);
    m_shadowCubeResolutions.fill(0);
    m_shadow2DLightIndices.fill(-1);
    m_shadowCubeLightIndices.fill(-1);
    ensureSamplers();
    ensureDummyCubeTexture();
    ensureDummy2DTexture();
}

ShadowManager::~ShadowManager()
{
    if (m_framebuffer != 0) {
        glDeleteFramebuffers(1, &m_framebuffer);
        m_framebuffer = 0;
    }

    for (GLuint& tex : m_shadow2DTextures) {
        if (tex != 0) {
            glDeleteTextures(1, &tex);
            tex = 0;
        }
    }
    for (auto& [lightIndex, shadow] : m_pointShadows) {
        (void)lightIndex;
        if (shadow.texture != 0) {
            glDeleteTextures(1, &shadow.texture);
            shadow.texture = 0;
        }
    }
    m_pointShadows.clear();

    if (m_spotSamplerCompare != 0) {
        glDeleteSamplers(1, &m_spotSamplerCompare);
        m_spotSamplerCompare = 0;
    }
    if (m_spotSamplerRaw != 0) {
        glDeleteSamplers(1, &m_spotSamplerRaw);
        m_spotSamplerRaw = 0;
    }
    if (m_pointSamplerCompare != 0) {
        glDeleteSamplers(1, &m_pointSamplerCompare);
        m_pointSamplerCompare = 0;
    }

    destroyShadowArrayTexture();

    if (m_shadowMatricesBuffer != 0) {
        glDeleteBuffers(1, &m_shadowMatricesBuffer);
        m_shadowMatricesBuffer = 0;
    }

    if (m_dummyCubeTexture != 0) {
        glDeleteTextures(1, &m_dummyCubeTexture);
        m_dummyCubeTexture = 0;
    }
    if (m_dummy2DTexture != 0) {
        glDeleteTextures(1, &m_dummy2DTexture);
        m_dummy2DTexture = 0;
    }
}

void ShadowManager::ensureFramebuffer()
{
    if (m_framebuffer == 0)
        glGenFramebuffers(1, &m_framebuffer);
}

void ShadowManager::ensureUniformBuffer(std::size_t matrixCount)
{
    if (matrixCount == 0) {
        return;
    }
    if (m_shadowMatricesBuffer == 0)
        glGenBuffers(1, &m_shadowMatricesBuffer);

    glBindBuffer(GL_UNIFORM_BUFFER, m_shadowMatricesBuffer);
    glBufferData(GL_UNIFORM_BUFFER,
        static_cast<GLsizeiptr>(matrixCount * sizeof(ShadowUniform)),
        m_shadowUniforms.data(),
        GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void ShadowManager::ensure2DTexture(int slot, int resolution)
{
    resolution = std::max(resolution, 32);
    GLuint& texture = m_shadow2DTextures[static_cast<std::size_t>(slot)];
    int& cachedResolution = m_shadow2DResolutions[static_cast<std::size_t>(slot)];
    if (texture == 0) {
        glGenTextures(1, &texture);
        cachedResolution = 0;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    if (cachedResolution != resolution) {
        glTexImage2D(GL_TEXTURE_2D,
            0,
            GL_DEPTH_COMPONENT24,
            resolution,
            resolution,
            0,
            GL_DEPTH_COMPONENT,
            GL_UNSIGNED_INT,
            nullptr);
        cachedResolution = resolution;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, glm::value_ptr(kShadowBorderColor));
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ShadowManager::ensureCubeTexture(CubeShadow& shadow, int resolution)
{
    resolution = std::max(resolution, 32);

    if (shadow.texture == 0)
        glGenTextures(1, &shadow.texture);

    if (shadow.resolution != resolution) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, shadow.texture);
        for (int face = 0; face < kCubeFaceCount; ++face) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                0,
                GL_DEPTH_COMPONENT24,
                resolution,
                resolution,
                0,
                GL_DEPTH_COMPONENT,
                GL_UNSIGNED_INT,
                nullptr);
        }
        shadow.resolution = resolution;
        shadow.nextFaceToUpdate = 0;
    } else {
        glBindTexture(GL_TEXTURE_CUBE_MAP, shadow.texture);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void ShadowManager::ensureDummyCubeTexture()
{
    if (m_dummyCubeTexture != 0)
        return;

    glGenTextures(1, &m_dummyCubeTexture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_dummyCubeTexture);
    const std::uint32_t depthOne = 0xFFFFFFu;
    for (int face = 0; face < kCubeFaceCount; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            0,
            GL_DEPTH_COMPONENT24,
            1,
            1,
            0,
            GL_DEPTH_COMPONENT,
            GL_UNSIGNED_INT,
            &depthOne);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void ShadowManager::ensureDummy2DTexture()
{
    if (m_dummy2DTexture != 0)
        return;

    glGenTextures(1, &m_dummy2DTexture);
    glBindTexture(GL_TEXTURE_2D, m_dummy2DTexture);
    const std::uint32_t depthOne = 0xFFFFFFu;
    glTexImage2D(GL_TEXTURE_2D,
        0,
        GL_DEPTH_COMPONENT24,
        1,
        1,
        0,
        GL_DEPTH_COMPONENT,
        GL_UNSIGNED_INT,
        &depthOne);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, glm::value_ptr(kShadowBorderColor));
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ShadowManager::ensureSamplers()
{
    if (m_spotSamplerCompare == 0) {
        glGenSamplers(1, &m_spotSamplerCompare);
        glSamplerParameteri(m_spotSamplerCompare, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_spotSamplerCompare, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_spotSamplerCompare, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_spotSamplerCompare, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_spotSamplerCompare, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m_spotSamplerCompare, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glSamplerParameterfv(m_spotSamplerCompare, GL_TEXTURE_BORDER_COLOR, glm::value_ptr(kShadowBorderColor));
    }

    if (m_spotSamplerRaw == 0) {
        glGenSamplers(1, &m_spotSamplerRaw);
        glSamplerParameteri(m_spotSamplerRaw, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glSamplerParameteri(m_spotSamplerRaw, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glSamplerParameteri(m_spotSamplerRaw, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_spotSamplerRaw, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_spotSamplerRaw, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glSamplerParameterfv(m_spotSamplerRaw, GL_TEXTURE_BORDER_COLOR, glm::value_ptr(kShadowBorderColor));
    }

    if (m_pointSamplerCompare == 0) {
        glGenSamplers(1, &m_pointSamplerCompare);
        glSamplerParameteri(m_pointSamplerCompare, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_pointSamplerCompare, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_pointSamplerCompare, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_pointSamplerCompare, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_pointSamplerCompare, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_pointSamplerCompare, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m_pointSamplerCompare, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
}

void ShadowManager::ensureShadowArrayTexture(int layers, int resolution)
{
    layers = std::max(layers, 0);
    resolution = std::max(resolution, 1);

    if (layers == 0) {
        destroyShadowArrayTexture();
        return;
    }

    if (m_shadowArrayTexture == 0)
        glGenTextures(1, &m_shadowArrayTexture);

    if (m_shadowArrayResolution != resolution || m_shadowArrayLayers != layers) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadowArrayTexture);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,
            0,
            GL_DEPTH_COMPONENT24,
            resolution,
            resolution,
            layers,
            0,
            GL_DEPTH_COMPONENT,
            GL_UNSIGNED_INT,
            nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, glm::value_ptr(kShadowBorderColor));
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

        m_shadowArrayResolution = resolution;
        m_shadowArrayLayers = layers;
    }
}

void ShadowManager::destroyShadowArrayTexture()
{
    if (m_shadowArrayTexture != 0) {
        glDeleteTextures(1, &m_shadowArrayTexture);
        m_shadowArrayTexture = 0;
    }
    m_shadowArrayResolution = 0;
    m_shadowArrayLayers = 0;
}

void ShadowManager::updateShadowArrayTexture(int activeLayers)
{
    if (activeLayers <= 0) {
        destroyShadowArrayTexture();
        return;
    }

    int resolution = 0;
    for (int layer = 0; layer < activeLayers; ++layer)
        resolution = std::max(resolution, m_shadow2DResolutions[static_cast<std::size_t>(layer)]);

    if (resolution <= 0) {
        destroyShadowArrayTexture();
        return;
    }

    ensureShadowArrayTexture(activeLayers, resolution);

    for (int layer = 0; layer < activeLayers; ++layer) {
        GLuint source = m_shadow2DTextures[static_cast<std::size_t>(layer)];
        if (source == 0)
            continue;

        glCopyImageSubData(source,
            GL_TEXTURE_2D,
            0,
            0,
            0,
            0,
            m_shadowArrayTexture,
            GL_TEXTURE_2D_ARRAY,
            0,
            0,
            0,
            layer,
            resolution,
            resolution,
            1);
    }

    m_shadowArrayLayers = activeLayers;
    m_shadowArrayResolution = resolution;
}

void ShadowManager::renderDirectionalOrSpot(const ShadowCaster& caster,
    int slot,
    int targetResolution,
    MeshManager& meshManager,
    ProceduralFloor* floor,
    const std::function<void(const glm::mat4&, MeshManager&, ProceduralFloor*)>& renderGeometryCallback)
{
    ensure2DTexture(slot, targetResolution);
    ensureFramebuffer();

    glm::mat4 view = (caster.type == ShadowType::Directional)
        ? buildDirectionalView(caster)
        : buildSpotView(caster);
    glm::mat4 projection = (caster.type == ShadowType::Directional)
        ? buildDirectionalProjection(caster)
        : buildSpotProjection(caster);
    glm::mat4 lightViewProj = projection * view;

    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadow2DTextures[static_cast<std::size_t>(slot)], 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    int resolution = m_shadow2DResolutions[static_cast<std::size_t>(slot)];
    glViewport(0, 0, resolution, resolution);
    glClear(GL_DEPTH_BUFFER_BIT);

    const bool useOffset = std::abs(caster.shadowSlopeBias) > std::numeric_limits<float>::epsilon() || std::abs(caster.shadowBias) > std::numeric_limits<float>::epsilon();
    if (useOffset) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(caster.shadowSlopeBias, caster.shadowBias);
    }

    renderGeometryCallback(lightViewProj, meshManager, floor);

    if (useOffset)
        glDisable(GL_POLYGON_OFFSET_FILL);

    ShadowUniform uniform;
    uniform.matrix = lightViewProj;
    float invResolution = 1.0f / static_cast<float>(resolution);
    uniform.params = glm::vec4(caster.shadowNearPlane, caster.shadowFarPlane, invResolution, static_cast<float>(caster.type == ShadowType::Directional ? 0 : 1));
    m_shadowUniforms.push_back(uniform);

    LightShadowInfo info;
    info.type = caster.type;
    info.lightIndex = caster.lightIndex;
    info.samplerIndex = slot;
    info.matrixIndex = static_cast<int>(m_shadowUniforms.size()) - 1;
    info.resolution = resolution;
    info.enablePCF = caster.enablePCF;
    info.pcfKernelSize = caster.pcfKernelSize;
    info.bias = caster.shadowBias;
    info.slopeBias = caster.shadowSlopeBias;
    info.nearPlane = caster.shadowNearPlane;
    info.farPlane = caster.shadowFarPlane;
    info.cullFrontFaces = caster.cullFrontFaces;
    info.active = true;
    info.lastUpdateFrame = caster.frameIndex;
    info.lastUpdateTime = caster.timestampSeconds;
    info.importance = caster.importance;
    m_lightToInfoIndex[caster.lightIndex] = static_cast<int>(m_lightInfos.size());
    m_lightInfos.push_back(info);

    m_shadow2DLightIndices[static_cast<std::size_t>(slot)] = caster.lightIndex;
    m_shadow2DCount = std::max(m_shadow2DCount, slot + 1);
}

ShadowManager::CubeMatrices ShadowManager::buildPointMatrices(const ShadowCaster& caster) const
{
    constexpr std::array<glm::vec3, kCubeFaceCount> kDirections = {
        glm::vec3(1.0f, 0.0f, 0.0f),
        glm::vec3(-1.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f)
    };

    constexpr std::array<glm::vec3, kCubeFaceCount> kUpVectors = {
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec3(0.0f, 0.0f, -1.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f)
    };

    CubeMatrices result;
    const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, caster.shadowNearPlane, caster.shadowFarPlane);

    for (int face = 0; face < kCubeFaceCount; ++face) {
        glm::vec3 direction = kDirections[static_cast<std::size_t>(face)];
        glm::vec3 up = kUpVectors[static_cast<std::size_t>(face)];
        glm::mat4 view = glm::lookAt(caster.position, caster.position + direction, up);
        result.view[static_cast<std::size_t>(face)] = view;
        result.proj[static_cast<std::size_t>(face)] = projection;
        result.viewProj[static_cast<std::size_t>(face)] = projection * view;
    }

    return result;
}

void ShadowManager::renderPoint(const ShadowCaster& caster,
    CubeShadow& shadow,
    MeshManager& meshManager,
    ProceduralFloor* floor,
    const std::function<void(const glm::mat4&, MeshManager&, ProceduralFloor*)>& renderGeometryCallback,
    int facesToUpdate)
{
    ensureCubeTexture(shadow, caster.shadowResolution);

    CubeMatrices matrices = buildPointMatrices(caster);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    const int resolution = shadow.resolution;
    if (facesToUpdate <= 0 || facesToUpdate > kCubeFaceCount)
        facesToUpdate = kCubeFaceCount;

    const bool useOffset = std::abs(caster.shadowSlopeBias) > std::numeric_limits<float>::epsilon() || std::abs(caster.shadowBias) > std::numeric_limits<float>::epsilon();
    if (useOffset) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(caster.shadowSlopeBias, caster.shadowBias);
    }

    int startFace = shadow.nextFaceToUpdate % kCubeFaceCount;

    for (int i = 0; i < facesToUpdate; ++i) {
        const int face = (startFace + i) % kCubeFaceCount;
        const glm::mat4& vp = matrices.viewProj[static_cast<std::size_t>(face)];
        glFramebufferTexture2D(GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            shadow.texture,
            0);
        glViewport(0, 0, resolution, resolution);
        glClear(GL_DEPTH_BUFFER_BIT);
        renderGeometryCallback(vp, meshManager, floor);
    }

    shadow.nextFaceToUpdate = (startFace + facesToUpdate) % kCubeFaceCount;

    if (useOffset)
        glDisable(GL_POLYGON_OFFSET_FILL);
}

glm::mat4 ShadowManager::buildDirectionalView(const ShadowCaster& caster) const
{
    glm::vec3 direction = sanitizeDirection(caster.direction);
    glm::vec3 focus = caster.position;
    glm::vec3 eye = focus - direction * (caster.shadowFarPlane * 0.5f + caster.orthoExtents.z);
    glm::vec3 up = std::abs(direction.y) > 0.98f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::lookAt(eye, focus, up);
}

glm::mat4 ShadowManager::buildSpotView(const ShadowCaster& caster) const
{
    glm::vec3 direction = sanitizeDirection(caster.direction);
    glm::vec3 up = std::abs(direction.y) > 0.98f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::lookAt(caster.position, caster.position + direction, up);
}

glm::mat4 ShadowManager::buildDirectionalProjection(const ShadowCaster& caster) const
{
    glm::vec3 extents = glm::max(caster.orthoExtents, glm::vec3(0.1f));
    float nearPlane = std::max(caster.shadowNearPlane, 0.01f);
    float farPlane = std::max(caster.shadowFarPlane, nearPlane + 0.1f);
    return glm::ortho(-extents.x, extents.x, -extents.y, extents.y, nearPlane, farPlane);
}

glm::mat4 ShadowManager::buildSpotProjection(const ShadowCaster& caster) const
{
    float outer = glm::clamp(caster.outerConeDegrees, 1.0f, 89.0f);
    float fov = glm::radians(outer * 2.0f);
    float nearPlane = std::max(caster.shadowNearPlane, 0.01f);
    float farPlane = std::max(caster.shadowFarPlane, nearPlane + 0.1f);
    return glm::perspective(fov, 1.0f, nearPlane, farPlane);
}

void ShadowManager::uploadUniforms()
{
    if (m_shadowUniforms.empty()) {
        if (m_shadowMatricesBuffer != 0) {
            glBindBuffer(GL_UNIFORM_BUFFER, m_shadowMatricesBuffer);
            glBufferData(GL_UNIFORM_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
        return;
    }

    ensureUniformBuffer(m_shadowUniforms.size());
}

std::optional<ShadowManager::LightShadowInfo> ShadowManager::infoForLight(int lightIndex) const
{
    auto it = m_lightToInfoIndex.find(lightIndex);
    if (it == m_lightToInfoIndex.end())
        return std::nullopt;
    int index = it->second;
    if (index < 0 || index >= static_cast<int>(m_lightInfos.size()))
        return std::nullopt;
    return m_lightInfos[static_cast<std::size_t>(index)];
}

void ShadowManager::renderShadows(const std::vector<ShadowCaster>& casters,
    MeshManager& meshManager,
    ProceduralFloor* floor,
    const std::function<void(const glm::mat4&, MeshManager&, ProceduralFloor*)>& renderGeometryCallback)
{
    ensureFramebuffer();
    ensureSamplers();
    ensureDummyCubeTexture();
    ensureDummy2DTexture();

    m_shadowUniforms.clear();
    m_lightInfos.clear();
    m_lightToInfoIndex.clear();

    m_shadow2DCount = 0;
    m_shadowCubeCount = 0;
    for (int i = 0; i < kMaxShadowLights; ++i) {
        m_shadow2DLightIndices[i] = -1;
        m_shadowCubeLightIndices[i] = -1;
    }

    std::unordered_set<int> processedPointLights;
    int pointUpdatedCount = 0;
    int pointRenderedCount = 0;
    int pointReleasedCount = 0;
    int pointTrimmedCount = 0;

    if (casters.empty()) {
        // Release any lingering point shadows if no casters are provided.
        for (auto& [lightIndex, shadow] : m_pointShadows) {
            (void)lightIndex;
            if (shadow.texture != 0)
                glDeleteTextures(1, &shadow.texture);
        }
        m_pointShadows.clear();
        m_shadowCubeTextures.fill(0);
        m_shadowCubeResolutions.fill(0);
        uploadUniforms();
        return;
    }

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);

    GLint prevDrawBuffer = 0;
    GLint prevReadBuffer = 0;
    glGetIntegerv(GL_DRAW_BUFFER, &prevDrawBuffer);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);

    GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
    GLint prevCullFaceMode = GL_BACK;
    glGetIntegerv(GL_CULL_FACE_MODE, &prevCullFaceMode);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    GLenum activeCullFace = GL_FRONT;
    glCullFace(activeCullFace);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    auto releasePointShadow = [&](int lightIndex) {
        auto it = m_pointShadows.find(lightIndex);
        if (it != m_pointShadows.end()) {
            if (it->second.texture != 0) {
                glDeleteTextures(1, &it->second.texture);
                it->second.texture = 0;
            }
            m_pointShadows.erase(it);
            ++pointReleasedCount;
            return true;
        }
        return false;
    };

    std::vector<const ShadowCaster*> directionalCasters;
    std::vector<const ShadowCaster*> pointCasters;
    directionalCasters.reserve(casters.size());
    pointCasters.reserve(casters.size());

    for (const ShadowCaster& caster : casters) {
        if (caster.type == ShadowType::Point) {
            if (!caster.castsShadows || !caster.active) {
                releasePointShadow(caster.lightIndex);
                continue;
            }
            pointCasters.push_back(&caster);
        } else {
            if (!caster.castsShadows || !caster.active)
                continue;
            directionalCasters.push_back(&caster);
        }
    }

    const auto sortByImportance = [](const ShadowCaster* lhs, const ShadowCaster* rhs) {
        if (lhs->importance == rhs->importance)
            return lhs->lightIndex < rhs->lightIndex;
        return lhs->importance > rhs->importance;
    };

    std::sort(directionalCasters.begin(), directionalCasters.end(), sortByImportance);
    std::sort(pointCasters.begin(), pointCasters.end(), sortByImportance);

    const int activeDirectionalCount = std::min(static_cast<int>(directionalCasters.size()), kMaxShadowLights);
    int directionalResolution = 0;
    for (int i = 0; i < activeDirectionalCount; ++i)
        directionalResolution = std::max(directionalResolution, directionalCasters[i]->shadowResolution);
    if (directionalResolution <= 0)
        directionalResolution = 1;
    for (int slot = 0; slot < activeDirectionalCount; ++slot) {
        const ShadowCaster& caster = *directionalCasters[slot];
        const GLenum desiredCull = caster.cullFrontFaces ? GL_FRONT : GL_BACK;
        if (desiredCull != activeCullFace) {
            glCullFace(desiredCull);
            activeCullFace = desiredCull;
        }
        renderDirectionalOrSpot(caster, slot, directionalResolution, meshManager, floor, renderGeometryCallback);
    }
    updateShadowArrayTexture(activeDirectionalCount);
    m_shadow2DCount = activeDirectionalCount;
    for (int slot = activeDirectionalCount; slot < kMaxShadowLights; ++slot)
        m_shadow2DLightIndices[slot] = -1;

    for (int index = activeDirectionalCount; index < static_cast<int>(directionalCasters.size()); ++index) {
        const ShadowCaster& caster = *directionalCasters[index];
        LightShadowInfo info;
        info.type = caster.type;
        info.lightIndex = caster.lightIndex;
        info.samplerIndex = -1;
        info.matrixIndex = -1;
        info.enablePCF = caster.enablePCF;
        info.pcfKernelSize = caster.pcfKernelSize;
        info.bias = caster.shadowBias;
        info.slopeBias = caster.shadowSlopeBias;
        info.nearPlane = caster.shadowNearPlane;
        info.farPlane = caster.shadowFarPlane;
        info.cullFrontFaces = caster.cullFrontFaces;
        info.active = false;
        info.importance = caster.importance;
        info.lastUpdateFrame = caster.frameIndex;
        info.lastUpdateTime = caster.timestampSeconds;
        info.resolution = caster.shadowResolution;
        m_lightToInfoIndex[caster.lightIndex] = static_cast<int>(m_lightInfos.size());
        m_lightInfos.push_back(info);
    }

    constexpr int kBasePointFacesPerFrame = 2;
    const int activePointCount = std::min(static_cast<int>(pointCasters.size()), kMaxShadowLights);
    for (int i = 0; i < activePointCount; ++i) {
        const ShadowCaster& caster = *pointCasters[i];
        const GLenum desiredCull = caster.cullFrontFaces ? GL_FRONT : GL_BACK;
        if (desiredCull != activeCullFace) {
            glCullFace(desiredCull);
            activeCullFace = desiredCull;
        }

        CubeShadow& shadow = m_pointShadows[caster.lightIndex];
        const bool needsInitialization = shadow.texture == 0;
        int facesToUpdate = caster.updateThisFrame ? kCubeFaceCount : kBasePointFacesPerFrame;
        if (needsInitialization)
            facesToUpdate = kCubeFaceCount;
        renderPoint(caster, shadow, meshManager, floor, renderGeometryCallback, facesToUpdate);

        shadow.info.type = ShadowType::Point;
        shadow.info.lightIndex = caster.lightIndex;
        shadow.info.matrixIndex = -1;
        shadow.info.enablePCF = caster.enablePCF;
        shadow.info.pcfKernelSize = caster.pcfKernelSize;
        shadow.info.bias = caster.shadowBias;
        shadow.info.slopeBias = caster.shadowSlopeBias;
        shadow.info.nearPlane = caster.shadowNearPlane;
        shadow.info.farPlane = caster.shadowFarPlane;
        shadow.info.cullFrontFaces = caster.cullFrontFaces;
        shadow.info.active = true;
        shadow.info.importance = caster.importance;
        shadow.info.lastUpdateFrame = caster.frameIndex;
        shadow.info.lastUpdateTime = caster.timestampSeconds;
        shadow.info.resolution = shadow.resolution;
        processedPointLights.insert(caster.lightIndex);
        ++pointRenderedCount;
        if (facesToUpdate == kCubeFaceCount)
            ++pointUpdatedCount;
    }

    for (int i = activePointCount; i < static_cast<int>(pointCasters.size()); ++i) {
        const ShadowCaster& caster = *pointCasters[i];
        if (releasePointShadow(caster.lightIndex))
            ++pointTrimmedCount;

        LightShadowInfo info;
        info.type = ShadowType::Point;
        info.lightIndex = caster.lightIndex;
        info.samplerIndex = -1;
        info.matrixIndex = -1;
        info.enablePCF = caster.enablePCF;
        info.pcfKernelSize = caster.pcfKernelSize;
        info.bias = caster.shadowBias;
        info.slopeBias = caster.shadowSlopeBias;
        info.nearPlane = caster.shadowNearPlane;
        info.farPlane = caster.shadowFarPlane;
        info.cullFrontFaces = caster.cullFrontFaces;
        info.active = false;
        info.importance = caster.importance;
        info.lastUpdateFrame = caster.frameIndex;
        info.lastUpdateTime = caster.timestampSeconds;
        info.resolution = caster.shadowResolution;
        m_lightToInfoIndex[caster.lightIndex] = static_cast<int>(m_lightInfos.size());
        m_lightInfos.push_back(info);
    }

    // Remove point shadows that were not processed this frame.
    for (auto it = m_pointShadows.begin(); it != m_pointShadows.end();) {
        if (processedPointLights.find(it->first) == processedPointLights.end()) {
            if (it->second.texture != 0) {
                glDeleteTextures(1, &it->second.texture);
                it->second.texture = 0;
            }
            it = m_pointShadows.erase(it);
            ++pointReleasedCount;
        } else {
            ++it;
        }
    }

    // Enforce maximum active point shadows by trimming the lowest-importance entries.
    if (m_pointShadows.size() > kMaxShadowLights) {
        std::vector<std::pair<float, int>> ranked;
        ranked.reserve(m_pointShadows.size());
        for (const auto& [lightIndex, shadow] : m_pointShadows)
            ranked.emplace_back(shadow.info.importance, lightIndex);

        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            if (a.first == b.first)
                return a.second < b.second;
            return a.first > b.first;
        });

        for (std::size_t i = kMaxShadowLights; i < ranked.size(); ++i) {
            if (releasePointShadow(ranked[i].second))
                ++pointTrimmedCount;
        }
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevDrawFbo == 0 && prevDrawBuffer == GL_NONE)
        glDrawBuffer(GL_BACK);
    else
        glDrawBuffer(prevDrawBuffer);
    if (prevReadFbo == 0 && prevReadBuffer == GL_NONE)
        glReadBuffer(GL_BACK);
    else
        glReadBuffer(prevReadBuffer);
    glCullFace(prevCullFaceMode);
    if (!cullFaceEnabled)
        glDisable(GL_CULL_FACE);
    else
        glEnable(GL_CULL_FACE);
    if (!depthTestEnabled)
        glDisable(GL_DEPTH_TEST);

    // Rebuild cube shadow outputs
    m_shadowCubeTextures.fill(0);
    m_shadowCubeResolutions.fill(0);
    m_shadowCubeLightIndices.fill(-1);

    std::vector<std::pair<float, int>> ordering;
    ordering.reserve(m_pointShadows.size());
    for (const auto& [lightIndex, shadow] : m_pointShadows)
        ordering.emplace_back(shadow.info.importance, lightIndex);

    std::sort(ordering.begin(), ordering.end(), [](const auto& a, const auto& b) {
        if (a.first == b.first)
            return a.second < b.second;
        return a.first > b.first;
    });

    m_shadowCubeCount = 0;
    for (const auto& [importance, lightIndex] : ordering) {
        (void)importance;
        if (m_shadowCubeCount >= kMaxShadowLights)
            break;
        CubeShadow& shadow = m_pointShadows[lightIndex];
        int slot = m_shadowCubeCount;
        m_shadowCubeTextures[slot] = shadow.texture;
        m_shadowCubeResolutions[slot] = shadow.resolution;
        m_shadowCubeLightIndices[slot] = lightIndex;
        shadow.info.samplerIndex = slot;

        m_lightToInfoIndex[lightIndex] = static_cast<int>(m_lightInfos.size());
        m_lightInfos.push_back(shadow.info);
        ++m_shadowCubeCount;
    }

    m_lastFrameStats.pointActive = m_shadowCubeCount;
    m_lastFrameStats.pointUpdated = pointUpdatedCount;
    m_lastFrameStats.pointReleased = pointReleasedCount;
    m_lastFrameStats.pointTrimmed = pointTrimmedCount;

    uploadUniforms();
}
