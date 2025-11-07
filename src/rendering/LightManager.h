// SPDX-License-Identifier: MIT
#pragma once

#include <framework/opengl_includes.h>
#include <framework/shader.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class MeshManager;
class ProceduralFloor;

class LightManager {
public:
    static constexpr int kMaxShadowLights = 8;
    static constexpr int kShadowMapResolution = 2048;

    enum class LightType {
        Point = 0,
        Spot = 1
    };

    struct Light {
        LightType type { LightType::Point };
        std::string name;
        bool enabled { true };
        bool castsShadows { false };
        glm::vec3 color { 1.0f, 1.0f, 1.0f };
        float intensity { 5.0f };
    bool useAttenuation { true };
    float attenuationConstant { 1.0f };
    float attenuationLinear { 0.0f };
    float attenuationQuadratic { 0.0f };
        glm::vec3 position { 0.0f, 5.0f, 0.0f };
        glm::vec3 direction { 0.0f, -1.0f, 0.0f };
        float range { 15.0f };
        float innerConeDegrees { 20.0f };
        float outerConeDegrees { 30.0f };
        float shadowBias { 0.0015f };
        float shadowNearPlane { 0.1f };
        float shadowFarPlane { 50.0f };
    };

    struct GpuBinding {
        GLuint lightSSBO { 0 };
        GLuint shadowMatricesUBO { 0 };
    GLuint directionalShadowTexture { 0 };
    GLuint directionalShadowFallback { 0 };
    std::array<GLuint, kMaxShadowLights> pointShadowTextures { { 0 } };
        GLuint pointShadowFallback { 0 };
        int pointShadowCount { 0 };
        int lightCount { 0 };
        int directionalLightCount { 0 };
    };

    enum class GizmoMode {
        None,
        Translate,
        Rotate,
        TranslateRotate
    };

    LightManager();
    ~LightManager();

    void drawImGui();
    void drawImGuiPanel();
    void drawShadowDebugPanel();
    void renderShadowMaps(const glm::mat4& cameraView,
        const glm::mat4& cameraProjection,
        const glm::vec3& cameraPosition,
        MeshManager& meshManager,
        ProceduralFloor* floor);
    void updateGpuData();

    [[nodiscard]] const GpuBinding& gpuBinding() const { return m_gpuBinding; }

    [[nodiscard]] const std::vector<Light>& lights() const { return m_lights; }
    [[nodiscard]] std::vector<Light>& lights() { return m_lights; }

    [[nodiscard]] Light* findLightByName(const std::string& name);
    [[nodiscard]] const Light* findLightByName(const std::string& name) const;
    [[nodiscard]] Light& ensureLight(const std::string& name, LightType type);

    void setSelectedIndex(int index);
    [[nodiscard]] int selectedIndex() const { return m_selectedIndex; }
    [[nodiscard]] Light* selectedLight();
    [[nodiscard]] const Light* selectedLight() const;
    
    void markDirty();

private:
    struct alignas(16) GpuLight {
        glm::vec4 positionType { 0.0f };
        glm::vec4 directionRange { 0.0f };
        glm::vec4 colorIntensity { 0.0f };
        glm::vec4 spotShadow { 0.0f };
        glm::vec4 shadowParams { 0.0f };
        glm::vec4 attenuation { 0.0f };
        glm::vec4 extra { 0.0f };
    };

    struct ShadowEntry {
        int lightIndex { -1 };
        int layerIndex { -1 };
        LightType type { LightType::Point };
        glm::mat4 viewMatrix { 1.0f };
        glm::mat4 projectionMatrix { 1.0f };
        glm::vec3 lightPosition { 0.0f, 0.0f, 0.0f };
        float nearPlane { 0.1f };
        float farPlane { 100.0f };
    };

    struct PointShadowEntry {
        int lightIndex { -1 };
        GLuint cubemap { 0 };
        int resolution { 0 };
        glm::vec3 lightPosition { 0.0f, 0.0f, 0.0f };
        float nearPlane { 0.1f };
        float farPlane { 100.0f };
        float slopeBias { 0.0f };
        float constantBias { 0.0f };
    };

    Light& addLight(LightType type);
    void removeLight(int index);
    void ensureDefaultLight();
    void destroyGpuBuffer();
    void rebuildGpuBuffer();
    void drawLightItem(int index, Light& light);
    static glm::vec3 sanitizeDirection(const glm::vec3& dir);
    void ensureShadowLayerMapping();
    void ensureShadowShader();
    void ensureShadowResources(std::size_t casterCount);
    void destroyShadowResources();
    void ensurePointShadowResources(std::size_t casterCount);
    void ensureShadowFallbackTexture();
    void ensurePointShadowFallbackTexture();
    void destroyPointShadowResources();
    bool isShadowCasterSupported(const Light& light) const;
    ShadowEntry buildShadowEntry(int lightIndex, const Light& light, const glm::vec3& cameraPosition) const;
    void bindShadowFramebuffer(const ShadowEntry& entry);
    void bindLayeredShadowFramebuffer();
    void renderShadowGeometry(bool layeredPass,
        MeshManager& meshManager,
        ProceduralFloor* floor,
        bool pointPass = false,
        const glm::mat4* lightViewProj = nullptr,
        const glm::vec3& lightPos = glm::vec3(0.0f),
        float nearPlane = 0.1f,
        float farPlane = 100.0f,
        int shadowLayerCount = 0);
    void ensurePointShadowInstancedShader();
    void renderPointShadowInstanced(const PointShadowEntry& entry,
        MeshManager& meshManager,
        ProceduralFloor* floor);
    void uploadShadowMatrices(const ShadowEntry* entries, int layerCount);
    void renderPointShadowFaces(GLuint cubemap,
        int resolution,
        const glm::vec3& lightPos,
        float nearPlane,
        float farPlane,
        float slopeBias,
        float constantBias,
        MeshManager& meshManager,
        ProceduralFloor* floor);
    void uploadShadowData(const std::vector<ShadowEntry>& entries, const std::vector<PointShadowEntry>& pointEntries);
    void ensureShadowDebugResources();
    void ensureShadowDebugShader();
    void updateShadowDebugPreview();
    void destroyShadowDebugResources();

    std::vector<Light> m_lights;
    int m_selectedIndex { -1 };
    bool m_editWithGizmo { false };
    bool m_gpuDirty { true };
    GLuint m_lightBuffer { 0 };
    GpuBinding m_gpuBinding {};
    std::uint32_t m_nextId { 1 };
    std::vector<int> m_shadowLayerForLight;
    GLuint m_shadowFramebuffer { 0 };
    GLuint m_shadowMapArray { 0 };
    GLuint m_shadowMatricesBuffer { 0 };
    GLuint m_shadowDummyTexture { 0 };
    Shader m_shadowShader;
    bool m_shadowShaderReady { false };
    std::vector<glm::mat4> m_shadowMatrices;
    std::vector<glm::vec4> m_shadowParams;
    bool m_shadowResourcesDirty { true };
    std::size_t m_shadowArrayLayers { 0 };
    std::vector<PointShadowEntry> m_pointShadowEntries;
    bool m_pointShadowResourcesDirty { true };
    std::vector<GLuint> m_pointShadowCubemaps;
    GLuint m_pointShadowSampler { 0 };
    GLuint m_pointShadowDummyTexture { 0 };
        Shader m_pointShadowInstancedShader;
        bool m_pointShadowInstancedShaderReady { false };
        GLuint m_pointShadowViewProjUBO { 0 };
        GLint m_pointShadowModelLocation { -1 };

    struct ShadowDebugLayer {
        int lightIndex { -1 };
        int layerIndex { -1 };
        LightType type { LightType::Point };
        float nearPlane { 0.1f };
        float farPlane { 1.0f };
    float bias { 0.0f };
        int resolution { 0 };
    };

    std::vector<ShadowDebugLayer> m_shadowDebugLayers;
    int m_shadowDebugSelectedLayer { 0 };
    bool m_shadowDebugLinearize { true };
    float m_shadowDebugContrast { 1.0f };
    bool m_shadowDebugDirty { true };
    GLuint m_shadowDebugTexture { 0 };
    GLuint m_shadowDebugFramebuffer { 0 };
    GLuint m_shadowDebugVao { 0 };
    GLuint m_shadowDebugVbo { 0 };
    GLuint m_shadowDebugSampler { 0 };
    glm::ivec2 m_shadowDebugResolution { 512, 512 };
    Shader m_shadowDebugShader;
    bool m_shadowDebugShaderReady { false };
    bool m_useLayeredShadows { true };
    bool m_usePointInstancedShadows { false }; // old, can be ignored for now
};
