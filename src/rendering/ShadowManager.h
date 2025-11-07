// SPDX-License-Identifier: MIT
#pragma once

#include <framework/opengl_includes.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <unordered_map>
#include <vector>

class MeshManager;
class ProceduralFloor;

class ShadowManager {
public:
    static constexpr int kMaxShadowLights = 4;

    enum class ShadowType {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    struct ShadowCaster {
        ShadowType type { ShadowType::Point };
        int lightIndex { -1 };
        bool castsShadows { false };
        bool enablePCF { true };
        int pcfKernelSize { 6 };
        int shadowResolution { 512 };
        float shadowBias { 0.015f };
        float shadowSlopeBias { 0.0f };
        float shadowNearPlane { 0.1f };
        float shadowFarPlane { 50.0f };
        glm::vec3 position { 0.0f }; // also used as focus point for directional
        glm::vec3 direction { 0.0f, -1.0f, 0.0f };
        float range { 15.0f };
        float innerConeDegrees { 20.0f };
        float outerConeDegrees { 30.0f };
        glm::vec3 orthoExtents { 20.0f, 20.0f, 25.0f };
        bool cullFrontFaces { true };
        bool active { true };
        bool updateThisFrame { true };
        float importance { 1.0f };
        std::uint64_t frameIndex { 0 };
        double timestampSeconds { 0.0 };
    };

    struct ShadowUniform {
        glm::mat4 matrix { 1.0f };
        glm::vec4 params { 0.0f }; // near, far, invResolution, type
    };

    struct LightShadowInfo {
        ShadowType type { ShadowType::Point };
        int lightIndex { -1 };
        int samplerIndex { -1 }; // slot in texture array/group (0..kMaxShadowLights-1)
        int matrixIndex { -1 };   // index into uniform buffer for directional/spot
        int resolution { 0 };
        bool enablePCF { true };
        int pcfKernelSize { 3 };
        float bias { 0.0015f };
        float slopeBias { 0.0f };
        float nearPlane { 0.1f };
        float farPlane { 50.0f };
        bool cullFrontFaces { true };
        bool active { true };
        std::uint64_t lastUpdateFrame { 0 };
        double lastUpdateTime { 0.0 };
        float importance { 1.0f };
    };

    struct FrameStatistics {
        int pointActive { 0 };
        int pointUpdated { 0 };
        int pointReleased { 0 };
        int pointTrimmed { 0 };
    };

    ShadowManager();
    ~ShadowManager();

    ShadowManager(const ShadowManager&) = delete;
    ShadowManager& operator=(const ShadowManager&) = delete;

    void renderShadows(const std::vector<ShadowCaster>& casters,
        MeshManager& meshManager,
        ProceduralFloor* floor,
        const std::function<void(const glm::mat4&, MeshManager&, ProceduralFloor*)>& renderGeometryCallback);

    [[nodiscard]] const std::vector<ShadowUniform>& uniforms() const { return m_shadowUniforms; }
    [[nodiscard]] GLuint uniformBuffer() const { return m_shadowMatricesBuffer; }

    [[nodiscard]] const std::array<GLuint, kMaxShadowLights>& shadow2DTextures() const { return m_shadow2DTextures; }
    [[nodiscard]] const std::array<GLuint, kMaxShadowLights>& shadowCubeTextures() const { return m_shadowCubeTextures; }
    [[nodiscard]] const std::array<int, kMaxShadowLights>& shadow2DResolutions() const { return m_shadow2DResolutions; }
    [[nodiscard]] const std::array<int, kMaxShadowLights>& shadowCubeResolutions() const { return m_shadowCubeResolutions; }
    [[nodiscard]] const std::array<int, kMaxShadowLights>& shadow2DLightIndices() const { return m_shadow2DLightIndices; }
    [[nodiscard]] const std::array<int, kMaxShadowLights>& shadowCubeLightIndices() const { return m_shadowCubeLightIndices; }
    [[nodiscard]] int shadow2DCount() const { return m_shadow2DCount; }
    [[nodiscard]] int shadowCubeCount() const { return m_shadowCubeCount; }

    [[nodiscard]] const std::vector<LightShadowInfo>& lightInfos() const { return m_lightInfos; }
    [[nodiscard]] std::optional<LightShadowInfo> infoForLight(int lightIndex) const;
    [[nodiscard]] GLuint dummyCubeTexture() const { return m_dummyCubeTexture; }
    [[nodiscard]] GLuint dummy2DTexture() const { return m_dummy2DTexture; }
    [[nodiscard]] GLuint spotSamplerCompare() const { return m_spotSamplerCompare; }
    [[nodiscard]] GLuint spotSamplerRaw() const { return m_spotSamplerRaw; }
    [[nodiscard]] GLuint pointSamplerCompare() const { return m_pointSamplerCompare; }
    [[nodiscard]] GLuint shadowArrayTexture() const { return m_shadowArrayTexture; }
    [[nodiscard]] int shadowArrayResolution() const { return m_shadowArrayResolution; }
    [[nodiscard]] int shadowArrayLayers() const { return m_shadowArrayLayers; }
    [[nodiscard]] const FrameStatistics& frameStats() const { return m_lastFrameStats; }

private:
    struct CubeMatrices {
        std::array<glm::mat4, 6> view;
        std::array<glm::mat4, 6> proj;
        std::array<glm::mat4, 6> viewProj;
    };

    struct CachedCubeMatrices {
        float nearPlane { 0.1f };
        float farPlane { 100.0f };
        glm::vec3 position { 0.0f };
        CubeMatrices matrices {};
    };

    struct CubeShadow {
        GLuint texture { 0 };
        int resolution { 0 };
        LightShadowInfo info {};
        int nextFaceToUpdate { 0 };
    };

    void ensureFramebuffer();
    void ensureUniformBuffer(std::size_t matrixCount);

    void ensure2DTexture(int slot, int resolution);
    void ensureCubeTexture(CubeShadow& shadow, int resolution);
    void ensureDummyCubeTexture();
    void ensureDummy2DTexture();
    void ensureSamplers();
    void ensureShadowArrayTexture(int layers, int resolution);
    void destroyShadowArrayTexture();
    void updateShadowArrayTexture(int activeLayers);

    void uploadUniforms();

    void renderDirectionalOrSpot(const ShadowCaster& caster,
        int slot,
        int targetResolution,
        MeshManager& meshManager,
        ProceduralFloor* floor,
        const std::function<void(const glm::mat4&, MeshManager&, ProceduralFloor*)>& renderGeometryCallback);

    void renderPoint(const ShadowCaster& caster,
        CubeShadow& shadow,
        MeshManager& meshManager,
        ProceduralFloor* floor,
        const std::function<void(const glm::mat4&, MeshManager&, ProceduralFloor*)>& renderGeometryCallback,
        int facesToUpdate);

    CubeMatrices buildPointMatrices(const ShadowCaster& caster) const;
    glm::mat4 buildDirectionalView(const ShadowCaster& caster) const;
    glm::mat4 buildSpotView(const ShadowCaster& caster) const;
    glm::mat4 buildDirectionalProjection(const ShadowCaster& caster) const;
    glm::mat4 buildSpotProjection(const ShadowCaster& caster) const;

    GLuint m_framebuffer { 0 };
    GLuint m_shadowMatricesBuffer { 0 };

    std::array<GLuint, kMaxShadowLights> m_shadow2DTextures {};
    std::array<int, kMaxShadowLights> m_shadow2DResolutions {};
    std::array<int, kMaxShadowLights> m_shadow2DLightIndices {};
    int m_shadow2DCount { 0 };
    int m_shadowCubeCount { 0 };

    std::array<GLuint, kMaxShadowLights> m_shadowCubeTextures {};
    std::array<int, kMaxShadowLights> m_shadowCubeResolutions {};
    std::array<int, kMaxShadowLights> m_shadowCubeLightIndices {};

    GLuint m_spotSamplerCompare { 0 };
    GLuint m_spotSamplerRaw { 0 };
    GLuint m_pointSamplerCompare { 0 };

    GLuint m_shadowArrayTexture { 0 };
    int m_shadowArrayResolution { 0 };
    int m_shadowArrayLayers { 0 };

    GLuint m_dummyCubeTexture { 0 };
    GLuint m_dummy2DTexture { 0 };

    FrameStatistics m_lastFrameStats {};

    std::vector<ShadowUniform> m_shadowUniforms;
    std::vector<LightShadowInfo> m_lightInfos;

    mutable std::unordered_map<int, int> m_lightToInfoIndex;
    std::unordered_map<int, CubeShadow> m_pointShadows;
};
