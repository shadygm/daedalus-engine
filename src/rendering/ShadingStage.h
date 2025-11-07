// SPDX-License-Identifier: MIT
#pragma once

#include "mesh/MeshInstance.h"
#include "rendering/ShaderManager.h"
#include "rendering/TextureUnits.h"

#include <framework/opengl_includes.h>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>
#include <array>
#include <unordered_map>
#include <vector>
#include <optional>
#include <limits>
#include <cstdint>
#include <memory>

struct LightingSettings {
    enum class UVDebugTarget {
        Albedo = 0,
        Normal = 1,
        Metallic = 2,
        Roughness = 3,
        AmbientOcclusion = 4,
        Emissive = 5,
        Height = 6
    };

    glm::vec3 ambientColor { 1.0f, 1.0f, 1.0f };
    float ambientStrength { 0.1f };
    glm::vec3 lightColor { 1.0f, 1.0f, 1.0f };
    glm::vec3 lightPos { 2.0f, 4.0f, 2.0f };
    bool debugShowUVs { false };
    UVDebugTarget debugTarget { UVDebugTarget::Albedo };
};

class ShadingStage {
public:
    static constexpr GLuint kMaterialTextureUnitCount = TextureUnits::Material_Count;
    static constexpr GLuint kEnvIrradianceUnit = TextureUnits::Env_Irradiance;
    static constexpr GLuint kEnvPrefilterUnit = TextureUnits::Env_Prefilter;
    static constexpr GLuint kEnvBrdfUnit = TextureUnits::Env_BRDF;
    static constexpr GLuint kEnvSkyboxUnit = TextureUnits::Env_Skybox;
    static constexpr GLuint kShadowMapUnit = 7;

    // NOTE: EnvironmentManager::TU_Skybox must NOT use 8..(8 + kPointShadowUnitCount - 1).
    // Skybox currently binds to unit 27.
    static constexpr GLuint kPointShadowUnitBase = 8;
    static constexpr GLuint kPointShadowUnitCount = 8;

    static constexpr GLuint kLightSsboBinding = 0;
    static constexpr GLuint kShadowMatricesBinding = 1;
    static constexpr GLuint kMaterialSsboBinding = 2;
    static constexpr GLuint kPerFrameBinding = 3;
    static constexpr GLuint kPerObjectBinding = 4;

    explicit ShadingStage(const std::filesystem::path& shaderDirectory);
    ~ShadingStage();

    struct EnvironmentState {
        GLuint irradianceMap { 0 };
        GLuint prefilterMap { 0 };
        GLuint brdfLut { 0 };
        float intensity { 1.0f };
        float prefilterMipLevels { 0.0f };
        bool useIBL { false };

        [[nodiscard]] bool isValid() const
        {
            return irradianceMap != 0 && prefilterMap != 0 && brdfLut != 0;
        }
    };

    struct LightBufferBinding {
        GLuint lightSSBO { 0 };
        GLuint shadowMatricesUBO { 0 };
        GLuint directionalShadowTexture { 0 };
        GLuint directionalShadowFallback { 0 };
        std::array<GLuint, kPointShadowUnitCount> pointShadowTextures { { 0 } };
        GLuint pointShadowFallback { 0 };
        int pointShadowCount { 0 };
        int lightCount { 0 };
        int directionalLightCount { 0 };
    };

    void drawImGui(std::vector<MeshInstance>& instances, int selectedInstanceIndex);
    void drawImGuiPanel(std::vector<MeshInstance>& instances, int selectedInstanceIndex);

    void beginFrame(const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition);

    void endFrame();

    void apply(const glm::mat4& model,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPosition,
        const RenderMaterial& material,
        bool hasPrimaryUVs,
        bool hasSecondaryUVs,
        bool hasTangents);

    void setEnvironmentState(const EnvironmentState& state);
    [[nodiscard]] const EnvironmentState& environmentState() const { return m_environmentState; }

    // World curvature: when enabled, geometry positions in view space are curved
    // by subtracting strength * dist^2 from the view-space Y coordinate.
    void setWorldCurvatureEnabled(bool enabled) { m_worldCurvatureEnabled = enabled; }
    void setWorldCurvatureStrength(float s) { m_worldCurvatureStrength = s; }
    void setFogEnabled(bool enabled) { m_fogEnabled = enabled; }
    void setFogColor(const glm::vec3& c) { m_fogColor = c; }
    void setFogDensity(float d) { m_fogDensity = d; }
    void setFogGradient(float g) { m_fogGradient = g; }

    // Parallax mapping (basic: height map or normal.a fallback)
    void setParallaxEnabled(bool enabled) { m_parallaxEnabled = enabled; }
    void setParallaxScale(float s) { m_parallaxScale = s; }
    void setParallaxBias(float b) { m_parallaxBias = b; }
    void setParallaxUseNormalAlpha(bool v) { m_parallaxUseNormalAlpha = v; }
    void setParallaxHasHeightMap(bool v) { m_parallaxHasHeightMap = v; }

    void setLightBinding(const LightBufferBinding& binding);
    [[nodiscard]] const LightBufferBinding& lightBinding() const { return m_lightBinding; }

    [[nodiscard]] LightingSettings& settings();
    [[nodiscard]] const LightingSettings& settings() const;

    [[nodiscard]] Shader& shader();
    [[nodiscard]] const Shader& shader() const;

private:
    struct alignas(16) PerFrameData {
        glm::mat4 view { 1.0f };
        glm::mat4 projection { 1.0f };
        glm::mat4 viewProjection { 1.0f };
        glm::mat4 inverseView { 1.0f };
        glm::vec4 cameraPos { 0.0f, 0.0f, 0.0f, 1.0f };
        glm::vec4 lightPos { 0.0f };
        glm::vec4 lightColor { 1.0f };
        glm::vec4 ambientColorStrength { 1.0f, 1.0f, 1.0f, 0.1f };
        glm::ivec4 frameFlags { 0, 0, 0, 0 };
        glm::vec4 envParams { 0.0f };
    };

    struct alignas(16) MaterialGPUData {
        glm::vec4 baseColor { 1.0f };
        glm::vec4 diffuseColor { 1.0f };
        glm::vec4 specularColor { 0.04f, 0.04f, 0.04f, 0.0f };
        glm::vec4 emissiveColorIntensity { 0.0f, 0.0f, 0.0f, 0.0f };
        glm::vec4 pbrParams { 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 extraParams { 1.0f, 1.0f, 1.0f, 0.0f };
        glm::ivec4 textureUsage { 0, 0, 0, 0 };
        glm::ivec4 textureUsage2 { 0, 0, 0, 0 };
        glm::ivec4 uvSets0 { 0, 0, 0, 0 };
        glm::ivec4 uvSets1 { 0, 0, 0, 0 };
        glm::vec4 uvTransformAlbedo { 0.0f, 0.0f, 1.0f, 1.0f };
        glm::vec4 uvTransformMR { 0.0f, 0.0f, 1.0f, 1.0f };
        glm::vec4 uvTransformNormal { 0.0f, 0.0f, 1.0f, 1.0f };
        glm::vec4 uvTransformAO { 0.0f, 0.0f, 1.0f, 1.0f };
        glm::vec4 uvTransformEmissive { 0.0f, 0.0f, 1.0f, 1.0f };
        glm::vec4 uvRotations { 0.0f };
        glm::vec4 uvRotations2 { 0.0f };
    };

    struct alignas(16) ObjectGPUData {
        glm::mat4 model { 1.0f };
        glm::mat4 normalMatrix { 1.0f };
        glm::ivec4 materialFlags { 0, 0, 0, 0 };
        glm::ivec4 textureUsage { 0, 0, 0, 0 };
        glm::ivec4 textureUsage2 { 0, 0, 0, 0 };
        glm::ivec4 uvSets0 { 0, 0, 0, 0 };
        glm::ivec4 uvSets1 { 0, 0, 0, 0 };
    };

    struct MaterialBindingInfo {
        bool useAlbedo { false };
        bool useMetallicRoughness { false };
        bool useNormal { false };
        bool useAO { false };
        bool useEmissive { false };
        bool useHeight { false };
        int albedoUV { 0 };
        int metallicRoughnessUV { 0 };
        int normalUV { 0 };
        int aoUV { 0 };
        int emissiveUV { 0 };
        int heightUV { 0 };
        bool hasPrimaryUVs { false };
        bool hasSecondaryUVs { false };
    };

    struct MaterialRecord {
        const RenderMaterial* material { nullptr };
        std::uint32_t index { 0 };
        MaterialGPUData gpuData {};
        std::shared_ptr<Texture> textures[kMaterialTextureUnitCount];
        AlphaMode alphaMode { AlphaMode::Opaque };
        bool doubleSided { false };
        bool usePBR { true };
        bool unlit { false };
        bool dirty { true };
    };

    struct BoundMaterialState {
        std::uint32_t materialIndex { std::numeric_limits<std::uint32_t>::max() };
        MaterialBindingInfo bindingInfo {};
        bool valid { false };
        bool usePBR { true };
        AlphaMode alphaMode { AlphaMode::Opaque };
        bool doubleSided { false };
    };

    void ensureEnvSamplers() const;
    void ensureShadowSampler() const;
    void ensureBuffers();
    void ensureMaterialCapacity(std::size_t requiredCapacity);
    MaterialBindingInfo evaluateMaterialUsage(const RenderMaterial& material,
        bool hasPrimaryUVs,
        bool hasSecondaryUVs) const;
    MaterialGPUData buildMaterialData(const RenderMaterial& material) const;
    MaterialRecord& getOrCreateMaterialRecord(const RenderMaterial& material);
    void uploadMaterialRecord(MaterialRecord& record);
    void bindMaterialResources(MaterialRecord& record,
        const MaterialBindingInfo& bindingInfo,
        bool hasTangents);
    void rebindEnvironmentForPbr(const Shader& shader);
    void updateObjectBuffer(const glm::mat4& model,
        const MaterialRecord& record,
        const MaterialBindingInfo& bindingInfo,
        bool hasTangents,
        bool hasPrimaryUVs,
        bool hasSecondaryUVs);

    LightingSettings m_settings;
    ShaderManager m_shader;
    mutable Shader* m_activeShader { nullptr };
    mutable GLuint m_envCubeSampler { 0 };
    mutable GLuint m_env2DSampler { 0 };
    mutable GLuint m_envCubeSamplerMip   = 0;
    mutable GLuint m_envCubeSamplerNoMip = 0; 
    mutable GLuint m_shadowSamplerCompare = 0;
    mutable GLuint m_shadowSamplerCube = 0;

    bool m_enableDebugLogging { false };
    EnvironmentState m_environmentState {};
    LightBufferBinding m_lightBinding {};

    // world curvature state
    bool m_worldCurvatureEnabled { false };
    float m_worldCurvatureStrength { 0.001f };
    // fog parameters
    bool m_fogEnabled { false };
    glm::vec3 m_fogColor { 0.6f, 0.7f, 0.9f };
    float m_fogDensity { 0.01f };
    float m_fogGradient { 1.8f };

    // Parallax mapping state
    bool m_parallaxEnabled { false };
    float m_parallaxScale { 0.04f };
    float m_parallaxBias { -0.02f }; // typically -0.5 * scale
    bool m_parallaxUseNormalAlpha { true }; // fallback to normal.a as height
    bool m_parallaxHasHeightMap { false };  // if a dedicated height map is bound (not wired yet)
        bool m_parallaxInvertOffset { false };  // runtime sign flip for UV offset

    GLuint m_perFrameUBO { 0 };
    GLuint m_objectUBO { 0 };
    GLuint m_materialSSBO { 0 };
    std::size_t m_materialCapacity { 0 };

    PerFrameData m_frameData {};
    ObjectGPUData m_objectData {};
    bool m_frameActive { false };

    std::vector<MaterialRecord> m_materialRecords;
    std::unordered_map<const RenderMaterial*, std::uint32_t> m_materialLookup;
    BoundMaterialState m_boundMaterialState {};
};
