// SPDX-License-Identifier: MIT
#pragma once

#include <framework/shader.h>
#include <framework/opengl_includes.h>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

class EnvironmentManager {
public:
    struct AdvancedSettings {
        int environmentResolution { 4096 };
        int irradianceResolution { 1024 };
        int prefilterBaseResolution { 128 };
        int prefilterMipLevels { 8 };
    };

    EnvironmentManager(const std::filesystem::path& shaderDirectory);
    ~EnvironmentManager();

    EnvironmentManager(const EnvironmentManager&) = delete;
    EnvironmentManager& operator=(const EnvironmentManager&) = delete;

    void initializeGL();

    bool loadEnvironment(const std::filesystem::path& path);
    void unload();

    void drawSkybox(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) const;
    void sanitizeGeneratedTextures() const;

    void setUseIBL(bool enabled) { m_useIBL = enabled; }
    [[nodiscard]] bool useIBL() const { return m_useIBL; }

    void setSkyboxVisible(bool visible) { m_skyboxVisible = visible; }
    [[nodiscard]] bool skyboxVisible() const { return m_skyboxVisible; }

    void setEnvironmentIntensity(float intensity) { m_environmentIntensity = intensity; }
    [[nodiscard]] float environmentIntensity() const { return m_environmentIntensity; }

    void setAdvancedSettings(const AdvancedSettings& settings);
    [[nodiscard]] const AdvancedSettings& advancedSettings() const { return m_settings; }

    void setSkyboxUsePrefilter(bool enabled);
    [[nodiscard]] bool skyboxUsePrefilter() const { return m_debugSkyboxUsePrefilter; }
    void setSkyboxMipOverride(float mipLevel);
    void clearSkyboxMipOverride();
    [[nodiscard]] float skyboxMipOverride() const { return m_debugSkyboxMipOverride; }

    [[nodiscard]] GLuint environmentCubemap() const;
    [[nodiscard]] GLuint irradianceCubemap() const;
    [[nodiscard]] GLuint prefilterCubemap() const;
    [[nodiscard]] GLuint brdfLutTexture();
    [[nodiscard]] int prefilterMipLevelCount() const;

    void bindForPbr(const Shader& shader, int firstTextureUnit = 16) const;

    [[nodiscard]] bool hasEnvironment() const { return static_cast<bool>(m_currentEnvironment); }
    [[nodiscard]] const std::filesystem::path& currentEnvironmentPath() const { return m_currentPath; }

private:
    struct EnvironmentTextures {
        GLuint hdrTexture { 0 };
        GLuint envCubemap { 0 };
        GLuint irradianceCubemap { 0 };
        GLuint prefilteredCubemap { 0 };
        int prefilterMipLevels { 0 };

        ~EnvironmentTextures();
        EnvironmentTextures(const EnvironmentTextures&) = delete;
        EnvironmentTextures& operator=(const EnvironmentTextures&) = delete;
        EnvironmentTextures(EnvironmentTextures&& other) noexcept;
        EnvironmentTextures& operator=(EnvironmentTextures&& other) noexcept;
        EnvironmentTextures() = default;

        void reset();
    };

    struct CacheKeyHash {
        std::size_t operator()(const std::string& value) const noexcept { return std::hash<std::string> {}(value); }
    };

    [[nodiscard]] std::shared_ptr<EnvironmentTextures> bakeEnvironment(const std::filesystem::path& path);
    void ensureBrdfLut();
    void ensureCaptureResources();
    void ensureCubeGeometry();
    void ensureQuadGeometry();
    void renderCube() const;
    void renderFullscreenQuad() const;

    void convertEquirectangularToCubemap(EnvironmentTextures& textures, int cubeSize);
    void convolveIrradiance(EnvironmentTextures& textures, int irradianceSize);
    void prefilterSpecular(EnvironmentTextures& textures, int baseSize, int mipLevels);
    void generateBrdfLutTexture();

    [[nodiscard]] GLuint loadHdrTexture(const std::filesystem::path& path);

private:
    std::filesystem::path m_shaderDirectory;

    std::unordered_map<std::string, std::weak_ptr<EnvironmentTextures>, CacheKeyHash> m_cache;
    std::shared_ptr<EnvironmentTextures> m_currentEnvironment;
    std::filesystem::path m_currentPath;

    AdvancedSettings m_settings;

    bool m_useIBL { true };
    bool m_skyboxVisible { true };
    float m_environmentIntensity { 1.0f };
    bool m_debugSkyboxUsePrefilter { false };
    float m_debugSkyboxMipOverride { -1.0f };

    GLuint m_cubeVAO { 0 };
    GLuint m_cubeVBO { 0 };
    GLuint m_quadVAO { 0 };
    GLuint m_quadVBO { 0 };
    GLuint m_captureFBO { 0 };
    GLuint m_captureRBO { 0 };
    GLuint m_cubeSampler { 0 };
    GLuint m_lutSampler  { 0 };


    Shader m_equirectangularShader;
    Shader m_irradianceShader;
    Shader m_prefilterShader;
    Shader m_brdfShader;
    Shader m_skyboxShader;

    GLuint m_brdfLut { 0 };
    bool m_isInitialized { false };
    GLint m_bakeTextureUnit { -1 };

    [[nodiscard]] std::string createCacheKey(const std::filesystem::path& path) const;

    void destroyShaders();
};
