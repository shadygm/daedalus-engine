// SPDX-License-Identifier: MIT

#include "rendering/EnvironmentManager.h"
#include "rendering/TextureUnits.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb/stb_image.h>
DISABLE_WARNINGS_POP()

#include <framework/opengl_includes.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <iostream>
#include <utility>
#include <vector>

namespace {

constexpr float kCubeVertices[] = {
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,

    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,

    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,

     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,

    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f,  1.0f,
    -1.0f, -1.0f, -1.0f,

    -1.0f,  1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
     1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f
};

constexpr float kQuadVertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f
};

// NOTE:
// Material system uses texture units 0..4 exclusively for per-material maps.
// Environment/IBL textures occupy units 24..27 (see TextureUnits.h).
// Environment baking stages temporarily claim the final texture unit (maxUnits - 1).
// Do not bind environment or bake resources to units below 16, or they may stomp
// on material textures and cause rendering artifacts.

void clearTextureBindings(std::initializer_list<GLint> units)
{
    GLint maxUnits = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxUnits);
    GLint previousActive = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActive);
    for (GLint unit : units) {
        if (unit >= 0 && unit < maxUnits) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glBindSampler(static_cast<GLuint>(unit), 0);
        }
    }
    glActiveTexture(previousActive);
}

void logFramebufferStatus(const char* label)
{
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[EnvManager] FBO incomplete in " << label << " (status=" << status << ")\n";
    }
}

#ifndef NDEBUG
void debugAssertTextureUnitCleared(GLuint unit, GLenum binding)
{
    GLint maxUnits = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxUnits);
    if (unit >= static_cast<GLuint>(maxUnits))
        return;

    GLint previousActive = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActive);
    glActiveTexture(GL_TEXTURE0 + unit);

    GLint boundId = 0;
    glGetIntegerv(binding, &boundId);
    assert(boundId == 0 && "Texture unit leakage detected");

    glActiveTexture(previousActive);
}
#endif

const std::array<glm::mat4, 6> kCaptureViews {
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    glm::lookAt(glm::vec3(0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
};

const glm::mat4 kCaptureProjection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

std::string buildCacheKey(const std::filesystem::path& path, const EnvironmentManager::AdvancedSettings& settings)
{
    std::string key = path.string();
    key += "|env=" + std::to_string(settings.environmentResolution);
    key += "|irr=" + std::to_string(settings.irradianceResolution);
    key += "|pref=" + std::to_string(settings.prefilterBaseResolution);
    key += "|mip=" + std::to_string(settings.prefilterMipLevels);
    return key;
}

Shader compileShader(const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath)
{
    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, vertexPath);
    builder.addStage(GL_FRAGMENT_SHADER, fragmentPath);
    return builder.build();
}

} // namespace

EnvironmentManager::EnvironmentTextures::~EnvironmentTextures()
{
    reset();
}

EnvironmentManager::EnvironmentTextures::EnvironmentTextures(EnvironmentTextures&& other) noexcept
{
    *this = std::move(other);
}

EnvironmentManager::EnvironmentTextures& EnvironmentManager::EnvironmentTextures::operator=(EnvironmentTextures&& other) noexcept
{
    if (this == &other)
        return *this;

    reset();

    hdrTexture = other.hdrTexture;
    envCubemap = other.envCubemap;
    irradianceCubemap = other.irradianceCubemap;
    prefilteredCubemap = other.prefilteredCubemap;
    prefilterMipLevels = other.prefilterMipLevels;

    other.hdrTexture = 0;
    other.envCubemap = 0;
    other.irradianceCubemap = 0;
    other.prefilteredCubemap = 0;
    other.prefilterMipLevels = 0;
    return *this;
}

void EnvironmentManager::EnvironmentTextures::reset()
{
    if (hdrTexture != 0)
        glDeleteTextures(1, &hdrTexture);
    if (envCubemap != 0)
        glDeleteTextures(1, &envCubemap);
    if (irradianceCubemap != 0)
        glDeleteTextures(1, &irradianceCubemap);
    if (prefilteredCubemap != 0)
        glDeleteTextures(1, &prefilteredCubemap);

    hdrTexture = 0;
    envCubemap = 0;
    irradianceCubemap = 0;
    prefilteredCubemap = 0;
    prefilterMipLevels = 0;
}

EnvironmentManager::EnvironmentManager(const std::filesystem::path& shaderDirectory)
    : m_shaderDirectory(shaderDirectory)
{
}

EnvironmentManager::~EnvironmentManager()
{
    unload();
    destroyShaders();

    if (m_cubeVBO)
        glDeleteBuffers(1, &m_cubeVBO);
    if (m_cubeVAO)
        glDeleteVertexArrays(1, &m_cubeVAO);
    if (m_quadVBO)
        glDeleteBuffers(1, &m_quadVBO);
    if (m_quadVAO)
        glDeleteVertexArrays(1, &m_quadVAO);
    if (m_captureRBO)
        glDeleteRenderbuffers(1, &m_captureRBO);
    if (m_captureFBO)
        glDeleteFramebuffers(1, &m_captureFBO);
    if (m_brdfLut)
        glDeleteTextures(1, &m_brdfLut);
}

void EnvironmentManager::initializeGL()
{
    if (m_isInitialized)
        return;

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    GLint maxUnits = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxUnits);

    if (maxUnits < 20)
        throw std::runtime_error("Not enough texture units for environment pipeline.");

    m_bakeTextureUnit = maxUnits - 1;

#ifndef NDEBUG
    if (TextureUnits::Env_Skybox >= static_cast<GLuint>(maxUnits)) {
        throw std::runtime_error("Insufficient texture units available for skybox binding.");
    }
#endif

    if (m_cubeSampler == 0) {
        glGenSamplers(1, &m_cubeSampler);
        glSamplerParameteri(m_cubeSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_cubeSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_cubeSampler, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_cubeSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glSamplerParameteri(m_cubeSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    if (m_lutSampler == 0) {
        glGenSamplers(1, &m_lutSampler);
        glSamplerParameteri(m_lutSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_lutSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_lutSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_lutSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    m_equirectangularShader = compileShader(m_shaderDirectory / "equirect_to_cubemap.vert", m_shaderDirectory / "equirect_to_cubemap.frag");
    m_irradianceShader      = compileShader(m_shaderDirectory / "equirect_to_cubemap.vert", m_shaderDirectory / "irradiance_convolution.frag");
    m_prefilterShader       = compileShader(m_shaderDirectory / "equirect_to_cubemap.vert", m_shaderDirectory / "prefilter.frag");
    m_brdfShader            = compileShader(m_shaderDirectory / "brdf_lut.vert", m_shaderDirectory / "brdf_lut.frag");
    m_skyboxShader          = compileShader(m_shaderDirectory / "skybox.vert", m_shaderDirectory / "skybox.frag");

    ensureCaptureResources();
    ensureCubeGeometry();
    ensureQuadGeometry();
    ensureBrdfLut();

    m_isInitialized = true;
}


void EnvironmentManager::destroyShaders()
{
    m_equirectangularShader = Shader {};
    m_irradianceShader = Shader {};
    m_prefilterShader = Shader {};
    m_brdfShader = Shader {};
    m_skyboxShader = Shader {};
}

bool EnvironmentManager::loadEnvironment(const std::filesystem::path& path)
{
    if (!m_isInitialized)
        initializeGL();

    auto baked = bakeEnvironment(path);
    if (!baked)
        return false;

    m_currentEnvironment = std::move(baked);
    m_currentPath = path;
    return true;
}


void EnvironmentManager::unload()
{
    m_currentEnvironment.reset();
    m_currentPath.clear();
}

void EnvironmentManager::drawSkybox(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix) const
{
    if (!m_skyboxVisible || !m_currentEnvironment)
        return;

    sanitizeGeneratedTextures();

    glEnable(GL_FRAMEBUFFER_SRGB);

    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    m_skyboxShader.bind();

    const glm::mat4 viewNoTranslation = glm::mat4(glm::mat3(viewMatrix));
    const GLint locProjection  = m_skyboxShader.getUniformLocation("projection");
    const GLint locView        = m_skyboxShader.getUniformLocation("view");
    const GLint locIntensity   = m_skyboxShader.getUniformLocation("uEnvIntensity");
    const GLint locEnv         = m_skyboxShader.getUniformLocation("uEnvironmentMap");
    const GLint locMipOverride = m_skyboxShader.getUniformLocation("uMipOverride");
    const GLint locMaxMip      = m_skyboxShader.getUniformLocation("uMaxMip");

    glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(projectionMatrix));
    glUniformMatrix4fv(locView,       1, GL_FALSE, glm::value_ptr(viewNoTranslation));
    glUniform1f(locIntensity, m_environmentIntensity);

    const bool usePrefilter = m_debugSkyboxUsePrefilter && m_currentEnvironment->prefilteredCubemap != 0;
    const GLuint skyboxTexture = usePrefilter
        ? m_currentEnvironment->prefilteredCubemap
        : m_currentEnvironment->envCubemap;

    if (skyboxTexture == 0) {
        static bool warnedMissingSkybox = false;
        if (!warnedMissingSkybox) {
            std::cerr << "[EnvManager] drawSkybox: skyboxTexture == 0 (environment cubemap missing)\n";
            warnedMissingSkybox = true;
        }
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glDisable(GL_FRAMEBUFFER_SRGB);
        return;
    }

    float maxMip = 0.0f;
    if (usePrefilter)
        maxMip = static_cast<float>(std::max(m_currentEnvironment->prefilterMipLevels - 1, 0));
    const float mipOverride = (m_debugSkyboxMipOverride >= 0.0f)
        ? std::min(std::max(0.0f, m_debugSkyboxMipOverride), maxMip)
        : -1.0f;

    if (locMipOverride >= 0)
        glUniform1f(locMipOverride, mipOverride);
    if (locMaxMip >= 0)
        glUniform1f(locMaxMip, maxMip);
    if (locEnv >= 0)
        glUniform1i(locEnv, static_cast<GLint>(TextureUnits::Env_Skybox));

    glBindTextureUnit(TextureUnits::Env_Skybox, skyboxTexture);
    if (m_cubeSampler != 0)
        glBindSampler(TextureUnits::Env_Skybox, m_cubeSampler);
    else
        glBindSampler(TextureUnits::Env_Skybox, 0);

    renderCube();

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);
}


void EnvironmentManager::setSkyboxUsePrefilter(bool enabled)
{
    m_debugSkyboxUsePrefilter = enabled;
    if (!enabled)
        m_debugSkyboxMipOverride = -1.0f;
}

void EnvironmentManager::setSkyboxMipOverride(float mipLevel)
{
    if (!std::isfinite(mipLevel))
        return;
    m_debugSkyboxMipOverride = std::max(mipLevel, 0.0f);
}

void EnvironmentManager::clearSkyboxMipOverride()
{
    m_debugSkyboxMipOverride = -1.0f;
}

void EnvironmentManager::setAdvancedSettings(const AdvancedSettings& settings)
{
    if (settings.environmentResolution == m_settings.environmentResolution
        && settings.irradianceResolution == m_settings.irradianceResolution
        && settings.prefilterBaseResolution == m_settings.prefilterBaseResolution
        && settings.prefilterMipLevels == m_settings.prefilterMipLevels)
        return;

    m_settings = settings;
    if (!m_currentPath.empty()) {
        loadEnvironment(m_currentPath);
    }
}

GLuint EnvironmentManager::environmentCubemap() const
{
    sanitizeGeneratedTextures();
    return m_currentEnvironment ? m_currentEnvironment->envCubemap : 0;
}

GLuint EnvironmentManager::irradianceCubemap() const
{
    sanitizeGeneratedTextures();
    return m_currentEnvironment ? m_currentEnvironment->irradianceCubemap : 0;
}

GLuint EnvironmentManager::prefilterCubemap() const
{
    sanitizeGeneratedTextures();
    return m_currentEnvironment ? m_currentEnvironment->prefilteredCubemap : 0;
}

GLuint EnvironmentManager::brdfLutTexture()
{
    ensureBrdfLut();
    return m_brdfLut;
}

int EnvironmentManager::prefilterMipLevelCount() const
{
    return m_currentEnvironment ? m_currentEnvironment->prefilterMipLevels : 0;
}

void EnvironmentManager::bindForPbr(const Shader& shader, int /*firstTextureUnit*/) const
{
    if (!m_useIBL || !m_currentEnvironment)
        return;

    const_cast<EnvironmentManager*>(this)->ensureBrdfLut();
    sanitizeGeneratedTextures();

    const auto& tex = *m_currentEnvironment;
    const GLuint program = shader.id();

    const GLint locIrr    = glGetUniformLocation(program, "uIrradianceMap");
    const GLint locPref   = glGetUniformLocation(program, "uPreFilterMap");
    const GLint locPrefCt = glGetUniformLocation(program, "uPrefilterMipCount");
    const GLint locBrdf   = glGetUniformLocation(program, "uBRDFLut");

    if (locIrr < 0 && locPref < 0 && locBrdf < 0 && locPrefCt < 0)
        return;

    if (locPrefCt >= 0)
        glUniform1f(locPrefCt, static_cast<float>(std::max(tex.prefilterMipLevels - 1, 0)));

    if (tex.irradianceCubemap != 0 && locIrr >= 0) {
        glUniform1i(locIrr, static_cast<GLint>(TextureUnits::Env_Irradiance));
        glBindTextureUnit(TextureUnits::Env_Irradiance, tex.irradianceCubemap);
        glBindSampler(TextureUnits::Env_Irradiance, 0);
    }

    if (tex.prefilteredCubemap != 0 && locPref >= 0) {
        glUniform1i(locPref, static_cast<GLint>(TextureUnits::Env_Prefilter));
        glBindTextureUnit(TextureUnits::Env_Prefilter, tex.prefilteredCubemap);
        glBindSampler(TextureUnits::Env_Prefilter, 0);
    }

    if (m_brdfLut != 0 && locBrdf >= 0) {
        glUniform1i(locBrdf, static_cast<GLint>(TextureUnits::Env_BRDF));
        glBindTextureUnit(TextureUnits::Env_BRDF, m_brdfLut);
        glBindSampler(TextureUnits::Env_BRDF, 0);
    }
}

std::string EnvironmentManager::createCacheKey(const std::filesystem::path& path) const
{
    return buildCacheKey(path, m_settings);
}

void EnvironmentManager::ensureBrdfLut()
{
    if (m_brdfLut != 0)
        return;
    generateBrdfLutTexture();
}

void EnvironmentManager::sanitizeGeneratedTextures() const
{
    if (m_brdfLut != 0) {
        glBindTexture(GL_TEXTURE_2D, m_brdfLut);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (!m_currentEnvironment)
        return;

    const EnvironmentTextures& tex = *m_currentEnvironment;

    if (tex.envCubemap != 0) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex.envCubemap);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    if (tex.irradianceCubemap != 0) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex.irradianceCubemap);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    if (tex.prefilteredCubemap != 0) {
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex.prefilteredCubemap);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        const int bakedMipLevels = std::max(tex.prefilterMipLevels, 1);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, bakedMipLevels - 1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
}

void EnvironmentManager::ensureCaptureResources()
{
    if (m_captureFBO != 0)
        return;

    glGenFramebuffers(1, &m_captureFBO);
    glGenRenderbuffers(1, &m_captureRBO);
}

void EnvironmentManager::ensureCubeGeometry()
{
    if (m_cubeVAO != 0)
        return;

    glGenVertexArrays(1, &m_cubeVAO);
    glGenBuffers(1, &m_cubeVBO);
    glBindVertexArray(m_cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVertices), kCubeVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), reinterpret_cast<void*>(0));
    glBindVertexArray(0);
}

void EnvironmentManager::ensureQuadGeometry()
{
    if (m_quadVAO != 0)
        return;

    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glBindVertexArray(0);
}

void EnvironmentManager::renderCube() const
{
    glBindVertexArray(m_cubeVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void EnvironmentManager::renderFullscreenQuad() const
{
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

std::shared_ptr<EnvironmentManager::EnvironmentTextures> EnvironmentManager::bakeEnvironment(const std::filesystem::path& path)
{
    GLuint hdrTexture = loadHdrTexture(path);
    if (hdrTexture == 0)
        return nullptr;

    auto textures = std::make_shared<EnvironmentTextures>();
    textures->hdrTexture = hdrTexture;

    convertEquirectangularToCubemap(*textures, m_settings.environmentResolution);
    convolveIrradiance(*textures, m_settings.irradianceResolution);
    prefilterSpecular(*textures, m_settings.prefilterBaseResolution, m_settings.prefilterMipLevels);

    sanitizeGeneratedTextures();

    if (textures->hdrTexture != 0) {
        glDeleteTextures(1, &textures->hdrTexture);
        textures->hdrTexture = 0;
    }

    return textures;
}

GLuint EnvironmentManager::loadHdrTexture(const std::filesystem::path& path)
{
    stbi_set_flip_vertically_on_load(true);

    int width=0, height=0, components=0;
    float* data = stbi_loadf(path.string().c_str(), &width, &height, &components, 3);
    if (!data) {
        std::cerr << "[EnvManager] Failed to load HDR environment: " << path << "\n";
        stbi_set_flip_vertically_on_load(false);
        return 0;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    GLint prevUnpack = 0; glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpack);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    stbi_set_flip_vertically_on_load(false);
    return texture;
}


void EnvironmentManager::convertEquirectangularToCubemap(EnvironmentTextures& textures, int cubeSize)
{
    assert(m_bakeTextureUnit >= 0);

    // Save state
    GLint prevViewport[4]; glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO = 0;      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevRBO = 0;      glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRBO);

    // Set up capture FBO/RBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubeSize, cubeSize);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_captureRBO);

    // State for cube render
    glDisable(GL_CULL_FACE);          // don’t accidentally cull cube faces
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    // Create target cubemap (no data yet)
    glGenTextures(1, &textures.envCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textures.envCubemap);
    for (unsigned int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, cubeSize, cubeSize, 0, GL_RGB, GL_FLOAT, nullptr);

    // Wrap; (min filter will be set after we generate mipmaps)
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Shader to convert equirect → cubemap
    m_equirectangularShader.bind();
    const GLint locProjection = m_equirectangularShader.getUniformLocation("projection");
    const GLint locView       = m_equirectangularShader.getUniformLocation("view");
    const GLint locEquirect   = m_equirectangularShader.getUniformLocation("equirectangularMap");
    if (locEquirect == -1) {
        std::cerr << "[EnvManager] equirect_to_cubemap.frag must declare `uniform sampler2D equirectangularMap;`\n";
    }
    assert(locEquirect != -1 && "equirect_to_cubemap.frag must declare `uniform sampler2D equirectangularMap;`");
    glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(kCaptureProjection));
    glUniform1i(locEquirect, m_bakeTextureUnit);

    // Bind HDR equirect texture on bake unit
    GLint previousActive = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActive);

    // Save and temporarily clear any sampler bound to the bake unit so the
    // pass samples with a clean sampler state (prevents compare samplers etc.)
    GLint prevSampler = 0;
    glGetIntegeri_v(GL_SAMPLER_BINDING, static_cast<GLuint>(m_bakeTextureUnit), &prevSampler);

    glActiveTexture(GL_TEXTURE0 + m_bakeTextureUnit);
    glBindSampler(static_cast<GLuint>(m_bakeTextureUnit), 0);
    glBindTexture(GL_TEXTURE_2D, textures.hdrTexture);

    // Render each face
    glViewport(0, 0, cubeSize, cubeSize);
    for (unsigned int i = 0; i < 6; ++i) {
        glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(kCaptureViews[i]));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, textures.envCubemap, 0);
        if (i == 0)
            logFramebufferStatus("convertEquirectangularToCubemap");
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, textures.envCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
#ifndef NDEBUG
    {
        std::vector<float> face(static_cast<std::size_t>(cubeSize) * static_cast<std::size_t>(cubeSize) * 3);
        glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0, GL_RGB, GL_FLOAT, face.data());
        float maxV = 0.0f;
        for (float v : face)
            maxV = std::max(maxV, v);
        if (maxV == 0.0f) {
            std::cerr << "[EnvManager] STILL zero after shader fix – check HDR path & sampler name.\n";
        } else {
            std::cerr << "[EnvManager] Cubemap face 0 OK, max=" << maxV << "\n";
        }
    }
#endif
    // ----------------------------------------------------------

    // Restore state and clean up
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, prevRBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    // Clear temporary bindings we made during the bake. This unbinds textures
    // and sampler; afterwards restore the sampler that was previously bound
    // to the bake unit so we don't permanently steal it from other systems.
    clearTextureBindings({m_bakeTextureUnit});
#ifndef NDEBUG
    debugAssertTextureUnitCleared(static_cast<GLuint>(m_bakeTextureUnit), GL_TEXTURE_BINDING_2D);
#endif
    // restore previous sampler for the bake unit
    glActiveTexture(GL_TEXTURE0 + m_bakeTextureUnit);
    glBindSampler(static_cast<GLuint>(m_bakeTextureUnit), static_cast<GLuint>(prevSampler));
    // make sure no local textures remain bound
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(previousActive);
}


void EnvironmentManager::convolveIrradiance(EnvironmentTextures& textures, int irradianceSize)
{
    assert(m_bakeTextureUnit >= 0);

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevRBO = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, irradianceSize, irradianceSize);

    glGenTextures(1, &textures.irradianceCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textures.irradianceCubemap);
    for (unsigned int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, irradianceSize, irradianceSize, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    m_irradianceShader.bind();
    GLint locProjection = m_irradianceShader.getUniformLocation("projection");
    GLint locView = m_irradianceShader.getUniformLocation("view");
    GLint locEnv = m_irradianceShader.getUniformLocation("environmentMap");
    glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(kCaptureProjection));
    glUniform1i(locEnv, m_bakeTextureUnit);

    GLint previousActive = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActive);

    // Save and temporarily clear any sampler bound to the bake unit.
    GLint prevSampler = 0;
    glGetIntegeri_v(GL_SAMPLER_BINDING, static_cast<GLuint>(m_bakeTextureUnit), &prevSampler);

    glActiveTexture(GL_TEXTURE0 + m_bakeTextureUnit);
    glBindSampler(static_cast<GLuint>(m_bakeTextureUnit), 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textures.envCubemap);

    glViewport(0, 0, irradianceSize, irradianceSize);
    for (unsigned int i = 0; i < 6; ++i) {
        glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(kCaptureViews[i]));
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, textures.irradianceCubemap, 0);
        if (i == 0)
            logFramebufferStatus("convolveIrradiance");
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderCube();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, prevRBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    clearTextureBindings({m_bakeTextureUnit});
#ifndef NDEBUG
    debugAssertTextureUnitCleared(static_cast<GLuint>(m_bakeTextureUnit), GL_TEXTURE_BINDING_CUBE_MAP);
#endif
    // restore previous sampler for the bake unit
    glActiveTexture(GL_TEXTURE0 + m_bakeTextureUnit);
    glBindSampler(static_cast<GLuint>(m_bakeTextureUnit), static_cast<GLuint>(prevSampler));
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(previousActive);
}

void EnvironmentManager::prefilterSpecular(EnvironmentTextures& textures, int baseSize, int mipLevels)
{
    assert(m_bakeTextureUnit >= 0);

    const int maxPossibleMipLevels = static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(baseSize, 1))))) + 1;
    mipLevels = std::clamp(mipLevels, 1, maxPossibleMipLevels);

    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevRBO = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRBO);

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_captureRBO);

    glGenTextures(1, &textures.prefilteredCubemap);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textures.prefilteredCubemap);
    for (unsigned int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F, baseSize, baseSize, 0, GL_RGB, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    textures.prefilterMipLevels = mipLevels;

    m_prefilterShader.bind();
    GLint locProjection = m_prefilterShader.getUniformLocation("projection");
    GLint locView = m_prefilterShader.getUniformLocation("view");
    GLint locEnv = m_prefilterShader.getUniformLocation("environmentMap");
    GLint locRoughness = m_prefilterShader.getUniformLocation("roughness");
    glUniformMatrix4fv(locProjection, 1, GL_FALSE, glm::value_ptr(kCaptureProjection));
    glUniform1i(locEnv, m_bakeTextureUnit);

    GLint previousActive = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActive);

    // Save and clear any sampler on the bake unit for the prefilter pass so
    // sampling occurs with a known clean sampler state.
    GLint prevSampler = 0;
    glGetIntegeri_v(GL_SAMPLER_BINDING, static_cast<GLuint>(m_bakeTextureUnit), &prevSampler);

    glActiveTexture(GL_TEXTURE0 + m_bakeTextureUnit);
    glBindSampler(static_cast<GLuint>(m_bakeTextureUnit), 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textures.envCubemap);

    for (int mip = 0; mip < mipLevels; ++mip) {
        const int mipWidth = static_cast<int>(baseSize * std::pow(0.5f, mip));
        const int mipHeight = mipWidth;
        glBindRenderbuffer(GL_RENDERBUFFER, m_captureRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipWidth, mipHeight);
        glViewport(0, 0, mipWidth, mipHeight);

        const float roughness = static_cast<float>(mip) / static_cast<float>(std::max(mipLevels - 1, 1));
        glUniform1f(locRoughness, roughness);

        for (unsigned int i = 0; i < 6; ++i) {
            glUniformMatrix4fv(locView, 1, GL_FALSE, glm::value_ptr(kCaptureViews[i]));
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, textures.prefilteredCubemap, mip);
            if (i == 0)
                logFramebufferStatus("prefilterSpecular");
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderCube();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, prevRBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    clearTextureBindings({m_bakeTextureUnit});
#ifndef NDEBUG
    debugAssertTextureUnitCleared(static_cast<GLuint>(m_bakeTextureUnit), GL_TEXTURE_BINDING_CUBE_MAP);
#endif
    // restore previous sampler for the bake unit
    glActiveTexture(GL_TEXTURE0 + m_bakeTextureUnit);
    glBindSampler(static_cast<GLuint>(m_bakeTextureUnit), static_cast<GLuint>(prevSampler));
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(previousActive);
}

void EnvironmentManager::generateBrdfLutTexture()
{
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevRBO = 0;
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRBO);

    glGenTextures(1, &m_brdfLut);
    glBindTexture(GL_TEXTURE_2D, m_brdfLut);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 512, 512, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindFramebuffer(GL_FRAMEBUFFER, m_captureFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_captureRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_brdfLut, 0);

    glViewport(0, 0, 512, 512);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_brdfShader.bind();
    renderFullscreenQuad();

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, prevRBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBindTexture(GL_TEXTURE_2D, 0);
}

