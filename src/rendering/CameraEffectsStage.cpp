// SPDX-License-Identifier: MIT
#include "rendering/CameraEffectsStage.h"
#include "rendering/TextureUnits.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()

#include <glad/glad.h>

#include <glm/common.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <cstdio>

namespace {

constexpr GLenum kColorAttachment = GL_COLOR_ATTACHMENT0;
constexpr GLenum kDepthAttachment = GL_DEPTH_ATTACHMENT;

constexpr std::array<float, 24> kFullscreenQuad = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
};

constexpr float kVelocityClear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
constexpr float kLensDirtWhite[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

[[nodiscard]] bool isValidSize(glm::ivec2 size)
{
    return size.x > 0 && size.y > 0;
}

#ifndef NDEBUG
void debugTraceFramebuffer(const char* label)
{
    GLint draw = 0;
    GLint read = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    std::fprintf(stderr, "[CameraEffectsStage] %s | draw=%d read=%d\n", label, draw, read);
}
#define TRACE_FBO(label) debugTraceFramebuffer(label)
#else
#define TRACE_FBO(label) ((void)0)
#endif

} // namespace

void CameraEffectsStage::initialize(const std::filesystem::path& shaderDirectory, glm::ivec2 framebufferSize)
{
    m_shaderDirectory = shaderDirectory;
    ensureShader();
    ensureBloomShaders();
    ensureQuad();
    ensureUniformBuffer();
    ensureFallbackTextures();
    ensureBloomResources();
    resize(framebufferSize);
    m_initialized = true;
}

void CameraEffectsStage::shutdown()
{
    if (m_quadVbo) glDeleteBuffers(1, &m_quadVbo);
    if (m_quadVao) glDeleteVertexArrays(1, &m_quadVao);
    if (m_sceneColor) glDeleteTextures(1, &m_sceneColor);
    if (m_sceneDepth) glDeleteTextures(1, &m_sceneDepth);
    if (m_velocityTexture) glDeleteTextures(1, &m_velocityTexture);
    if (m_lensDirtTexture) glDeleteTextures(1, &m_lensDirtTexture);
    if (m_framebuffer) glDeleteFramebuffers(1, &m_framebuffer);
    if (m_bloomFramebuffer) glDeleteFramebuffers(1, &m_bloomFramebuffer);
    if (m_settingsUbo) glDeleteBuffers(1, &m_settingsUbo);
    
    // Clean up MSAA resources
    if (m_msaaFramebuffer) glDeleteFramebuffers(1, &m_msaaFramebuffer);
    if (m_msaaColorRBO) glDeleteRenderbuffers(1, &m_msaaColorRBO);
    if (m_msaaDepthRBO) glDeleteRenderbuffers(1, &m_msaaDepthRBO);

    destroyBloomMipChain();

    m_quadVbo = m_quadVao = 0;
    m_sceneColor = m_sceneDepth = 0;
    m_velocityTexture = 0;
    m_lensDirtTexture = 0;
    m_framebuffer = 0;
    m_bloomFramebuffer = 0;
    m_settingsUbo = 0;
    m_msaaFramebuffer = 0;
    m_msaaColorRBO = 0;
    m_msaaDepthRBO = 0;
    m_currentMsaaSamples = 0;
    m_msaaEnabled = false;
    m_bloomResult = 0;
    m_framebufferSize = glm::ivec2(0);
    m_bloomBaseSize = glm::ivec2(0);
    m_initialized = false;
    m_time = 0.0f;
    m_settingsDirty = true;
    m_cachedSettingsValid = false;
}

void CameraEffectsStage::resize(glm::ivec2 framebufferSize)
{
    if (!isValidSize(framebufferSize))
        return;

    ensureFramebuffer(framebufferSize);
    ensureBloomResources();
    m_bloomResult = 0;
}

void CameraEffectsStage::beginSceneCapture(glm::ivec2 framebufferSize, const Settings& settings)
{
    ensureResources();
    ensureFramebuffer(framebufferSize);
    
    // Store MSAA settings for this frame
    m_msaaEnabled = settings.msaaEnabled;
    
    // Create MSAA framebuffer if needed
    if (m_msaaEnabled) {
        ensureMSAAFramebuffer(framebufferSize, settings.msaaSamples);
    }

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_prevDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_prevReadFramebuffer);
    glGetIntegerv(GL_VIEWPORT, m_prevViewport);
    m_restoreFramebuffer = true;
    m_restoreViewport = true;

    glGetIntegerv(GL_DRAW_BUFFER, &m_prevDrawBufferEnum);
    glGetIntegerv(GL_READ_BUFFER, &m_prevReadBufferEnum);

    // Bind the appropriate FBO (MSAA or regular)
    GLuint targetFBO = m_msaaEnabled ? m_msaaFramebuffer : m_framebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);

    const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuf);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    glViewport(0, 0, framebufferSize.x, framebufferSize.y);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    TRACE_FBO("beginSceneCapture bound capture FBO");

    m_bloomResult = 0;
}

void CameraEffectsStage::endSceneCapture()
{
    TRACE_FBO("endSceneCapture entry");

    // If MSAA was enabled, resolve the multisampled framebuffer to the single-sample texture
    if (m_msaaEnabled && m_msaaFramebuffer != 0 && m_framebuffer != 0) {
        // Blit from MSAA FBO to regular FBO (this resolves the MSAA)
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_framebuffer);
        
        glBlitFramebuffer(
            0, 0, m_framebufferSize.x, m_framebufferSize.y,  // src
            0, 0, m_framebufferSize.x, m_framebufferSize.y,  // dst
            GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,        // mask
            GL_NEAREST                                         // filter
        );
        
        TRACE_FBO("endSceneCapture after MSAA resolve");
    }

    // Go to default FB for the bloom/downsample work that follows
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    TRACE_FBO("endSceneCapture after bind default");

    // If something left the default FB with NONE, fix it so post doesn’t go black
    {
        GLint curDraw = 0, curRead = 0;
        glGetIntegerv(GL_DRAW_BUFFER, &curDraw);
        glGetIntegerv(GL_READ_BUFFER, &curRead);
        if (curDraw == GL_NONE) glDrawBuffer(GL_BACK);
        if (curRead == GL_NONE) glReadBuffer(GL_BACK);
    }

    // mark bloom result dirty so it will be recomputed during post-process
    m_bloomResult = 0;

    // -----------------------------------------
    // Restore previous FB + viewport + draw/read buffers
    // -----------------------------------------
    if (m_restoreFramebuffer) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(m_prevDrawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(m_prevReadFramebuffer));

        // restore draw/read buffer exactly as they were
        // but guard against restoring GL_NONE on a window
        if (m_prevDrawBufferEnum == GL_NONE && m_prevDrawFramebuffer == 0)
            glDrawBuffer(GL_BACK);
        else
            glDrawBuffer(m_prevDrawBufferEnum);

        if (m_prevReadBufferEnum == GL_NONE && m_prevReadFramebuffer == 0)
            glReadBuffer(GL_BACK);
        else
            glReadBuffer(m_prevReadBufferEnum);

        TRACE_FBO("endSceneCapture restored previous framebuffer");
    }

    if (m_restoreViewport) {
        glViewport(m_prevViewport[0], m_prevViewport[1],
                   m_prevViewport[2], m_prevViewport[3]);
    }

    m_restoreFramebuffer = false;
    m_restoreViewport    = false;
}
void CameraEffectsStage::updateUniforms(const Settings& settings,
    glm::ivec2 framebufferSize,
    float deltaTime,
    float nearPlane,
    float farPlane)
{
    Settings sanitized = settings;
    sanitized.bloom.strength = glm::clamp(sanitized.bloom.strength, 0.0f, 5.0f);
    sanitized.bloom.radius = glm::clamp(sanitized.bloom.radius, 0.0001f, 0.1f);
    sanitized.bloom.softKnee = glm::clamp(sanitized.bloom.softKnee, 0.0f, 1.0f);
    sanitized.bloom.threshold = glm::max(sanitized.bloom.threshold, 0.0f);
    sanitized.bloom.mipCount = std::clamp(sanitized.bloom.mipCount, 1, 8);
    sanitized.bloom.dirtIntensity = glm::max(sanitized.bloom.dirtIntensity, 0.0f);

    if (!isValidSize(framebufferSize))
        framebufferSize = m_framebufferSize;

    m_time += std::max(deltaTime, 0.0f);

    m_gpuSettings.togglesA = glm::vec4(
        sanitized.bloomEnabled ? 1.0f : 0.0f,
        sanitized.lensFlareEnabled ? 1.0f : 0.0f,
        sanitized.chromaticAberrationEnabled ? 1.0f : 0.0f,
        sanitized.vignetteEnabled ? 1.0f : 0.0f);

    m_gpuSettings.togglesB = glm::vec4(
        sanitized.depthOfFieldEnabled ? 1.0f : 0.0f,
        sanitized.motionBlurEnabled ? 1.0f : 0.0f,
        sanitized.colorGradingEnabled ? 1.0f : 0.0f,
        sanitized.filmGrainEnabled ? 1.0f : 0.0f);

    m_gpuSettings.exposureParams = glm::vec4(sanitized.exposure, sanitized.gamma, sanitized.contrast, sanitized.saturation);
    m_gpuSettings.bloomParams = glm::vec4(
        sanitized.bloomEnabled ? 1.0f : 0.0f,
        sanitized.bloom.strength,
        sanitized.bloom.softKnee,
        sanitized.bloom.radius);
    m_gpuSettings.bloomAdvanced = glm::vec4(
        sanitized.bloom.threshold,
        sanitized.bloom.useSoftThreshold ? 1.0f : 0.0f,
        sanitized.bloom.dirtIntensity,
        sanitized.bloom.useLegacyBloom ? 1.0f : 0.0f);
    m_gpuSettings.lensFlareParams = glm::vec4(
        sanitized.lensFlare.intensity,
        sanitized.lensFlare.ghostCount,
        sanitized.lensFlare.haloRadius,
        sanitized.lensFlare.chromaBoost);
    m_gpuSettings.lensFlareShape = glm::vec4(
        sanitized.lensFlare.ghostSpacing,
        sanitized.lensFlare.ghostThreshold,
        sanitized.lensFlare.haloThickness,
        sanitized.lensFlare.starburstStrength);
    m_gpuSettings.chromaticParams = glm::vec4(sanitized.chromatic.strength, sanitized.chromatic.radialStrength, sanitized.chromatic.tangentialStrength, sanitized.chromatic.falloff);
    m_gpuSettings.vignetteParams = glm::vec4(sanitized.vignette.innerRadius, sanitized.vignette.outerRadius, sanitized.vignette.power, sanitized.vignette.intensity);
    m_gpuSettings.dofParams = glm::vec4(sanitized.dof.focusDistance, sanitized.dof.focusRange, sanitized.dof.maxBlurRadius, sanitized.dof.bokehBias);
    m_gpuSettings.motionBlurParams = glm::vec4(sanitized.motionBlur.strength, sanitized.motionBlur.sampleCount, sanitized.motionBlur.shutterScale, 0.0f);
    m_gpuSettings.colorGradeLift = glm::vec4(sanitized.colorGrade.lift, 0.0f);
    m_gpuSettings.colorGradeGamma = glm::vec4(sanitized.colorGrade.gamma, 0.0f);
    m_gpuSettings.colorGradeGain = glm::vec4(sanitized.colorGrade.gain, 0.0f);
    m_gpuSettings.grainParams = glm::vec4(
        sanitized.filmGrain.amount,
        sanitized.filmGrain.response,
        m_time * sanitized.filmGrain.temporalScale,
        sanitized.filmGrain.seed);

    const float safeNear = std::max(nearPlane, 0.0001f);
    const float safeFar = std::max(farPlane, safeNear + 0.0001f);

    m_gpuSettings.depthParams = glm::vec4(safeNear, safeFar, 1.0f / safeNear, 1.0f / safeFar);

    const float width = static_cast<float>(std::max(framebufferSize.x, 1));
    const float height = static_cast<float>(std::max(framebufferSize.y, 1));
    m_gpuSettings.resolutionParams = glm::vec4(width, height, 1.0f / width, 1.0f / height);

    m_settingsDirty = true;
    m_cachedSettings = sanitized;
    m_cachedSettingsValid = true;
}

void CameraEffectsStage::drawPostProcess(glm::ivec2 framebufferSize, GLuint targetFramebuffer)
{
    if (!m_initialized)
        return;

    if (!isValidSize(framebufferSize))
        framebufferSize = m_framebufferSize;

    uploadSettingsIfNeeded();

    glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
    glViewport(0, 0, framebufferSize.x, framebufferSize.y);

    if (targetFramebuffer == 0) {
        glDrawBuffer(GL_BACK);
        glReadBuffer(GL_BACK);
    }
    TRACE_FBO("drawPostProcess target");

    // save + disable depth
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthEnabled)
        glDisable(GL_DEPTH_TEST);

    // save + disable BLEND  ← this is the important bit
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    if (blendEnabled)
        glDisable(GL_BLEND);

    GLboolean srgbEnabled = glIsEnabled(GL_FRAMEBUFFER_SRGB);
    if (!srgbEnabled)
        glEnable(GL_FRAMEBUFFER_SRGB);

#ifndef NDEBUG
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
#endif

    GLuint bloomTexture = 0;
    if (m_cachedSettingsValid && m_cachedSettings.bloomEnabled) {
        bloomTexture = runBloom(m_sceneColor, m_framebufferSize);
    }
    if (bloomTexture == 0)
        bloomTexture = m_sceneColor;
    m_shader.bind();
#ifndef NDEBUG
    GLint curProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &curProgram);
    if (curProgram == 0) {
        std::fprintf(stderr, "[CameraEffectsStage][ERROR] postprocess shader program is 0\n");
    }
    if (m_quadVao == 0) {
        std::fprintf(stderr, "[CameraEffectsStage][ERROR] postprocess quad VAO missing before draw\n");
    }
#endif
    glBindBufferBase(GL_UNIFORM_BUFFER, kSettingsBinding, m_settingsUbo);

    TextureUnits::assertNotEnvUnit(0);
    TextureUnits::assertNotEnvUnit(1);
    TextureUnits::assertNotEnvUnit(2);
    TextureUnits::assertNotEnvUnit(3);
    TextureUnits::assertNotEnvUnit(4);
    glBindTextureUnit(0, m_sceneColor);
    glBindTextureUnit(1, m_sceneDepth);
    glBindTextureUnit(2, bloomTexture);
    glBindTextureUnit(3, m_lensDirtTexture);
    glBindTextureUnit(4, m_velocityTexture);

    drawFullscreenQuad();
    TRACE_FBO("drawPostProcess after quad");

    // Apply outline pass if enabled (reads from current framebuffer, writes back)
    // Note: This is a simplified approach that darkens in-place
    // A production version would use ping-pong buffers
    
    // restore sRGB
    if (!srgbEnabled)
        glDisable(GL_FRAMEBUFFER_SRGB);
    // restore BLEND
    if (blendEnabled)
        glEnable(GL_BLEND);
    // restore depth
    if (depthEnabled)
        glEnable(GL_DEPTH_TEST);

    glUseProgram(0);
}

void CameraEffectsStage::drawOutlinePass(const Settings& settings, glm::ivec2 framebufferSize, 
                                         GLuint sourceColor, GLuint sourceDepth, GLuint targetFramebuffer,
                                         float nearPlane, float farPlane)
{
    if (!settings.outline.enabled)
        return;

    // Bind target framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
    glViewport(0, 0, framebufferSize.x, framebufferSize.y);

    // Disable depth test and blending for full-screen pass
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthEnabled) glDisable(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    if (blendEnabled) glDisable(GL_BLEND);

    // Bind outline shader
    m_outlineShader.bind();

    // Set uniforms
    if (const GLint loc = m_outlineShader.getUniformLocation("u_outlineEnabled"); loc >= 0)
        glUniform1i(loc, settings.outline.enabled ? 1 : 0);
    if (const GLint loc = m_outlineShader.getUniformLocation("u_outlineStrength"); loc >= 0)
        glUniform1f(loc, settings.outline.strength);
    if (const GLint loc = m_outlineShader.getUniformLocation("u_depthThreshold"); loc >= 0)
        glUniform1f(loc, settings.outline.depthThreshold);
    if (const GLint loc = m_outlineShader.getUniformLocation("u_normalThreshold"); loc >= 0)
        glUniform1f(loc, settings.outline.normalThreshold);
    if (const GLint loc = m_outlineShader.getUniformLocation("u_useNormalEdges"); loc >= 0)
        glUniform1i(loc, settings.outline.useNormalEdges ? 1 : 0);
    if (const GLint loc = m_outlineShader.getUniformLocation("u_previewEdgeMask"); loc >= 0)
        glUniform1i(loc, settings.outline.previewEdgeMask ? 1 : 0);
    if (const GLint loc = m_outlineShader.getUniformLocation("u_nearPlane"); loc >= 0)
        glUniform1f(loc, nearPlane);
    if (const GLint loc = m_outlineShader.getUniformLocation("u_farPlane"); loc >= 0)
        glUniform1f(loc, farPlane);
    if (const GLint loc = m_outlineShader.getUniformLocation("u_texelSize"); loc >= 0)
        glUniform2f(loc, 1.0f / framebufferSize.x, 1.0f / framebufferSize.y);

    // Bind textures
    glBindTextureUnit(0, sourceColor);
    glBindTextureUnit(1, sourceDepth);

    // Draw fullscreen quad
    drawFullscreenQuad();

    // Restore states
    if (blendEnabled) glEnable(GL_BLEND);
    if (depthEnabled) glEnable(GL_DEPTH_TEST);
    glUseProgram(0);
}


void CameraEffectsStage::drawImGuiPanel(Settings& settings)
{
    // ===== MSAA CONTROLS =====
    ImGui::TextUnformatted("Anti-Aliasing (MSAA)");
    ImGui::Checkbox("Enable MSAA", &settings.msaaEnabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Multisample Anti-Aliasing smooths edges by rendering at higher resolution");
    }
    
    ImGui::BeginDisabled(!settings.msaaEnabled);
    const char* sampleOptions[] = { "2x", "4x", "8x", "16x" };
    const int sampleValues[] = { 2, 4, 8, 16 };
    int currentSampleIndex = 1; // default to 4x
    for (int i = 0; i < 4; i++) {
        if (settings.msaaSamples == sampleValues[i]) {
            currentSampleIndex = i;
            break;
        }
    }
    if (ImGui::Combo("MSAA Samples", &currentSampleIndex, sampleOptions, 4)) {
        settings.msaaSamples = sampleValues[currentSampleIndex];
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Higher samples = smoother edges but more GPU memory\n4x is a good balance");
    }
    ImGui::EndDisabled();
    
    ImGui::Separator();
    ImGui::TextUnformatted("Tone Mapping");
    ImGui::SliderFloat("Exposure", &settings.exposure, -5.0f, 5.0f);
    ImGui::SliderFloat("Gamma", &settings.gamma, 0.8f, 3.2f);
    ImGui::SliderFloat("Contrast", &settings.contrast, 0.0f, 2.5f);
    ImGui::SliderFloat("Saturation", &settings.saturation, 0.0f, 2.5f);

    ImGui::Separator();
    ImGui::TextUnformatted("Effect Toggles");
    ImGui::Checkbox("Bloom", &settings.bloomEnabled);

    ImGui::Checkbox("Vignette", &settings.vignetteEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("Depth of Field", &settings.depthOfFieldEnabled);

    ImGui::Checkbox("Color Grading", &settings.colorGradingEnabled);
    ImGui::SameLine();
    ImGui::Checkbox("Film Grain", &settings.filmGrainEnabled);

    ImGui::Separator();
    ImGui::TextUnformatted("Bloom Settings");
    ImGui::BeginDisabled(!settings.bloomEnabled);
    ImGui::SliderFloat("Strength", &settings.bloom.strength, 0.0f, 0.2f, "%.3f");
    ImGui::SliderFloat("Radius", &settings.bloom.radius, 0.001f, 0.02f, "%.4f");
    ImGui::SliderInt("Mip Count", &settings.bloom.mipCount, 3, 6);
    ImGui::Checkbox("Soft Threshold", &settings.bloom.useSoftThreshold);
    ImGui::BeginDisabled(!settings.bloom.useSoftThreshold);
    ImGui::SliderFloat("Threshold", &settings.bloom.threshold, 0.0f, 10.0f);
    ImGui::SliderFloat("Soft Knee", &settings.bloom.softKnee, 0.0f, 1.0f);
    ImGui::EndDisabled();
    ImGui::SliderFloat("Lens Dirt Intensity", &settings.bloom.dirtIntensity, 0.0f, 5.0f);
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Vignette Settings");
    ImGui::BeginDisabled(!settings.vignetteEnabled);
    ImGui::SliderFloat("Inner Radius", &settings.vignette.innerRadius, 0.0f, 1.0f);
    ImGui::SliderFloat("Outer Radius", &settings.vignette.outerRadius, 0.0f, 2.0f);
    ImGui::SliderFloat("Power", &settings.vignette.power, 0.5f, 5.0f);
    ImGui::SliderFloat("Intensity##Vignette", &settings.vignette.intensity, 0.0f, 1.0f);
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Depth of Field Settings");
    ImGui::BeginDisabled(!settings.depthOfFieldEnabled);
    ImGui::SliderFloat("Focus Distance", &settings.dof.focusDistance, 0.1f, 200.0f);
    ImGui::SliderFloat("Focus Range", &settings.dof.focusRange, 0.01f, 50.0f);
    ImGui::SliderFloat("Max Blur Radius", &settings.dof.maxBlurRadius, 0.0f, 30.0f);
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Color Grading");
    ImGui::BeginDisabled(!settings.colorGradingEnabled);
    ImGui::ColorEdit3("Lift", glm::value_ptr(settings.colorGrade.lift));
    ImGui::ColorEdit3("Gamma", glm::value_ptr(settings.colorGrade.gamma));
    ImGui::ColorEdit3("Gain", glm::value_ptr(settings.colorGrade.gain));
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Film Grain");
    ImGui::BeginDisabled(!settings.filmGrainEnabled);
    ImGui::SliderFloat("Amount", &settings.filmGrain.amount, 0.0f, 0.5f);
    ImGui::SliderFloat("Response", &settings.filmGrain.response, 0.0f, 2.0f);
    ImGui::SliderFloat("Seed", &settings.filmGrain.seed, 0.0f, 256.0f);
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextUnformatted("Edge Detection Outline");
    ImGui::Checkbox("Enable Outline", &settings.outline.enabled);
    ImGui::BeginDisabled(!settings.outline.enabled);
    ImGui::SliderFloat("Outline Strength", &settings.outline.strength, 0.0f, 2.0f);
    ImGui::SliderFloat("Depth Threshold", &settings.outline.depthThreshold, 0.0f, 1.0f);
    ImGui::Checkbox("Use Normal Edges", &settings.outline.useNormalEdges);
    ImGui::BeginDisabled(!settings.outline.useNormalEdges);
    ImGui::SliderFloat("Normal Threshold", &settings.outline.normalThreshold, 0.0f, 1.0f);
    ImGui::EndDisabled();
    ImGui::Checkbox("Preview Edge Mask", &settings.outline.previewEdgeMask);
    ImGui::EndDisabled();
}

void CameraEffectsStage::ensureResources()
{
    ensureShader();
    ensureQuad();
    ensureUniformBuffer();
    ensureFallbackTextures();
    ensureBloomResources();
}

void CameraEffectsStage::ensureFramebuffer(glm::ivec2 size)
{
    if (!isValidSize(size))
        return;

    if (m_framebuffer == 0)
        glGenFramebuffers(1, &m_framebuffer);

    if (m_sceneColor == 0)
        glGenTextures(1, &m_sceneColor);
    if (m_sceneDepth == 0)
        glGenTextures(1, &m_sceneDepth);
    if (m_velocityTexture == 0)
        glGenTextures(1, &m_velocityTexture);
    if (m_framebufferSize != size) {
        destroyBloomMipChain();
        m_framebufferSize = size;
        m_bloomBaseSize = glm::ivec2(0);
        m_bloomResult = 0;

        glBindTexture(GL_TEXTURE_2D, m_sceneColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x, size.y, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_2D, m_sceneDepth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, size.x, size.y, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindTexture(GL_TEXTURE_2D, m_velocityTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.x, size.y, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glClearTexImage(m_velocityTexture, 0, GL_RGBA, GL_FLOAT, kVelocityClear);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, kColorAttachment, GL_TEXTURE_2D, m_sceneColor, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, kDepthAttachment, GL_TEXTURE_2D, m_sceneDepth, 0);

    GLenum buffers[] = { kColorAttachment };
    glDrawBuffers(1, buffers);

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        throw std::runtime_error("CameraEffectsStage framebuffer incomplete.");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

        auto fixColorRT = [](GLuint tex) {
        if (!tex) return;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // force single-level texture: no mips
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    };

    auto fixDepthRT = [](GLuint tex) {
        if (!tex) return;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    };

    // our main targets
    fixColorRT(m_sceneColor);
    fixDepthRT(m_sceneDepth);
    fixColorRT(m_velocityTexture);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void CameraEffectsStage::ensureMSAAFramebuffer(glm::ivec2 size, int samples)
{
    if (!isValidSize(size) || samples <= 0)
        return;

    // Check if we need to recreate MSAA resources
    bool needsRecreate = (m_msaaFramebuffer == 0 || 
                          m_currentMsaaSamples != samples || 
                          m_framebufferSize != size);

    if (!needsRecreate)
        return;

    // Clean up old resources if they exist
    if (m_msaaFramebuffer) {
        glDeleteFramebuffers(1, &m_msaaFramebuffer);
        m_msaaFramebuffer = 0;
    }
    if (m_msaaColorRBO) {
        glDeleteRenderbuffers(1, &m_msaaColorRBO);
        m_msaaColorRBO = 0;
    }
    if (m_msaaDepthRBO) {
        glDeleteRenderbuffers(1, &m_msaaDepthRBO);
        m_msaaDepthRBO = 0;
    }

    // Query max samples supported
    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    samples = std::min(samples, maxSamples);
    samples = std::max(samples, 1);

    // Create MSAA framebuffer
    glGenFramebuffers(1, &m_msaaFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFramebuffer);

    // Create multisampled color renderbuffer
    glGenRenderbuffers(1, &m_msaaColorRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRBO);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, size.x, size.y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColorRBO);

    // Create multisampled depth renderbuffer
    glGenRenderbuffers(1, &m_msaaDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepthRBO);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT32F, size.x, size.y);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepthRBO);

    // Check framebuffer completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        throw std::runtime_error("MSAA framebuffer incomplete!");
    }

    m_currentMsaaSamples = samples;
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void CameraEffectsStage::ensureQuad()
{
    if (m_quadVao != 0)
        return;

    glGenVertexArrays(1, &m_quadVao);
    glGenBuffers(1, &m_quadVbo);

    glBindVertexArray(m_quadVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kFullscreenQuad), kFullscreenQuad.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CameraEffectsStage::drawFullscreenQuad()
{
    if (m_quadVao == 0)
#ifndef NDEBUG
    {
        std::fprintf(stderr, "[CameraEffectsStage][ERROR] fullscreen quad VAO is 0\n");
        return;
    }
#else
        return;
#endif

    glBindVertexArray(m_quadVao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void CameraEffectsStage::ensureBloomResources()
{
    ensureBloomShaders();
    if (m_bloomFramebuffer == 0)
        glGenFramebuffers(1, &m_bloomFramebuffer);
}

void CameraEffectsStage::destroyBloomMipChain()
{
    for (BloomMip& mip : m_bloomMips) {
        if (mip.texture != 0)
            glDeleteTextures(1, &mip.texture);
        mip.texture = 0;
    }
    m_bloomMips.clear();
    m_bloomBaseSize = glm::ivec2(0);
    m_cachedMipCount = 0;
    m_bloomResult = 0;
}

void CameraEffectsStage::ensureBloomMipChain(glm::ivec2 baseSize, int mipCount)
{
    baseSize = glm::max(baseSize, glm::ivec2(1));
    mipCount = std::clamp(mipCount, 1, 8);

    if (m_bloomBaseSize == baseSize && m_cachedMipCount == mipCount && !m_bloomMips.empty())
        return;

    destroyBloomMipChain();

    glm::ivec2 mipSize = baseSize;
    for (int level = 0; level < mipCount; ++level) {
        mipSize = glm::max(mipSize / 2, glm::ivec2(1));

        BloomMip mip;
        mip.size = mipSize;
        glGenTextures(1, &mip.texture);
        glBindTexture(GL_TEXTURE_2D, mip.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, mip.size.x, mip.size.y, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        m_bloomMips.push_back(mip);

        if (mip.size.x == 1 && mip.size.y == 1)
            break;
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    m_bloomBaseSize = baseSize;
    m_cachedMipCount = static_cast<int>(m_bloomMips.size());
    m_bloomResult = 0;
}

GLuint CameraEffectsStage::runBloom(GLuint sourceTexture, glm::ivec2 sourceSize)
{
    if (sourceTexture == 0 || !isValidSize(sourceSize))
        return 0;
    if (!m_cachedSettingsValid || !m_cachedSettings.bloomEnabled)
        return 0;

    ensureBloomResources();

    const int targetMipCount = std::clamp(m_cachedSettings.bloom.mipCount, 1, 8);
    ensureBloomMipChain(sourceSize, targetMipCount);
    if (m_bloomMips.empty())
        return 0;

    const Settings::Bloom& bloomSettings = m_cachedSettings.bloom;
    if (bloomSettings.strength <= 0.0f)
        return 0;

    GLint prevFramebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFramebuffer);
    GLint prevViewport[4] { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    if (depthEnabled)
        glDisable(GL_DEPTH_TEST);

    const GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLint prevBlendSrcRGB = GL_ONE, prevBlendDstRGB = GL_ZERO;
    GLint prevBlendSrcAlpha = GL_ONE, prevBlendDstAlpha = GL_ZERO;
    GLint prevBlendEqRGB = GL_FUNC_ADD, prevBlendEqAlpha = GL_FUNC_ADD;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEqRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevBlendEqAlpha);

    if (!blendWasEnabled)
        glEnable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFramebuffer);
    const GLenum drawAttachment = GL_COLOR_ATTACHMENT0;
    const GLenum drawBuffers[] = { drawAttachment };
    glDrawBuffers(1, drawBuffers);

    TextureUnits::assertNotEnvUnit(0);

    // Downsample chain
    GLuint inputTexture = sourceTexture;
    glm::ivec2 inputSize = sourceSize;
    const GLint downTexelLoc = m_bloomDownsampleShader.getUniformLocation("uTexelSize");
    const GLint downKarisLoc = m_bloomDownsampleShader.getUniformLocation("uApplyKarisAverage");
    const GLint downClampLoc = m_bloomDownsampleShader.getUniformLocation("uClampToMinimum");
    const GLint downMinLoc = m_bloomDownsampleShader.getUniformLocation("uMinimumValue");

    for (std::size_t i = 0; i < m_bloomMips.size(); ++i) {
        const bool firstMip = (i == 0);
        BloomMip& mip = m_bloomMips[i];

        glFramebufferTexture2D(GL_FRAMEBUFFER, drawAttachment, GL_TEXTURE_2D, mip.texture, 0);
        glViewport(0, 0, mip.size.x, mip.size.y);
        glClear(GL_COLOR_BUFFER_BIT);

        m_bloomDownsampleShader.bind();
        glBindTextureUnit(0, inputTexture);

        if (downTexelLoc >= 0) {
            const float invX = 1.0f / std::max(inputSize.x, 1);
            const float invY = 1.0f / std::max(inputSize.y, 1);
            glUniform2f(downTexelLoc, invX, invY);
        }
        if (downKarisLoc >= 0)
            glUniform1i(downKarisLoc, (firstMip && bloomSettings.useKarisAverage) ? 1 : 0);
        if (downClampLoc >= 0)
            glUniform1i(downClampLoc, firstMip ? 1 : 0);
        if (downMinLoc >= 0)
            glUniform1f(downMinLoc, 1.0e-4f);

        drawFullscreenQuad();

        inputTexture = mip.texture;
        inputSize = mip.size;
    }

    // Upsample chain (skip smallest mip because nothing to add)
    const GLint upTexelLoc = m_bloomUpsampleShader.getUniformLocation("uTexelSize");
    const GLint upRadiusLoc = m_bloomUpsampleShader.getUniformLocation("uFilterRadius");
    glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);

    for (int i = static_cast<int>(m_bloomMips.size()) - 2; i >= 0; --i) {
        BloomMip& srcMip = m_bloomMips[static_cast<std::size_t>(i + 1)];
        BloomMip& dstMip = m_bloomMips[static_cast<std::size_t>(i)];

        glFramebufferTexture2D(GL_FRAMEBUFFER, drawAttachment, GL_TEXTURE_2D, dstMip.texture, 0);
        glViewport(0, 0, dstMip.size.x, dstMip.size.y);

        m_bloomUpsampleShader.bind();
        glBindTextureUnit(0, srcMip.texture);

        if (upTexelLoc >= 0) {
            const float invX = 1.0f / std::max(srcMip.size.x, 1);
            const float invY = 1.0f / std::max(srcMip.size.y, 1);
            glUniform2f(upTexelLoc, invX, invY);
        }

        if (upRadiusLoc >= 0) {
            const float srcDim = static_cast<float>(std::max(srcMip.size.x, srcMip.size.y));
            const float radiusUV = bloomSettings.radius;
            const float radiusTexels = glm::clamp(radiusUV * srcDim, 0.0f, 64.0f);
            glUniform1f(upRadiusLoc, radiusTexels);
        }

        drawFullscreenQuad();
    }

    glUseProgram(0);

    // Restore blend state
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrcAlpha, prevBlendDstAlpha);
    glBlendEquationSeparate(prevBlendEqRGB, prevBlendEqAlpha);
    if (!blendWasEnabled)
        glDisable(GL_BLEND);

    if (depthEnabled)
        glEnable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    m_bloomResult = m_bloomMips.empty() ? 0 : m_bloomMips.front().texture;
    return m_bloomResult;
}

void CameraEffectsStage::ensureShader()
{
    if (m_shader.id() == std::numeric_limits<GLuint>::max()) {
        ShaderBuilder builder;
        builder.addStage(GL_VERTEX_SHADER, (m_shaderDirectory / "camera_effects.vert").string());
        builder.addStage(GL_FRAGMENT_SHADER, (m_shaderDirectory / "camera_effects.frag").string());
        m_shader = builder.build();

        // set sampler units ...
        m_shader.bind();
        if (const GLint loc = m_shader.getUniformLocation("uSceneColor"); loc >= 0) glUniform1i(loc, 0);
        if (const GLint loc = m_shader.getUniformLocation("uSceneDepth"); loc >= 0) glUniform1i(loc, 1);
        if (const GLint loc = m_shader.getUniformLocation("uBloomTexture"); loc >= 0) glUniform1i(loc, 2);
        if (const GLint loc = m_shader.getUniformLocation("uLensDirtTexture"); loc >= 0) glUniform1i(loc, 3);
        if (const GLint loc = m_shader.getUniformLocation("uVelocityTexture"); loc >= 0) glUniform1i(loc, 4);
        glUseProgram(0);
    }

    // Load outline shader
    if (m_outlineShader.id() == std::numeric_limits<GLuint>::max()) {
        ShaderBuilder builder;
        builder.addStage(GL_VERTEX_SHADER, (m_shaderDirectory / "outline.vert").string());
        builder.addStage(GL_FRAGMENT_SHADER, (m_shaderDirectory / "outline.frag").string());
        m_outlineShader = builder.build();

        m_outlineShader.bind();
        if (const GLint loc = m_outlineShader.getUniformLocation("u_sceneColor"); loc >= 0) glUniform1i(loc, 0);
        if (const GLint loc = m_outlineShader.getUniformLocation("u_sceneDepth"); loc >= 0) glUniform1i(loc, 1);
        glUseProgram(0);
    }

#ifndef NDEBUG
    if (m_shader.id() != std::numeric_limits<GLuint>::max()) {
        GLuint prog = m_shader.id();
        GLuint blockIndex = glGetUniformBlockIndex(prog, "CameraEffectsSettings");
        if (blockIndex != GL_INVALID_INDEX) {
            GLint glBlockSize = 0;
            glGetActiveUniformBlockiv(prog, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &glBlockSize);

            const GLint cppSize = static_cast<GLint>(sizeof(GpuSettings));
            if (glBlockSize != cppSize) {
                std::fprintf(stderr,
                    "[CameraEffectsStage][ERROR] UBO size mismatch: GLSL=%d bytes, C++=%d bytes\n",
                    glBlockSize, cppSize);
            }
        } else {
            std::fprintf(stderr,
                "[CameraEffectsStage][ERROR] camera_effects.frag has no block named CameraEffectsSettings\n");
        }
    }
#endif
}

void CameraEffectsStage::ensureBloomShaders()
{
    if (m_bloomDownsampleShader.id() == std::numeric_limits<GLuint>::max()) {
        ShaderBuilder builder;
        builder.addStage(GL_VERTEX_SHADER, (m_shaderDirectory / "bloom_downsample.vert").string());
        builder.addStage(GL_FRAGMENT_SHADER, (m_shaderDirectory / "bloom_downsample.frag").string());
        m_bloomDownsampleShader = builder.build();

        m_bloomDownsampleShader.bind();
        if (const GLint loc = m_bloomDownsampleShader.getUniformLocation("uSource"); loc >= 0)
            glUniform1i(loc, 0);
        glUseProgram(0);
    }

    if (m_bloomUpsampleShader.id() == std::numeric_limits<GLuint>::max()) {
        ShaderBuilder builder;
        builder.addStage(GL_VERTEX_SHADER, (m_shaderDirectory / "bloom_upsample.vert").string());
        builder.addStage(GL_FRAGMENT_SHADER, (m_shaderDirectory / "bloom_upsample.frag").string());
        m_bloomUpsampleShader = builder.build();

        m_bloomUpsampleShader.bind();
        if (const GLint loc = m_bloomUpsampleShader.getUniformLocation("uSource"); loc >= 0)
            glUniform1i(loc, 0);
        glUseProgram(0);
    }
}


void CameraEffectsStage::ensureUniformBuffer()
{
    if (m_settingsUbo != 0)
        return;

    glGenBuffers(1, &m_settingsUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, m_settingsUbo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(GpuSettings), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, kSettingsBinding, m_settingsUbo);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    m_settingsDirty = true;
}

void CameraEffectsStage::ensureFallbackTextures()
{
    if (m_lensDirtTexture != 0)
        return;

    glGenTextures(1, &m_lensDirtTexture);
    glBindTexture(GL_TEXTURE_2D, m_lensDirtTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_FLOAT, kLensDirtWhite);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void CameraEffectsStage::uploadSettingsIfNeeded()
{
    if (!m_settingsDirty || m_settingsUbo == 0)
        return;

    glBindBuffer(GL_UNIFORM_BUFFER, m_settingsUbo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(GpuSettings), &m_gpuSettings);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    m_settingsDirty = false;
}
