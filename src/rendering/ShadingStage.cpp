// SPDX-License-Identifier: MIT

#include "rendering/ShadingStage.h"
#include "rendering/texture.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()

#include <glad/glad.h>

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <string>

namespace {

constexpr std::array<GLuint, ShadingStage::kMaterialTextureUnitCount + 5 + ShadingStage::kPointShadowUnitCount> kTrackedTextureUnits {
    0, 1, 2, 3, 4,
    ShadingStage::kEnvIrradianceUnit,
    ShadingStage::kEnvPrefilterUnit,
    ShadingStage::kEnvBrdfUnit,
    ShadingStage::kEnvSkyboxUnit,
    ShadingStage::kShadowMapUnit,
    ShadingStage::kPointShadowUnitBase + 0,
    ShadingStage::kPointShadowUnitBase + 1,
    ShadingStage::kPointShadowUnitBase + 2,
    ShadingStage::kPointShadowUnitBase + 3,
    ShadingStage::kPointShadowUnitBase + 4,
    ShadingStage::kPointShadowUnitBase + 5,
    ShadingStage::kPointShadowUnitBase + 6,
    ShadingStage::kPointShadowUnitBase + 7
};

void resetTrackedTextureUnits()
{
    GLint maxUnits = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxUnits);
    for (GLuint unit : kTrackedTextureUnits) {
        if (unit < static_cast<GLuint>(maxUnits)) {
            glBindTextureUnit(unit, 0);
            glBindSampler(unit, 0);
        }
    }
}

bool isSamplerType(GLenum type)
{
    switch (type) {
    case GL_SAMPLER_2D:
    case GL_SAMPLER_2D_SHADOW:
    case GL_SAMPLER_CUBE:
    case GL_SAMPLER_CUBE_SHADOW:
    case GL_SAMPLER_2D_ARRAY:
#ifdef GL_SAMPLER_CUBE_MAP_ARRAY
    case GL_SAMPLER_CUBE_MAP_ARRAY:
#endif
        return true;
    default:
        return false;
    }
}

void dumpActiveSamplers(GLuint program)
{
#ifndef NDEBUG
    if (program == 0)
        return;

    GLint count = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    if (count <= 0)
        return;

    constexpr GLsizei kNameBuffer = 256;
    GLchar name[kNameBuffer];
    for (GLint i = 0; i < count; ++i) {
        GLsizei nameLength = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program,
            static_cast<GLuint>(i),
            kNameBuffer,
            &nameLength,
            &size,
            &type,
            name);
        if (!isSamplerType(type))
            continue;
        std::string label(name, static_cast<std::size_t>(nameLength));
        std::cout << "[ActiveUniform] " << label << " size=" << size << " type=0x" << std::hex << type << std::dec << "\n";
    }
#else
    (void)program;
#endif
}

void logTextureBinding(const char* label, GLuint unit, GLenum bindingTarget)
{
    GLint bound = 0;
    glGetIntegeri_v(bindingTarget, unit, &bound);
    std::cout << "    unit[" << unit << "] " << label << " -> tex=" << bound << "\n";
}

#ifndef NDEBUG
void validateTrackedUnitAvailability()
{
    GLint maxUnits = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxUnits);
    for (GLuint unit : kTrackedTextureUnits) {
        if (unit >= static_cast<GLuint>(maxUnits)) {
            throw std::runtime_error("Insufficient texture units available for shading stage bindings.");
        }
    }
}
#endif

glm::mat4 composeTransform(const RenderMaterial& material)
{
    const glm::vec3 translation = material.translation;
    const glm::vec3 rotationRadians = glm::radians(material.rotationEuler);
    const glm::vec3 scale = glm::max(material.scale, glm::vec3(0.0001f));

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 rotation = glm::yawPitchRoll(rotationRadians.y, rotationRadians.x, rotationRadians.z);
    transform *= rotation;
    transform = glm::scale(transform, scale);
    return transform;
}

void resetMaterialParameters(RenderMaterial& material)
{
    const bool preservePBR = material.usePBR;
    const bool preserveUnlit = material.unlit;

    auto albedo = material.albedoMap;
    auto metallicRoughness = material.metallicRoughnessMap;
    auto normal = material.normalMap;
    auto ao = material.aoMap;
    auto emissive = material.emissiveMap;

    RenderMaterial defaults;
    defaults.usePBR = preservePBR;
    defaults.unlit = preserveUnlit;

    defaults.baseColor = glm::vec3(1.0f);
    defaults.diffuseColor = glm::vec3(1.0f);
    defaults.specularColor = glm::vec3(0.04f);
    defaults.shininess = 64.0f;

    defaults.albedoMap = std::move(albedo);
    defaults.metallicRoughnessMap = std::move(metallicRoughness);
    defaults.normalMap = std::move(normal);
    defaults.aoMap = std::move(ao);
    defaults.emissiveMap = std::move(emissive);

    defaults.translation = glm::vec3(0.0f);
    defaults.rotationEuler = glm::vec3(0.0f);
    defaults.scale = glm::vec3(1.0f);

    material = std::move(defaults);
    material.refreshTextureUsageFlags();
}

void synchronizeDerivedValues(RenderMaterial& material)
{
    material.baseColor = glm::clamp(material.baseColor, glm::vec3(0.0f), glm::vec3(1.0f));
    material.diffuseColor = glm::clamp(material.diffuseColor, glm::vec3(0.0f), glm::vec3(1.0f));
    material.specularColor = glm::clamp(material.specularColor, glm::vec3(0.0f), glm::vec3(1.0f));
    material.shininess = glm::clamp(material.shininess, 1.0f, 512.0f);
    material.ao = glm::clamp(material.ao, 0.0f, 1.0f);
    material.aoIntensity = glm::max(material.aoIntensity, 0.0f);
    material.normalStrength = glm::max(material.normalStrength, 0.0f);
    material.normalScale = glm::max(material.normalScale, 0.0f);
    material.emissiveIntensity = glm::max(material.emissiveIntensity, 0.0f);

    if (material.usePBR) {
        material.diffuseColor = material.baseColor;
        material.shininess = glm::clamp(256.0f * (1.0f - material.roughness), 1.0f, 512.0f);
    } else {
        material.baseColor = material.diffuseColor;
        material.roughness = glm::clamp(1.0f - (material.shininess / 256.0f), 0.04f, 1.0f);
    }
}

void configureLightingBindings(const Shader& shader)
{
    const GLuint program = shader.id();
    if (program == 0)
        return;

    const GLuint shadowBlockIndex = glGetUniformBlockIndex(program, "ShadowDataBlock");
    if (shadowBlockIndex != GL_INVALID_INDEX)
        glUniformBlockBinding(program, shadowBlockIndex, ShadingStage::kShadowMatricesBinding);

    const GLuint lightBufferIndex = glGetProgramResourceIndex(program, GL_SHADER_STORAGE_BLOCK, "LightBuffer");
    if (lightBufferIndex != GL_INVALID_INDEX)
        glShaderStorageBlockBinding(program, lightBufferIndex, ShadingStage::kLightSsboBinding);

    const GLint samplerLocation = glGetUniformLocation(program, "uShadowMapArray");
    const GLint pointSamplerLocation = glGetUniformLocation(program, "uPointShadowMaps");
    if (samplerLocation >= 0 || pointSamplerLocation >= 0) {
        GLint previousProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
        if (static_cast<GLuint>(previousProgram) != program)
            glUseProgram(program);
        if (samplerLocation >= 0)
            glUniform1i(samplerLocation, ShadingStage::kShadowMapUnit);
        if (pointSamplerLocation >= 0) {
            std::array<GLint, ShadingStage::kPointShadowUnitCount> units {};
            for (std::size_t i = 0; i < units.size(); ++i)
                units[i] = static_cast<GLint>(ShadingStage::kPointShadowUnitBase + static_cast<GLuint>(i));
            glUniform1iv(pointSamplerLocation, static_cast<GLsizei>(units.size()), units.data());
        }
        if (static_cast<GLuint>(previousProgram) != program)
            glUseProgram(static_cast<GLuint>(previousProgram));
    }
}

}

ShadingStage::ShadingStage(const std::filesystem::path& shaderDirectory)
{
    m_shader.load("blinn_phong", shaderDirectory / "blinn_phong.vert", shaderDirectory / "blinn_phong.frag");
    m_shader.load("pbr", shaderDirectory / "pbr.vert", shaderDirectory / "pbr.frag");

    if (Shader* blinnShader = m_shader.find("blinn_phong"))
        configureLightingBindings(*blinnShader);
    if (Shader* pbrShader = m_shader.find("pbr"))
        configureLightingBindings(*pbrShader);
    m_shader.bind("blinn_phong");
#ifndef NDEBUG
    validateTrackedUnitAvailability();
#endif
    m_activeShader = &m_shader.current();

    ensureBuffers();
}

// (world curvature setters are implemented inline in the header)

ShadingStage::~ShadingStage()
{
    resetTrackedTextureUnits();
    if (m_materialSSBO != 0) {
        glDeleteBuffers(1, &m_materialSSBO);
        m_materialSSBO = 0;
    }
    if (m_perFrameUBO != 0) {
        glDeleteBuffers(1, &m_perFrameUBO);
        m_perFrameUBO = 0;
    }
    if (m_objectUBO != 0) {
        glDeleteBuffers(1, &m_objectUBO);
        m_objectUBO = 0;
    }
    if (m_envCubeSampler != 0) {
        glDeleteSamplers(1, &m_envCubeSampler);
        m_envCubeSampler = 0;
    }
    if (m_env2DSampler != 0) {
        glDeleteSamplers(1, &m_env2DSampler);
        m_env2DSampler = 0;
    }
    if (m_shadowSamplerCompare != 0) {
        glDeleteSamplers(1, &m_shadowSamplerCompare);
        m_shadowSamplerCompare = 0;
    }
    if (m_shadowSamplerCube != 0) {
        glDeleteSamplers(1, &m_shadowSamplerCube);
        m_shadowSamplerCube = 0;
    }
}

void ShadingStage::ensureBuffers()
{
    if (m_perFrameUBO == 0) {
        glGenBuffers(1, &m_perFrameUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, m_perFrameUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(PerFrameData), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, kPerFrameBinding, m_perFrameUBO);
    }

    if (m_objectUBO == 0) {
        glGenBuffers(1, &m_objectUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, m_objectUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(ObjectGPUData), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, kPerObjectBinding, m_objectUBO);
    }

    if (m_materialSSBO == 0) {
        glGenBuffers(1, &m_materialSSBO);
        m_materialCapacity = 16;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_materialSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(m_materialCapacity * sizeof(MaterialGPUData)), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialSsboBinding, m_materialSSBO);
    }
}

void ShadingStage::ensureMaterialCapacity(std::size_t requiredCapacity)
{
    if (m_materialSSBO == 0)
        ensureBuffers();

    if (requiredCapacity <= m_materialCapacity)
        return;

    if (m_materialCapacity == 0)
        m_materialCapacity = 1;

    while (m_materialCapacity < requiredCapacity)
        m_materialCapacity *= 2;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_materialSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(m_materialCapacity * sizeof(MaterialGPUData)), nullptr, GL_DYNAMIC_DRAW);

    for (const MaterialRecord& record : m_materialRecords) {
        if (!record.material)
            continue;
        glBufferSubData(GL_SHADER_STORAGE_BUFFER,
            static_cast<GLintptr>(record.index * sizeof(MaterialGPUData)),
            static_cast<GLsizeiptr>(sizeof(MaterialGPUData)),
            &record.gpuData);
    }

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialSsboBinding, m_materialSSBO);
}

void ShadingStage::setEnvironmentState(const EnvironmentState& state)
{
    m_environmentState = state;
    m_boundMaterialState.valid = false;
}

void ShadingStage::setLightBinding(const LightBufferBinding& binding)
{
    m_lightBinding = binding;
    m_frameActive = false;
}

void ShadingStage::drawImGui(std::vector<MeshInstance>& instances, int selectedInstanceIndex)
{
    if (!ImGui::Begin("Shading Controls")) {
        ImGui::End();
        return;
    }

    drawImGuiPanel(instances, selectedInstanceIndex);

    ImGui::End();
}

void ShadingStage::drawImGuiPanel(std::vector<MeshInstance>& instances, int selectedInstanceIndex)
{
    ImGui::TextUnformatted("Material");
    ImGui::Separator();

    // Lighting controls (global ambient settings)
    ImGui::TextUnformatted("Lighting");
    ImGui::Separator();
    if (ImGui::ColorEdit3("Ambient Color", glm::value_ptr(m_settings.ambientColor))) {
        m_settings.ambientColor = glm::clamp(m_settings.ambientColor, glm::vec3(0.0f), glm::vec3(10.0f));
    }
    if (ImGui::SliderFloat("Ambient Strength", &m_settings.ambientStrength, 0.0f, 10.0f)) {
        m_settings.ambientStrength = glm::max(m_settings.ambientStrength, 0.0f);
    }
    ImGui::Separator();

    if (instances.empty()) {
        ImGui::TextDisabled("No mesh instances loaded.");
        return;
    }

    if (selectedInstanceIndex < 0 || selectedInstanceIndex >= static_cast<int>(instances.size())) {
        ImGui::TextDisabled("Select an instance in the Mesh Manager to edit material parameters.");
        return;
    }

    MeshInstance& selectedInstance = instances[static_cast<std::size_t>(selectedInstanceIndex)];
    if (selectedInstance.drawItems().empty()) {
        ImGui::TextDisabled("Selected instance has no meshes.");
        return;
    }

    MeshDrawItem& item = selectedInstance.drawItems().front();
    RenderMaterial& material = item.material;
    material.refreshTextureUsageFlags();

    ImGui::Text("Object: %s", selectedInstance.name().c_str());
    ImGui::Separator();

    bool usePBR = material.usePBR;
    if (ImGui::Checkbox("Use PBR", &usePBR))
        material.usePBR = usePBR;
    ImGui::SameLine();
    ImGui::Checkbox("Unlit", &material.unlit);

    ImGui::Separator();

    // ===== TRANSPARENCY CONTROLS =====
    ImGui::TextUnformatted("Transparency");
    bool isTransparent = material.isTransparent;
    if (ImGui::Checkbox("Transparent", &isTransparent)) {
        material.isTransparent = isTransparent;
        // If enabling transparency, ensure blending mode is set
        if (isTransparent && material.alphaMode == AlphaMode::Opaque) {
            material.alphaMode = AlphaMode::Blend;
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enable transparency with alpha blending.\nTransparent objects are rendered back-to-front.");
    }

    // Show opacity slider only when transparent is enabled
    if (material.isTransparent) {
        float opacity = material.opacity;
        if (ImGui::SliderFloat("Opacity", &opacity, 0.0f, 1.0f, "%.2f")) {
            material.opacity = glm::clamp(opacity, 0.0f, 1.0f);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0.0 = fully transparent, 1.0 = fully opaque");
        }
    }

    ImGui::Separator();

    if (material.usePBR) {
        ImGui::TextUnformatted("PBR Parameters");
        if (ImGui::ColorEdit3("Base Color", glm::value_ptr(material.baseColor)))
            material.diffuseColor = material.baseColor;

        if (ImGui::SliderFloat("Metallic", &material.metallic, 0.0f, 1.0f))
            material.metallic = glm::clamp(material.metallic, 0.0f, 1.0f);
        if (ImGui::SliderFloat("Roughness", &material.roughness, 0.04f, 1.0f))
            material.roughness = glm::clamp(material.roughness, 0.04f, 1.0f);

        ImGui::Separator();
        ImGui::TextDisabled("Texture bindings");
        ImGui::BulletText("Base Color Map: %s", material.hasAlbedoTexture ? "yes" : "none");
        ImGui::BulletText("Metallic-Roughness Map: %s", material.hasMetallicRoughnessTexture ? "yes" : "none");
        ImGui::BulletText("Normal Map: %s", material.hasNormalTexture ? "yes" : "none");
        ImGui::BulletText("Occlusion Map: %s", material.hasAOTexture ? "yes" : "none");
        ImGui::BulletText("Emissive Map: %s", material.hasEmissiveTexture ? "yes" : "none");
        ImGui::BulletText("Height Map: %s", material.hasHeightTexture ? "yes" : "none");
        ImGui::ColorEdit3("Emissive Color", glm::value_ptr(material.emissive));
        ImGui::SliderFloat("Emissive Intensity", &material.emissiveIntensity, 0.0f, 10.0f);
    } else {
        ImGui::TextUnformatted("Blinn-Phong Parameters");
        if (ImGui::ColorEdit3("Diffuse Color", glm::value_ptr(material.diffuseColor)))
            material.baseColor = material.diffuseColor;
        ImGui::ColorEdit3("Specular (Ks)", glm::value_ptr(material.specularColor));
        if (ImGui::SliderFloat("Shininess (Ns)", &material.shininess, 1.0f, 512.0f))
            material.shininess = glm::clamp(material.shininess, 1.0f, 512.0f);
    }

    // Parallax mapping controls (global for now)
    if (ImGui::CollapsingHeader("Parallax Mapping")) {
        ImGui::Checkbox("Enable Parallax", &m_parallaxEnabled);
        ImGui::SameLine();
        ImGui::Checkbox("Use Normal.a as Height", &m_parallaxUseNormalAlpha);
        ImGui::SameLine();
        ImGui::Checkbox("Invert Offset", &m_parallaxInvertOffset);
        ImGui::SliderFloat("Parallax Scale", &m_parallaxScale, 0.0f, 0.1f, "%.4f");
        // Bias commonly -0.5 * scale; keep it editable
        ImGui::SliderFloat("Parallax Bias", &m_parallaxBias, -0.1f, 0.0f, "%.4f");
        ImGui::TextDisabled("Tip: For best results, pack height into normal alpha or add a dedicated height map later.");
    }


    ImGui::Separator();
    bool transformChanged = false;
    transformChanged |= ImGui::DragFloat3("Translation", glm::value_ptr(material.translation), 0.05f);
    transformChanged |= ImGui::DragFloat3("Rotation (deg)", glm::value_ptr(material.rotationEuler), 0.5f);
    transformChanged |= ImGui::DragFloat3("Scale", glm::value_ptr(material.scale), 0.01f, 0.0f, 100.0f);

    if (transformChanged)
        selectedInstance.setTransform(composeTransform(material));

    if (ImGui::Button("Reset Material Parameters"))
        resetMaterialParameters(material);
    ImGui::SameLine();
    if (ImGui::Button("Reset Transform")) {
        material.translation = glm::vec3(0.0f);
        material.rotationEuler = glm::vec3(0.0f);
        material.scale = glm::vec3(1.0f);
        selectedInstance.setTransform(composeTransform(material));
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Debug Views");
    bool debugUVs = m_settings.debugShowUVs;
    if (ImGui::Checkbox("Show Debug Overlay", &debugUVs))
        m_settings.debugShowUVs = debugUVs;
    if (m_settings.debugShowUVs) {
        static const char* kUvDebugLabels[] = {
            "Albedo",
            "Normals",
            "Metallic",
            "Roughness",
            "Ambient Occlusion",
            "Emissive",
            "Height (Normal.a)"
        };
        int debugIndex = static_cast<int>(m_settings.debugTarget);
        if (ImGui::Combo("Debug Target", &debugIndex, kUvDebugLabels, IM_ARRAYSIZE(kUvDebugLabels)))
            m_settings.debugTarget = static_cast<LightingSettings::UVDebugTarget>(glm::clamp(debugIndex, 0, static_cast<int>(LightingSettings::UVDebugTarget::Height)));
    }

    bool forceUpload = Texture::forcePerDrawUpload();
    if (ImGui::Checkbox("Force Per-Draw Texture Upload", &forceUpload)) {
        Texture::setForcePerDrawUpload(forceUpload);
    }
    if (forceUpload) {
        ImGui::SameLine();
        ImGui::TextDisabled("(slow safety mode)");
    }

    bool debugLogging = m_enableDebugLogging;
    if (ImGui::Checkbox("Log Texture Bindings", &debugLogging)) {
        m_enableDebugLogging = debugLogging;
    }
    if (m_enableDebugLogging) {
        ImGui::SameLine();
        ImGui::TextDisabled("(stdout spam)");
    }

    synchronizeDerivedValues(material);
}

void ShadingStage::beginFrame(const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition)
{
    ensureBuffers();

    m_frameData.view = view;
    m_frameData.projection = projection;
    m_frameData.viewProjection = projection * view;
    m_frameData.inverseView = glm::inverse(view);
    m_frameData.cameraPos = glm::vec4(cameraPosition, 1.0f);
    m_frameData.lightPos = glm::vec4(m_settings.lightPos, 1.0f);
    m_frameData.lightColor = glm::vec4(m_settings.lightColor, 1.0f);
    m_frameData.ambientColorStrength = glm::vec4(m_settings.ambientColor, m_settings.ambientStrength);

    const bool iblReady = m_environmentState.useIBL && m_environmentState.isValid();
    const int lightCount = std::max(m_lightBinding.lightCount, 0);

    m_frameData.frameFlags = glm::ivec4(lightCount,
        m_settings.debugShowUVs ? 1 : 0,
        static_cast<int>(m_settings.debugTarget),
        iblReady ? 1 : 0);
    m_frameData.envParams = glm::vec4(iblReady ? m_environmentState.intensity : 0.0f,
        iblReady ? m_environmentState.prefilterMipLevels : 0.0f,
        0.0f,
        0.0f);

    glBindBuffer(GL_UNIFORM_BUFFER, m_perFrameUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PerFrameData), &m_frameData);
    glBindBufferBase(GL_UNIFORM_BUFFER, kPerFrameBinding, m_perFrameUBO);

    for (GLuint unit = 0; unit < kMaterialTextureUnitCount; ++unit) {
        glBindTextureUnit(unit, 0);
        glBindSampler(unit, 0);
    }
    m_boundMaterialState.valid = false;

    if (m_lightBinding.lightSSBO != 0)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kLightSsboBinding, m_lightBinding.lightSSBO);
    else
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kLightSsboBinding, 0);

    if (m_lightBinding.shadowMatricesUBO != 0)
        glBindBufferBase(GL_UNIFORM_BUFFER, kShadowMatricesBinding, m_lightBinding.shadowMatricesUBO);
    else
        glBindBufferBase(GL_UNIFORM_BUFFER, kShadowMatricesBinding, 0);

    ensureShadowSampler();
    GLuint directionalShadow = m_lightBinding.directionalShadowTexture;
    if (directionalShadow == 0)
        directionalShadow = m_lightBinding.directionalShadowFallback;

    if (directionalShadow != 0) {
        glBindTextureUnit(kShadowMapUnit, directionalShadow);
        glBindSampler(kShadowMapUnit, m_shadowSamplerCompare);
    } else {
        glBindTextureUnit(kShadowMapUnit, 0);
        glBindSampler(kShadowMapUnit, 0);
    }

    const int pointShadowCount = glm::clamp(m_lightBinding.pointShadowCount, 0, static_cast<int>(kPointShadowUnitCount));
    const GLuint pointFallback = m_lightBinding.pointShadowFallback;
    for (GLuint i = 0; i < kPointShadowUnitCount; ++i) {
        GLuint texture = pointFallback;
        if (i < static_cast<GLuint>(pointShadowCount)) {
            GLuint candidate = m_lightBinding.pointShadowTextures[i];
            if (candidate != 0)
                texture = candidate;
        }
        glBindTextureUnit(kPointShadowUnitBase + i, texture);
        if (m_shadowSamplerCube != 0)
            glBindSampler(kPointShadowUnitBase + i, m_shadowSamplerCube);
        else
            glBindSampler(kPointShadowUnitBase + i, 0);
    }

    if (m_enableDebugLogging) {
        std::cout << "[ShadowBind] dirUnit=" << kShadowMapUnit
                  << " dirTex=" << m_lightBinding.directionalShadowTexture
                  << " pointBase=" << kPointShadowUnitBase
                  << " count=" << pointShadowCount
                  << " fallback=" << pointFallback << "\n";
        logTextureBinding("dirShadow", kShadowMapUnit, GL_TEXTURE_BINDING_2D);
        for (GLuint i = 0; i < kPointShadowUnitCount; ++i) {
            const std::string label = "pointShadow[" + std::to_string(i) + "]";
            logTextureBinding(label.c_str(), kPointShadowUnitBase + i, GL_TEXTURE_BINDING_CUBE_MAP);
        }
    }
    m_frameActive = true;
}

void ShadingStage::endFrame()
{
    m_frameActive = false;
    m_boundMaterialState.valid = false;
}

ShadingStage::MaterialBindingInfo ShadingStage::evaluateMaterialUsage(const RenderMaterial& material,
    bool hasPrimaryUVs,
    bool hasSecondaryUVs) const
{
    MaterialBindingInfo info {};
    info.hasPrimaryUVs = hasPrimaryUVs;
    info.hasSecondaryUVs = hasSecondaryUVs;

    const auto hasUVSet = [&](unsigned int index) {
        switch (index) {
        case 0:
            return hasPrimaryUVs;
        case 1:
            return hasSecondaryUVs;
        default:
            return false;
        }
    };

    const auto evaluate = [&](const std::shared_ptr<Texture>& texture, unsigned int uvIndex, const char* label) {
        if (!texture)
            return false;
        const bool available = hasUVSet(uvIndex);
        if (!available && m_enableDebugLogging) {
            std::cout << "Material texture '" << label << "' requires UV" << uvIndex
                      << " but geometry provides only " << (hasPrimaryUVs ? 1 : 0) + (hasSecondaryUVs ? 1 : 0)
                      << " set(s). Skipping binding.\n";
        }
        return available;
    };

    info.useAlbedo = evaluate(material.albedoMap, material.albedoUV, "albedo");
    info.useMetallicRoughness = material.usePBR && evaluate(material.metallicRoughnessMap, material.metallicRoughnessUV, "metallicRoughness");
    info.useNormal = evaluate(material.normalMap, material.normalUV, "normal");
    info.useAO = evaluate(material.aoMap, material.aoUV, "ao");
    info.useEmissive = evaluate(material.emissiveMap, material.emissiveUV, "emissive");
    info.useHeight = evaluate(material.heightMap, material.heightUV, "height");

    info.albedoUV = info.useAlbedo ? static_cast<int>(material.albedoUV) : 0;
    info.metallicRoughnessUV = info.useMetallicRoughness ? static_cast<int>(material.metallicRoughnessUV) : 0;
    info.normalUV = info.useNormal ? static_cast<int>(material.normalUV) : 0;
    info.aoUV = info.useAO ? static_cast<int>(material.aoUV) : 0;
    info.emissiveUV = info.useEmissive ? static_cast<int>(material.emissiveUV) : 0;
    info.heightUV = info.useHeight ? static_cast<int>(material.heightUV) : 0;

    return info;
}

ShadingStage::MaterialGPUData ShadingStage::buildMaterialData(const RenderMaterial& material) const
{
    MaterialGPUData gpu {};

    const float opacity = glm::clamp(material.opacity, 0.0f, 1.0f);
    const float metallic = glm::clamp(material.metallic, 0.0f, 1.0f);
    const float roughness = glm::clamp(material.roughness, 0.04f, 1.0f);
    const float ao = glm::clamp(material.ao, 0.0f, 1.0f);
    const float aoIntensity = glm::max(material.aoIntensity, 0.0f);
    const float normalScale = glm::max(material.normalScale, 0.0f);
    const float normalStrength = glm::max(material.normalStrength, 0.0f);
    const float emissiveIntensity = glm::max(material.emissiveIntensity, 0.0f);
    const float alphaCutoff = glm::clamp(material.alphaCutoff, 0.0f, 1.0f);
    const float shininess = glm::clamp(material.shininess, 1.0f, 512.0f);

    gpu.baseColor = glm::vec4(glm::clamp(material.baseColor, glm::vec3(0.0f), glm::vec3(1.0f)), opacity);
    gpu.diffuseColor = glm::vec4(glm::clamp(material.diffuseColor, glm::vec3(0.0f), glm::vec3(1.0f)), shininess);
    gpu.specularColor = glm::vec4(glm::clamp(material.specularColor, glm::vec3(0.0f), glm::vec3(1.0f)), 0.0f);
    gpu.emissiveColorIntensity = glm::vec4(glm::clamp(material.emissive, glm::vec3(0.0f), glm::vec3(10.0f)), emissiveIntensity);
    gpu.pbrParams = glm::vec4(metallic, roughness, ao, aoIntensity);
    gpu.extraParams = glm::vec4(normalScale, normalStrength, alphaCutoff, shininess);

    gpu.textureUsage = glm::ivec4(material.hasAlbedoTexture ? 1 : 0,
        material.hasMetallicRoughnessTexture ? 1 : 0,
        material.hasNormalTexture ? 1 : 0,
        material.hasAOTexture ? 1 : 0);
    gpu.textureUsage2 = glm::ivec4(material.hasEmissiveTexture ? 1 : 0,
        material.occlusionFromMetallicRoughness ? 1 : 0,
        material.unlit ? 1 : 0,
        material.usePBR ? 1 : 0);
    gpu.uvSets0 = glm::ivec4(static_cast<int>(material.albedoUV),
        static_cast<int>(material.metallicRoughnessUV),
        static_cast<int>(material.normalUV),
        static_cast<int>(material.aoUV));
    gpu.uvSets1 = glm::ivec4(static_cast<int>(material.emissiveUV),
        material.doubleSided ? 1 : 0,
        static_cast<int>(material.alphaMode),
        0);

    gpu.uvTransformAlbedo = glm::vec4(material.albedoUVTransform.offset.x, material.albedoUVTransform.offset.y,
        material.albedoUVTransform.scale.x, material.albedoUVTransform.scale.y);
    gpu.uvTransformMR = glm::vec4(material.metallicRoughnessUVTransform.offset.x, material.metallicRoughnessUVTransform.offset.y,
        material.metallicRoughnessUVTransform.scale.x, material.metallicRoughnessUVTransform.scale.y);
    gpu.uvTransformNormal = glm::vec4(material.normalUVTransform.offset.x, material.normalUVTransform.offset.y,
        material.normalUVTransform.scale.x, material.normalUVTransform.scale.y);
    gpu.uvTransformAO = glm::vec4(material.aoUVTransform.offset.x, material.aoUVTransform.offset.y,
        material.aoUVTransform.scale.x, material.aoUVTransform.scale.y);
    gpu.uvTransformEmissive = glm::vec4(material.emissiveUVTransform.offset.x, material.emissiveUVTransform.offset.y,
        material.emissiveUVTransform.scale.x, material.emissiveUVTransform.scale.y);
    gpu.uvRotations = glm::vec4(material.albedoUVTransform.rotation,
        material.metallicRoughnessUVTransform.rotation,
        material.normalUVTransform.rotation,
        material.aoUVTransform.rotation);
    gpu.uvRotations2 = glm::vec4(material.emissiveUVTransform.rotation, 0.0f, 0.0f, 0.0f);

    return gpu;
}

ShadingStage::MaterialRecord& ShadingStage::getOrCreateMaterialRecord(const RenderMaterial& material)
{
    const auto it = m_materialLookup.find(&material);
    if (it != m_materialLookup.end())
        return m_materialRecords[it->second];

    const std::uint32_t newIndex = static_cast<std::uint32_t>(m_materialRecords.size());
    m_materialRecords.emplace_back();
    MaterialRecord& record = m_materialRecords.back();
    record.material = &material;
    record.index = newIndex;
    record.alphaMode = material.alphaMode;
    record.doubleSided = material.doubleSided;
    record.usePBR = material.usePBR;
    record.unlit = material.unlit;
    record.dirty = true;

    ensureMaterialCapacity(m_materialRecords.size());
    m_materialLookup.emplace(&material, newIndex);
    return record;
}

void ShadingStage::uploadMaterialRecord(MaterialRecord& record)
{
    if (m_materialSSBO == 0)
        ensureBuffers();

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_materialSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
        static_cast<GLintptr>(record.index * sizeof(MaterialGPUData)),
        static_cast<GLsizeiptr>(sizeof(MaterialGPUData)),
        &record.gpuData);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialSsboBinding, m_materialSSBO);
    record.dirty = false;
}

void ShadingStage::bindMaterialResources(MaterialRecord& record,
    const MaterialBindingInfo& bindingInfo,
    bool /*hasTangents*/)
{
    if (!record.material)
        return;

    const RenderMaterial& material = *record.material;

    const std::shared_ptr<Texture> materialTextures[TextureUnits::Material_Count] {
        material.albedoMap,
        material.normalMap,
        material.metallicRoughnessMap,
        material.aoMap,
        material.emissiveMap
    };
    for (std::size_t i = 0; i < TextureUnits::Material_Count; ++i)
        record.textures[i] = materialTextures[i];

    if (m_enableDebugLogging) {
        std::cout << "[MaterialBind] material=" << &material
                  << " useAlbedo=" << bindingInfo.useAlbedo
                  << " albedoTex=" << (material.albedoMap ? material.albedoMap->id() : 0)
                  << " uv=" << material.albedoUV
                  << " hasUV0=" << bindingInfo.hasPrimaryUVs
                  << " hasUV1=" << bindingInfo.hasSecondaryUVs
                  << "\n";
        std::cout << "               useMR=" << bindingInfo.useMetallicRoughness
                  << " mrTex=" << (material.metallicRoughnessMap ? material.metallicRoughnessMap->id() : 0)
                  << " uv=" << material.metallicRoughnessUV
                  << " | useNormal=" << bindingInfo.useNormal
                  << " normalTex=" << (material.normalMap ? material.normalMap->id() : 0)
                  << " uv=" << material.normalUV
                  << "\n";
        std::cout << "               useAO=" << bindingInfo.useAO
                  << " aoTex=" << (material.aoMap ? material.aoMap->id() : 0)
                  << " uv=" << material.aoUV
                  << " | useEmissive=" << bindingInfo.useEmissive
                  << " emissiveTex=" << (material.emissiveMap ? material.emissiveMap->id() : 0)
                  << " uv=" << material.emissiveUV
                  << "\n";
        std::cout << "               useHeight=" << bindingInfo.useHeight
                  << " heightTex=" << (material.heightMap ? material.heightMap->id() : 0)
                  << " uv=" << material.heightUV
                  << "\n";
    }

    const auto bindTextureIf = [](const std::shared_ptr<Texture>& texture,
                                  GLuint unitIndex,
                                  bool shouldBind) {
        TextureUnits::assertNotEnvUnit(unitIndex);
        if (shouldBind && texture) {
            glBindTextureUnit(unitIndex, texture->id());
            glBindSampler(unitIndex, texture->samplerHandle());
        } else {
            glBindTextureUnit(unitIndex, 0);
            glBindSampler(unitIndex, 0);
        }
    };

    bindTextureIf(material.albedoMap,            TextureUnits::Material_Albedo,            bindingInfo.useAlbedo);
    bindTextureIf(material.normalMap,            TextureUnits::Material_Normal,            bindingInfo.useNormal);
    bindTextureIf(material.metallicRoughnessMap, TextureUnits::Material_MetallicRoughness, bindingInfo.useMetallicRoughness);
    bindTextureIf(material.aoMap,                TextureUnits::Material_AO,                bindingInfo.useAO);
    bindTextureIf(material.emissiveMap,          TextureUnits::Material_Emissive,          bindingInfo.useEmissive);
    bindTextureIf(material.heightMap,            5,                                        bindingInfo.useHeight);

    if (m_enableDebugLogging) {
        logTextureBinding("albedo", TextureUnits::Material_Albedo, GL_TEXTURE_BINDING_2D);
        logTextureBinding("normal", TextureUnits::Material_Normal, GL_TEXTURE_BINDING_2D);
        logTextureBinding("metalRough", TextureUnits::Material_MetallicRoughness, GL_TEXTURE_BINDING_2D);
        logTextureBinding("ao", TextureUnits::Material_AO, GL_TEXTURE_BINDING_2D);
        logTextureBinding("emissive", TextureUnits::Material_Emissive, GL_TEXTURE_BINDING_2D);
        logTextureBinding("height", 5, GL_TEXTURE_BINDING_2D);
    }

    const bool wasValid = m_boundMaterialState.valid;
    m_boundMaterialState.materialIndex = record.index;
    m_boundMaterialState.bindingInfo = bindingInfo;
    m_boundMaterialState.valid = true;

    if (!wasValid || m_boundMaterialState.doubleSided != record.doubleSided) {
        if (record.doubleSided) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
        }
        m_boundMaterialState.doubleSided = record.doubleSided;
    }

    if (!wasValid || m_boundMaterialState.alphaMode != record.alphaMode) {
        switch (record.alphaMode) {
        case AlphaMode::Opaque:
        case AlphaMode::Mask:
            glDisable(GL_BLEND);
            break;
        case AlphaMode::Blend:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        }
        m_boundMaterialState.alphaMode = record.alphaMode;
    }

    m_boundMaterialState.usePBR = record.usePBR;
}

    void ShadingStage::rebindEnvironmentForPbr(const Shader& shader)
    {
        if (!m_environmentState.useIBL)
            return;

        ensureEnvSamplers();

        const GLuint program = shader.id();
        if (program == 0)
            return;

        const GLint locIrradiance = glGetUniformLocation(program, "uIrradianceMap");
        const GLint locPrefilter  = glGetUniformLocation(program, "uPreFilterMap");
        const GLint locPrefCount  = glGetUniformLocation(program, "uPrefilterMipCount");
        const GLint locBrdf       = glGetUniformLocation(program, "uBRDFLut");

        const bool hasUniforms = locIrradiance >= 0 || locPrefilter >= 0 || locBrdf >= 0 || locPrefCount >= 0;
        if (!hasUniforms)
            return;

        if (locPrefCount >= 0)
            glUniform1f(locPrefCount, std::max(m_environmentState.prefilterMipLevels - 1.0f, 0.0f));

        if (m_environmentState.irradianceMap != 0 && locIrradiance >= 0) {
            glUniform1i(locIrradiance, static_cast<GLint>(TextureUnits::Env_Irradiance));
            glBindTextureUnit(TextureUnits::Env_Irradiance, m_environmentState.irradianceMap);
            if (m_envCubeSamplerNoMip != 0)
                glBindSampler(TextureUnits::Env_Irradiance, m_envCubeSamplerNoMip);
            else
                glBindSampler(TextureUnits::Env_Irradiance, 0);
        }

        if (m_environmentState.prefilterMap != 0 && locPrefilter >= 0) {
            glUniform1i(locPrefilter, static_cast<GLint>(TextureUnits::Env_Prefilter));
            glBindTextureUnit(TextureUnits::Env_Prefilter, m_environmentState.prefilterMap);
            if (m_envCubeSamplerMip != 0)
                glBindSampler(TextureUnits::Env_Prefilter, m_envCubeSamplerMip);
            else
                glBindSampler(TextureUnits::Env_Prefilter, 0);
        }

        if (m_environmentState.brdfLut != 0 && locBrdf >= 0) {
            glUniform1i(locBrdf, static_cast<GLint>(TextureUnits::Env_BRDF));
            glBindTextureUnit(TextureUnits::Env_BRDF, m_environmentState.brdfLut);
            if (m_env2DSampler != 0)
                glBindSampler(TextureUnits::Env_BRDF, m_env2DSampler);
            else
                glBindSampler(TextureUnits::Env_BRDF, 0);
        }

        if (m_enableDebugLogging) {
            std::cout << "[EnvBind] irradianceUnit=" << TextureUnits::Env_Irradiance
                      << " tex=" << m_environmentState.irradianceMap
                      << " prefilterUnit=" << TextureUnits::Env_Prefilter
                      << " tex=" << m_environmentState.prefilterMap
                      << " brdfUnit=" << TextureUnits::Env_BRDF
                      << " tex=" << m_environmentState.brdfLut
                      << "\n";
            logTextureBinding("envIrradiance", TextureUnits::Env_Irradiance, GL_TEXTURE_BINDING_CUBE_MAP);
            logTextureBinding("envPrefilter", TextureUnits::Env_Prefilter, GL_TEXTURE_BINDING_CUBE_MAP);
            logTextureBinding("envBRDF", TextureUnits::Env_BRDF, GL_TEXTURE_BINDING_2D);
            dumpActiveSamplers(program);
        }
    }

void ShadingStage::updateObjectBuffer(const glm::mat4& model,
    const MaterialRecord& record,
    const MaterialBindingInfo& bindingInfo,
    bool hasTangents,
    bool hasPrimaryUVs,
    bool hasSecondaryUVs)
{
    const glm::mat3 normalMatrix3 = glm::transpose(glm::inverse(glm::mat3(model)));
    glm::mat4 normalMatrix4(1.0f);
    normalMatrix4[0] = glm::vec4(normalMatrix3[0], 0.0f);
    normalMatrix4[1] = glm::vec4(normalMatrix3[1], 0.0f);
    normalMatrix4[2] = glm::vec4(normalMatrix3[2], 0.0f);

    m_objectData.model = model;
    m_objectData.normalMatrix = normalMatrix4;
    m_objectData.materialFlags = glm::ivec4(static_cast<int>(record.index),
        hasTangents ? 1 : 0,
        hasPrimaryUVs ? 1 : 0,
        hasSecondaryUVs ? 1 : 0);
    m_objectData.textureUsage = glm::ivec4(bindingInfo.useAlbedo ? 1 : 0,
        bindingInfo.useMetallicRoughness ? 1 : 0,
        bindingInfo.useNormal ? 1 : 0,
        bindingInfo.useAO ? 1 : 0);
    m_objectData.textureUsage2 = glm::ivec4(bindingInfo.useEmissive ? 1 : 0,
        static_cast<int>(record.alphaMode),
        0,
        0);
    m_objectData.uvSets0 = glm::ivec4(bindingInfo.albedoUV,
        bindingInfo.metallicRoughnessUV,
        bindingInfo.normalUV,
        bindingInfo.aoUV);
    m_objectData.uvSets1 = glm::ivec4(bindingInfo.emissiveUV, 0, 0, 0);

    glBindBuffer(GL_UNIFORM_BUFFER, m_objectUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ObjectGPUData), &m_objectData);
    glBindBufferBase(GL_UNIFORM_BUFFER, kPerObjectBinding, m_objectUBO);
}

void ShadingStage::apply(const glm::mat4& model,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPosition,
    const RenderMaterial& material,
    bool hasPrimaryUVs,
    bool hasSecondaryUVs,
    bool hasTangents)
{
    if (!m_frameActive)
        beginFrame(view, projection, cameraPosition);

    const bool usePBR = material.usePBR;
    const char* shaderName = usePBR ? "pbr" : "blinn_phong";
    if (!m_shader.bind(shaderName))
        throw std::runtime_error(std::string("Requested shader not loaded: ") + shaderName);
    m_activeShader = &m_shader.current();

    // Push global shader toggles (if the shader exposes them)
    {
        GLuint program = m_activeShader->id();
        GLint locEnabled = glGetUniformLocation(program, "uWorldCurvatureEnabled");
        if (locEnabled >= 0)
            glUniform1i(locEnabled, m_worldCurvatureEnabled ? 1 : 0);
        GLint locStrength = glGetUniformLocation(program, "uWorldCurvatureStrength");
        if (locStrength >= 0)
            glUniform1f(locStrength, m_worldCurvatureStrength);
        // fog uniforms
        GLint locFogEnabled = glGetUniformLocation(program, "uFogEnabled");
        if (locFogEnabled >= 0)
            glUniform1i(locFogEnabled, m_fogEnabled ? 1 : 0);
        GLint locFogColor = glGetUniformLocation(program, "uFogColor");
        if (locFogColor >= 0)
            glUniform3fv(locFogColor, 1, glm::value_ptr(m_fogColor));
        GLint locFogDensity = glGetUniformLocation(program, "uFogDensity");
        if (locFogDensity >= 0)
            glUniform1f(locFogDensity, m_fogDensity);
        GLint locFogGrad = glGetUniformLocation(program, "uFogGradient");
        if (locFogGrad >= 0)
            glUniform1f(locFogGrad, m_fogGradient);

        // Parallax uniforms (basic)
        GLint locParallaxEnabled = glGetUniformLocation(program, "uParallaxEnabled");
        if (locParallaxEnabled >= 0)
            glUniform1i(locParallaxEnabled, m_parallaxEnabled ? 1 : 0);
        GLint locParallaxScale = glGetUniformLocation(program, "uParallaxScale");
        if (locParallaxScale >= 0)
            glUniform1f(locParallaxScale, m_parallaxScale);
        GLint locParallaxBias = glGetUniformLocation(program, "uParallaxBias");
        if (locParallaxBias >= 0)
            glUniform1f(locParallaxBias, m_parallaxBias);
        GLint locParallaxUseNormalA = glGetUniformLocation(program, "uParallaxUseNormalAlpha");
        if (locParallaxUseNormalA >= 0)
            glUniform1i(locParallaxUseNormalA, m_parallaxUseNormalAlpha ? 1 : 0);
        GLint locParallaxInvert = glGetUniformLocation(program, "uParallaxInvertOffset");
        if (locParallaxInvert >= 0)
            glUniform1i(locParallaxInvert, m_parallaxInvertOffset ? 1 : 0);
    }

    const_cast<RenderMaterial&>(material).refreshTextureUsageFlags();

    MaterialBindingInfo bindingInfo = evaluateMaterialUsage(material, hasPrimaryUVs, hasSecondaryUVs);
    
    // Set uHasHeightMap uniform based on whether we have a height map bound
    {
        GLuint program = m_activeShader->id();
        GLint locHasHeightMap = glGetUniformLocation(program, "uHasHeightMap");
        if (locHasHeightMap >= 0)
            glUniform1i(locHasHeightMap, bindingInfo.useHeight ? 1 : 0);
    }
    MaterialRecord& record = getOrCreateMaterialRecord(material);
    record.material = &material;

    if (record.usePBR != material.usePBR || record.unlit != material.unlit ||
        record.alphaMode != material.alphaMode || record.doubleSided != material.doubleSided) {
        record.usePBR = material.usePBR;
        record.unlit = material.unlit;
        record.alphaMode = material.alphaMode;
        record.doubleSided = material.doubleSided;
        record.dirty = true;
    }

    MaterialGPUData gpuData = buildMaterialData(material);
    if (std::memcmp(&gpuData, &record.gpuData, sizeof(MaterialGPUData)) != 0) {
        record.gpuData = gpuData;
        record.dirty = true;
    }

    if (record.dirty)
        uploadMaterialRecord(record);

    bindMaterialResources(record, bindingInfo, hasTangents);
    if (usePBR)
        rebindEnvironmentForPbr(*m_activeShader);
    updateObjectBuffer(model, record, bindingInfo, hasTangents, hasPrimaryUVs, hasSecondaryUVs);
}

LightingSettings& ShadingStage::settings()
{
    return m_settings;
}

const LightingSettings& ShadingStage::settings() const
{
    return m_settings;
}

void ShadingStage::ensureEnvSamplers() const
{
    if (m_envCubeSamplerMip == 0) {
        glGenSamplers(1, &m_envCubeSamplerMip);
        glSamplerParameteri(m_envCubeSamplerMip, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_envCubeSamplerMip, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_envCubeSamplerMip, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_envCubeSamplerMip, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glSamplerParameteri(m_envCubeSamplerMip, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    if (m_envCubeSamplerNoMip == 0) {
        glGenSamplers(1, &m_envCubeSamplerNoMip);
        glSamplerParameteri(m_envCubeSamplerNoMip, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_envCubeSamplerNoMip, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_envCubeSamplerNoMip, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_envCubeSamplerNoMip, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_envCubeSamplerNoMip, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    if (m_env2DSampler == 0) {
        glGenSamplers(1, &m_env2DSampler);
        glSamplerParameteri(m_env2DSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_env2DSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_env2DSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_env2DSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}

void ShadingStage::ensureShadowSampler() const
{
    if (m_shadowSamplerCompare == 0) {
        glGenSamplers(1, &m_shadowSamplerCompare);
        std::cout << "Created shadow compare sampler id " << m_shadowSamplerCompare << '\n';
        glSamplerParameteri(m_shadowSamplerCompare, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_shadowSamplerCompare, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_shadowSamplerCompare, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_shadowSamplerCompare, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glSamplerParameterfv(m_shadowSamplerCompare, GL_TEXTURE_BORDER_COLOR, border);
        glSamplerParameteri(m_shadowSamplerCompare, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m_shadowSamplerCompare, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }

    if (m_shadowSamplerCube == 0) {
        glGenSamplers(1, &m_shadowSamplerCube);
        std::cout << "Created shadow cube sampler id " << m_shadowSamplerCube << '\n';
        glSamplerParameteri(m_shadowSamplerCube, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_shadowSamplerCube, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_shadowSamplerCube, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_shadowSamplerCube, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_shadowSamplerCube, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_shadowSamplerCube, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m_shadowSamplerCube, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }
}


Shader& ShadingStage::shader()
{
    if (m_activeShader)
        return *m_activeShader;
    return m_shader.current();
}

const Shader& ShadingStage::shader() const
{
    if (m_activeShader)
        return *m_activeShader;
    return m_shader.current();
}
