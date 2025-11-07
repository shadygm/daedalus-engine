// SPDX-License-Identifier: MIT
#pragma once

#include <framework/opengl_includes.h>
#include <framework/shader.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>
#include <vector>

class CameraEffectsStage {
public:
    struct Settings {
        bool bloomEnabled { true }; // Meh
        bool lensFlareEnabled { false }; // nope
        bool chromaticAberrationEnabled { false }; // Nope
        bool vignetteEnabled { true }; // Yes
        bool depthOfFieldEnabled { false }; // Kinda
        bool motionBlurEnabled { false }; // Nope
        bool colorGradingEnabled { true }; // Kinda
        bool filmGrainEnabled { false };  // Meh

        // MSAA settings
        bool msaaEnabled { true };
        int msaaSamples { 4 }; // 2, 4, 8, or 16

        float exposure { 0.0f };
        float gamma { 1.0f };
        float contrast { 1.0f };
        float saturation { 1.0f };

        struct Bloom {
            float strength { 0.02f };
            float radius { 0.005f };
            float softKnee { 0.4f };
            float threshold { 0.575f };
            float dirtIntensity { 0.0f };
            int mipCount { 6 };
            bool useSoftThreshold { true };
            bool useLegacyBloom { true };
            bool useKarisAverage { true };
        } bloom;

        struct LensFlare {
            float intensity { 0.35f };
            float ghostCount { 3.0f };
            float haloRadius { 1.2f };
            float chromaBoost { 0.35f };
            float ghostSpacing { 0.45f };
            float ghostThreshold { 0.25f };
            float haloThickness { 0.35f };
            float starburstStrength { 0.0f };
        } lensFlare;

        struct Chromatic {
            float strength { 0.0025f };
            float radialStrength { 1.0f };
            float tangentialStrength { 0.35f };
            float falloff { 1.5f };
        } chromatic;

        struct Vignette {
            float innerRadius { 0.15f };
            float outerRadius { 0.9f };
            float power { 2.2f };
            float intensity { 0.85f };
        } vignette;

        struct DepthOfField {
            float focusDistance { 8.0f };
            float focusRange { 4.0f };
            float maxBlurRadius { 12.0f };
            float bokehBias { 0.0f };
        } dof;

        struct MotionBlur {
            float strength { 1.0f };
            float sampleCount { 6.0f };
            float shutterScale { 1.0f };
        } motionBlur;

        struct ColorGrade {
            glm::vec3 lift { 0.0f, 0.0f, 0.0f };
            glm::vec3 gamma { 1.0f, 1.0f, 1.0f };
            glm::vec3 gain { 1.0f, 1.0f, 1.0f };
        } colorGrade;

        struct FilmGrain {
            float amount { 0.04f };
            float response { 0.6f };
            float temporalScale { 1.0f };
            float seed { 19.0f };
        } filmGrain;

        struct Outline {
            bool enabled { false };
            float strength { 1.0f };
            float depthThreshold { 0.6f };
            float normalThreshold { 0.3f };
            bool useNormalEdges { true };
            bool previewEdgeMask { false };
        } outline;
    };

    void initialize(const std::filesystem::path& shaderDirectory, glm::ivec2 framebufferSize);
    void shutdown();

    void resize(glm::ivec2 framebufferSize);

    void beginSceneCapture(glm::ivec2 framebufferSize, const Settings& settings);
    void endSceneCapture();

    void updateUniforms(const Settings& settings, glm::ivec2 framebufferSize, float deltaTime, float nearPlane, float farPlane);

    void drawPostProcess(glm::ivec2 framebufferSize, GLuint targetFramebuffer = 0);
    void drawOutlinePass(const Settings& settings, glm::ivec2 framebufferSize, GLuint sourceColor, GLuint sourceDepth, GLuint targetFramebuffer, float nearPlane, float farPlane);

    void drawImGuiPanel(Settings& settings);

    [[nodiscard]] GLuint sceneColorTexture() const { return m_sceneColor; }
    [[nodiscard]] GLuint sceneDepthTexture() const { return m_sceneDepth; }
    [[nodiscard]] GLuint velocityTexture() const { return m_velocityTexture; }
    [[nodiscard]] GLuint sceneFramebuffer() const { return m_framebuffer; }

private:
    static constexpr GLuint kSettingsBinding = 5;

    struct alignas(16) GpuSettings {
        glm::vec4 togglesA { 0.0f };
        glm::vec4 togglesB { 0.0f };
        glm::vec4 exposureParams { 0.0f };
    glm::vec4 bloomParams { 0.0f };
    glm::vec4 bloomAdvanced { 0.0f };
        glm::vec4 lensFlareParams { 0.0f };
        glm::vec4 lensFlareShape { 0.0f };
        glm::vec4 chromaticParams { 0.0f };
        glm::vec4 vignetteParams { 0.0f };
        glm::vec4 dofParams { 0.0f };
        glm::vec4 motionBlurParams { 0.0f };
        glm::vec4 colorGradeLift { 0.0f };
        glm::vec4 colorGradeGamma { 1.0f };
        glm::vec4 colorGradeGain { 1.0f };
        glm::vec4 grainParams { 0.0f };
        glm::vec4 depthParams { 0.0f };
        glm::vec4 resolutionParams { 0.0f };
    };


    void ensureResources();
    void ensureFramebuffer(glm::ivec2 size);
    void ensureMSAAFramebuffer(glm::ivec2 size, int samples);
    void ensureBloomShaders();
    void ensureBloomResources();
    void destroyBloomMipChain();
    void ensureBloomMipChain(glm::ivec2 baseSize, int mipCount);
    GLuint runBloom(GLuint sourceTexture, glm::ivec2 sourceSize);
    void ensureQuad();
    void ensureShader();
    void ensureUniformBuffer();
    void ensureFallbackTextures();
    void uploadSettingsIfNeeded();
    void drawFullscreenQuad();

    std::filesystem::path m_shaderDirectory;
    Shader m_shader;

    Shader m_outlineShader;
    Shader m_bloomDownsampleShader;
    Shader m_bloomUpsampleShader;


    GLuint m_settingsUbo { 0 };
    GLuint m_framebuffer { 0 };
    GLuint m_sceneColor { 0 };
    GLuint m_sceneDepth { 0 };
    
    // MSAA resources
    GLuint m_msaaFramebuffer { 0 };
    GLuint m_msaaColorRBO { 0 };
    GLuint m_msaaDepthRBO { 0 };
    int m_currentMsaaSamples { 0 };
    bool m_msaaEnabled { false };
    GLuint m_velocityTexture { 0 };
    GLuint m_lensDirtTexture { 0 };
    GLuint m_bloomFramebuffer { 0 };
    struct BloomMip {
        glm::ivec2 size { 0 };
        GLuint texture { 0 };
    };
    std::vector<BloomMip> m_bloomMips;
    GLuint m_bloomResult { 0 };
    GLuint m_quadVao { 0 };
    GLuint m_quadVbo { 0 };
    GLint m_prevDrawBufferEnum { GL_BACK };
    GLint m_prevReadBufferEnum { GL_BACK };

    int m_cachedMipCount { 0 };

    glm::ivec2 m_framebufferSize { 0 };
    glm::ivec2 m_bloomBaseSize { 0 };
    bool m_initialized { false };
    bool m_settingsDirty { true };
    bool m_cachedSettingsValid { false };

    GLint m_prevDrawFramebuffer { 0 };
    GLint m_prevReadFramebuffer { 0 };
    GLint m_prevViewport[4] { 0, 0, 0, 0 };
    bool m_restoreFramebuffer { false };
    bool m_restoreViewport { false };

    GpuSettings m_gpuSettings {};
    float m_time { 0.0f };
    Settings m_cachedSettings {};
};
