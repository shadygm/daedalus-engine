// SPDX-License-Identifier: MIT

#include "rendering/LightManager.h"

#include "rendering/TextureUnits.h"

#include "mesh/MeshManager.h"
#include "mesh/MeshInstance.h"
#include "terrain/ProceduralFloor.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cassert>

#ifdef NDEBUG
#define GLCHK() ((void)0)
#else
#define GLCHK()                                                                 \
    do {                                                                        \
        GLenum e;                                                               \
        while ((e = glGetError()) != GL_NO_ERROR)                               \
            std::fprintf(stderr, "GLERR 0x%X %s:%d\n", e, __FILE__, __LINE__); \
    } while (0)
#endif

namespace {

constexpr float kMinRange = 0.1f;
constexpr float kMinOuterCone = 1.0f;
constexpr float kMaxOuterCone = 89.0f;
constexpr float kMinInnerCone = 0.1f;
constexpr glm::vec4 kShadowBorderColor(1.0f, 1.0f, 1.0f, 1.0f);
constexpr std::array<float, 12> kShadowDebugTriangle = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     3.0f, -1.0f, 2.0f, 0.0f,
    -1.0f,  3.0f, 0.0f, 2.0f
};
constexpr std::array<glm::vec3, 6> kPointShadowDirections = {
    glm::vec3(1.0f, 0.0f, 0.0f),
    glm::vec3(-1.0f, 0.0f, 0.0f),
    glm::vec3(0.0f, 1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f)
};
constexpr std::array<glm::vec3, 6> kPointShadowUps = {
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, 0.0f, 1.0f),
    glm::vec3(0.0f, 0.0f, -1.0f),
    glm::vec3(0.0f, -1.0f, 0.0f),
    glm::vec3(0.0f, -1.0f, 0.0f)
};

struct ShadowUniform {
    glm::mat4 matrix { 1.0f };
    glm::vec4 params { 0.0f };
};

[[nodiscard]] std::string defaultLabel(LightManager::LightType type)
{
    switch (type) {
    case LightManager::LightType::Point:
        return "Point Light";
    case LightManager::LightType::Spot:
        return "Spot Light";
    }
    return "Light";
}

[[nodiscard]] float radiansClamp(float degrees)
{
    return glm::cos(glm::radians(glm::clamp(degrees, kMinInnerCone, kMaxOuterCone)));
}

[[nodiscard]] float typeValue(LightManager::LightType type)
{
    switch (type) {
    case LightManager::LightType::Point:
        return 0.0f;
    case LightManager::LightType::Spot:
        return 1.0f;
    }
    return 0.0f;
}

[[nodiscard]] glm::mat4 buildOrientationFromForward(const glm::vec3& forward)
{
    const glm::vec3 f = glm::normalize(forward);
    glm::vec3 up = std::abs(f.y) > 0.98f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(up, f));
    glm::vec3 trueUp = glm::normalize(glm::cross(f, right));

    glm::mat4 rotation(1.0f);
    rotation[0] = glm::vec4(right, 0.0f);
    rotation[1] = glm::vec4(trueUp, 0.0f);
    rotation[2] = glm::vec4(-f, 0.0f);
    return rotation;
}

} // namespace

LightManager::LightManager()
{
    ensureDefaultLight();
    ensureShadowLayerMapping();
}

LightManager::~LightManager()
{
    destroyGpuBuffer();
    destroyShadowResources();
    destroyPointShadowResources();
}

void LightManager::drawImGui()
{
    if (ImGui::Begin("Light Manager")) {
        drawImGuiPanel();
    }
    ImGui::End();
}

void LightManager::drawImGuiPanel()
{
    // Add light buttons (consistent header layout)
    if (ImGui::Button("Add Point"))
        addLight(LightType::Point);
    ImGui::SameLine();
    if (ImGui::Button("Add Spot"))
        addLight(LightType::Spot);

    ImGui::Separator();

    // Show lights list like MeshManager
    if (ImGui::BeginListBox("Lights")) {
        for (int i = 0; i < static_cast<int>(m_lights.size()); ++i) {
            const Light& light = m_lights[static_cast<std::size_t>(i)];
            std::string label = light.name.empty()
                ? defaultLabel(light.type) + "##" + std::to_string(i)
                : light.name + "##" + std::to_string(i);
            const bool selected = (i == m_selectedIndex);
            if (ImGui::Selectable(label.c_str(), selected))
                setSelectedIndex(i);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    // Show light details (like "Selected: <name>" section)
    if (Light* light = selectedLight()) {
        ImGui::Separator();
        ImGui::Text("Selected: %s", light->name.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Remove Light"))
            removeLight(m_selectedIndex);

        ImGui::Separator();

        ImGui::Checkbox("Enabled", &light->enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Cast Shadows", &light->castsShadows);
        markDirty();

        ImGui::Separator();
        ImGui::Text("Light Properties:");
        drawLightItem(m_selectedIndex, *light);;
    } else {
        ImGui::TextDisabled("No light selected.");
    }
}


void LightManager::drawLightItem(int index, Light& light)
{
    ImGui::PushID(index);

    char nameBuffer[64] = {};
    if (!light.name.empty())
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", light.name.c_str());
    if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
        if (ImGui::BeginListBox("Lights")) {
            for (int i = 0; i < static_cast<int>(m_lights.size());) {
                Light& light = m_lights[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                std::string label = light.name.empty() ? defaultLabel(light.type) + "##" + std::to_string(i) : light.name + "##" + std::to_string(i);
                const bool isSelected = (i == m_selectedIndex);
                if (ImGui::Selectable(label.c_str(), isSelected)) { m_selectedIndex = i; }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
                ++i;
            }
            ImGui::EndListBox();
        }

        // Per-light quick toggles and actions under the list, for the selected light
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_lights.size())) {
            Light& light = m_lights[static_cast<std::size_t>(m_selectedIndex)];
            bool enabled = light.enabled;
            if (ImGui::Checkbox("Enabled", &enabled)) { light.enabled = enabled; markDirty(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) {
                removeLight(m_selectedIndex);
                if (m_selectedIndex >= static_cast<int>(m_lights.size()))
                    m_selectedIndex = static_cast<int>(m_lights.size()) - 1;
            }
        }
    }

    if (ImGui::ColorEdit3("Color", glm::value_ptr(light.color))) {
        light.color = glm::clamp(light.color, glm::vec3(0.0f), glm::vec3(10.0f));
        markDirty();
    }
    if (ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 10.0f)) {
        light.intensity = std::max(light.intensity, 0.0f);
        markDirty();
    }

    bool useAttenuation = light.useAttenuation;
    if (ImGui::Checkbox("Use attenuation", &useAttenuation)) {
        light.useAttenuation = useAttenuation;
        markDirty();
    }

    switch (light.type) {
    case LightType::Point: {
        if (ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.05f)) {
            markDirty();
        }
        ImGui::BeginDisabled(!light.useAttenuation);
        if (ImGui::DragFloat("Range", &light.range, 0.1f, kMinRange, 200.0f)) {
            light.range = std::max(light.range, kMinRange);
            markDirty();
        }
        ImGui::EndDisabled();
        break;
    }
    case LightType::Spot: {
        if (ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.05f)) {
            markDirty();
        }
        glm::vec3 dir = light.direction;
        if (ImGui::DragFloat3("Direction", glm::value_ptr(dir), 0.01f, -1.0f, 1.0f)) {
            light.direction = sanitizeDirection(dir);
            markDirty();
        }
        ImGui::BeginDisabled(!light.useAttenuation);
        if (ImGui::DragFloat("Range", &light.range, 0.1f, kMinRange, 200.0f)) {
            light.range = std::max(light.range, kMinRange);
            markDirty();
        }
        ImGui::EndDisabled();
        float inner = light.innerConeDegrees;
        float outer = light.outerConeDegrees;
        if (ImGui::SliderFloat("Inner Cone", &inner, 0.0f, outer - 0.1f)) {
            light.innerConeDegrees = std::clamp(inner, 0.0f, outer - 0.1f);
            markDirty();
        }
        if (ImGui::SliderFloat("Outer Cone", &outer, std::max(light.innerConeDegrees + 0.1f, kMinOuterCone), kMaxOuterCone)) {
            light.outerConeDegrees = std::clamp(outer, light.innerConeDegrees + 0.1f, kMaxOuterCone);
            markDirty();
        }
        break;
    }
    }

    ImGui::BeginDisabled(!light.useAttenuation);
    float attenuationValues[3] = {
        light.attenuationConstant,
        light.attenuationLinear,
        light.attenuationQuadratic
    };
    if (ImGui::DragFloat3("Atten {const, lin, quad}", attenuationValues, 0.01f, 0.0f, 10.0f)) {
        light.attenuationConstant = std::max(attenuationValues[0], 0.0f);
        light.attenuationLinear = std::max(attenuationValues[1], 0.0f);
        light.attenuationQuadratic = std::max(attenuationValues[2], 0.0f);
        markDirty();
    }
    ImGui::EndDisabled();

    if (ImGui::DragFloat("Shadow Bias", &light.shadowBias, 0.0001f, 0.0f, 0.05f)) {
        markDirty();
    }
    if (ImGui::DragFloat("Shadow Near", &light.shadowNearPlane, 0.01f, 0.01f, light.shadowFarPlane - 0.1f)) {
        light.shadowNearPlane = std::max(light.shadowNearPlane, 0.01f);
        markDirty();
    }
    if (ImGui::DragFloat("Shadow Far", &light.shadowFarPlane, 0.1f, light.shadowNearPlane + 0.1f, 300.0f)) {
        light.shadowFarPlane = std::max(light.shadowFarPlane, light.shadowNearPlane + 0.1f);
        markDirty();
    }

    ImGui::PopID();
}

void LightManager::updateGpuData()
{
    if (m_gpuDirty)
        rebuildGpuBuffer();
}

void LightManager::rebuildGpuBuffer()
{
    std::vector<GpuLight> gpuLights;
    gpuLights.reserve(m_lights.size());

    for (std::size_t i = 0; i < m_lights.size(); ++i) {
        const Light& light = m_lights[i];
        if (!light.enabled)
            continue;

        GpuLight gpu;
        gpu.positionType = glm::vec4(light.position, typeValue(light.type));
        glm::vec3 dir = sanitizeDirection(light.direction);
        const float range = std::max(light.range, kMinRange);
        switch (light.type) {
        case LightType::Point:
            gpu.directionRange = glm::vec4(0.0f, 0.0f, 0.0f, range);
            break;
        case LightType::Spot:
            gpu.directionRange = glm::vec4(dir, range);
            break;
        }
        gpu.colorIntensity = glm::vec4(light.color * light.intensity, light.intensity);

        float innerCos = radiansClamp(std::min(light.innerConeDegrees, light.outerConeDegrees - 0.1f));
        float outerCos = radiansClamp(light.outerConeDegrees);
        int shadowLayer = (i < m_shadowLayerForLight.size()) ? m_shadowLayerForLight[i] : -1;
        if (shadowLayer < 0)
            shadowLayer = -1;
        gpu.spotShadow = glm::vec4(innerCos, outerCos, static_cast<float>(shadowLayer), light.castsShadows ? 1.0f : 0.0f);
        gpu.shadowParams = glm::vec4(light.shadowBias, light.shadowNearPlane, light.shadowFarPlane, shadowLayer >= 0 ? 1.0f : 0.0f);
        gpu.attenuation = glm::vec4(
            std::max(light.attenuationConstant, 0.0f),
            std::max(light.attenuationLinear, 0.0f),
            std::max(light.attenuationQuadratic, 0.0f),
            light.useAttenuation ? 1.0f : 0.0f);
        gpu.extra = glm::vec4(range, 0.0f, 0.0f, 0.0f);

        gpuLights.push_back(gpu);
    }

    if (m_lightBuffer == 0)
        glGenBuffers(1, &m_lightBuffer);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_lightBuffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(gpuLights.size() * sizeof(GpuLight)),
        gpuLights.empty() ? nullptr : gpuLights.data(),
        GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    m_gpuBinding.lightSSBO = gpuLights.empty() ? 0 : m_lightBuffer;
    m_gpuBinding.lightCount = static_cast<int>(gpuLights.size());

    m_gpuDirty = false;
}

void LightManager::destroyGpuBuffer()
{
    if (m_lightBuffer != 0) {
        glDeleteBuffers(1, &m_lightBuffer);
        m_lightBuffer = 0;
    }
    m_gpuBinding = {};
    m_gpuDirty = true;
}

void LightManager::ensureDefaultLight()
{
    if (!m_lights.empty())
        return;

    Light& light = addLight(LightType::Point);
    light.position = glm::vec3(0.0f, 4.0f, 0.0f);
    light.range = 12.0f;
    light.intensity = 1.0f;
    light.castsShadows = false;
    markDirty();
    ensureShadowLayerMapping();
}

LightManager::Light& LightManager::addLight(LightType type)
{
    Light light;
    light.type = type;
    light.name = defaultLabel(type) + " " + std::to_string(m_nextId++);
    switch (type) {
    case LightType::Point:
        light.position = glm::vec3(0.0f, 4.0f, 0.0f);
        light.range = 12.0f;
        light.intensity = 1.0f;
        light.castsShadows = false;
        break;
    case LightType::Spot:
        light.position = glm::vec3(0.0f, 6.0f, 0.0f);
        light.direction = sanitizeDirection(glm::vec3(-0.2f, -1.0f, -0.3f));
        light.range = 18.0f;
        light.intensity = 1.0f;
        light.innerConeDegrees = 20.0f;
        light.outerConeDegrees = 35.0f;
        light.castsShadows = true;
        break;
    }

    m_lights.push_back(light);
    m_shadowLayerForLight.push_back(-1);
    m_selectedIndex = static_cast<int>(m_lights.size()) - 1;
    markDirty();
    return m_lights.back();
}

void LightManager::removeLight(int index)
{
    if (index < 0 || index >= static_cast<int>(m_lights.size()))
        return;

    m_lights.erase(m_lights.begin() + index);
    if (index >= 0 && index < static_cast<int>(m_shadowLayerForLight.size()))
        m_shadowLayerForLight.erase(m_shadowLayerForLight.begin() + index);

    if (m_selectedIndex >= static_cast<int>(m_lights.size()))
        m_selectedIndex = static_cast<int>(m_lights.size()) - 1;

    // Allow exactly 0 lights in the system
    if (m_lights.empty())
        m_selectedIndex = -1;

    markDirty();

    ensureShadowLayerMapping();
}

void LightManager::setSelectedIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(m_lights.size())) {
        m_selectedIndex = -1;
        return;
    }
    m_selectedIndex = index;
}

LightManager::Light* LightManager::selectedLight()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_lights.size()))
        return nullptr;
    return &m_lights[static_cast<std::size_t>(m_selectedIndex)];
}

LightManager::Light* LightManager::findLightByName(const std::string& name)
{
    auto it = std::find_if(m_lights.begin(), m_lights.end(), [&name](const Light& light) {
        return light.name == name;
    });
    if (it == m_lights.end())
        return nullptr;
    return &(*it);
}

const LightManager::Light* LightManager::findLightByName(const std::string& name) const
{
    auto it = std::find_if(m_lights.begin(), m_lights.end(), [&name](const Light& light) {
        return light.name == name;
    });
    if (it == m_lights.end())
        return nullptr;
    return &(*it);
}

LightManager::Light& LightManager::ensureLight(const std::string& name, LightType type)
{
    if (Light* existing = findLightByName(name))
        return *existing;

    Light& light = addLight(type);
    light.name = name;
    markDirty();
    return light;
}

const LightManager::Light* LightManager::selectedLight() const
{
    if (m_selectedIndex < 0 || m_selectedIndex >= static_cast<int>(m_lights.size()))
        return nullptr;
    return &m_lights[static_cast<std::size_t>(m_selectedIndex)];
}

void LightManager::markDirty()
{
    m_gpuDirty = true;
    m_shadowResourcesDirty = true;
}

glm::vec3 LightManager::sanitizeDirection(const glm::vec3& dir)
{
    glm::vec3 result = dir;
    if (glm::length(result) < 1e-4f)
        result = glm::vec3(0.0f, -1.0f, 0.0f);
    return glm::normalize(result);
}

void LightManager::ensureShadowLayerMapping()
{
    if (m_shadowLayerForLight.size() != m_lights.size()) {
        std::size_t previousSize = m_shadowLayerForLight.size();
        m_shadowLayerForLight.resize(m_lights.size(), -1);
        if (m_shadowLayerForLight.size() != previousSize)
            m_shadowResourcesDirty = true;
    }
}

void LightManager::ensureShadowShader()
{
    if (m_shadowShaderReady)
        return;

    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shadow_vert.glsl");
    builder.addStage(GL_GEOMETRY_SHADER, RESOURCE_ROOT "shaders/shadow_geom.glsl");
    builder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shadow_frag.glsl");
    m_shadowShader = builder.build();
    m_shadowShaderReady = true;
}

void LightManager::ensurePointShadowInstancedShader()
{
    if (m_pointShadowInstancedShaderReady)
        return;

    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shadow_point_instanced.vert");
    builder.addStage(GL_GEOMETRY_SHADER, RESOURCE_ROOT "shaders/shadow_point_instanced.geom");
    m_pointShadowInstancedShader = builder.build();
    m_pointShadowModelLocation = m_pointShadowInstancedShader.getUniformLocation("uModel");

    if (m_pointShadowViewProjUBO == 0)
        glGenBuffers(1, &m_pointShadowViewProjUBO);

    m_pointShadowInstancedShaderReady = true;
}

bool LightManager::isShadowCasterSupported(const Light& light) const
{
    switch (light.type) {
    case LightType::Spot:
        return true;
    case LightType::Point:
        return true;
    }
    return false;
}

LightManager::ShadowEntry LightManager::buildShadowEntry(int lightIndex, const Light& light, const glm::vec3& cameraPosition) const
{
    ShadowEntry entry;
    entry.lightIndex = lightIndex;
    entry.type = light.type;
    entry.lightPosition = light.position;
    entry.nearPlane = std::max(light.shadowNearPlane, 0.01f);
    entry.farPlane = std::max(light.shadowFarPlane, entry.nearPlane + 0.1f);

    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(light.direction.y) > 0.98f)
        up = glm::vec3(0.0f, 0.0f, 1.0f);

    switch (light.type) {
    case LightType::Spot: {
        glm::vec3 direction = sanitizeDirection(light.direction);
        entry.viewMatrix = glm::lookAt(light.position, light.position + direction, up);

        float outer = glm::clamp(light.outerConeDegrees, kMinOuterCone, kMaxOuterCone);
        float fov = glm::radians(glm::clamp(outer * 2.0f, kMinOuterCone, 170.0f));
        float farPlane = std::max(light.shadowFarPlane, light.range);
        entry.projectionMatrix = glm::perspective(fov, 1.0f, entry.nearPlane, farPlane);
        entry.farPlane = farPlane;
        break;
    }
    case LightType::Point:
        break;
    }

    return entry;
}

void LightManager::ensureShadowResources(std::size_t casterCount)
{
    casterCount = std::min<std::size_t>(casterCount, kMaxShadowLights);
    if (casterCount == 0) {
        m_gpuBinding.directionalShadowTexture = 0;
        return;
    }

    if (m_shadowMapArray == 0 || m_shadowArrayLayers < casterCount) {
        if (m_shadowMapArray != 0)
            glDeleteTextures(1, &m_shadowMapArray);

        glGenTextures(1, &m_shadowMapArray);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadowMapArray);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,
            0,
            GL_DEPTH_COMPONENT24,
            kShadowMapResolution,
            kShadowMapResolution,
            static_cast<GLsizei>(casterCount),
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

        m_shadowArrayLayers = casterCount;
        m_shadowResourcesDirty = true;
    }

    if (m_shadowFramebuffer == 0)
        glGenFramebuffers(1, &m_shadowFramebuffer);

    if (m_shadowMatricesBuffer == 0)
        glGenBuffers(1, &m_shadowMatricesBuffer);

    m_shadowMatrices.resize(casterCount);
    m_shadowParams.resize(casterCount);
}

void LightManager::ensurePointShadowResources(std::size_t casterCount)
{
    casterCount = std::min<std::size_t>(casterCount, kMaxShadowLights);

    if (m_shadowFramebuffer == 0)
        glGenFramebuffers(1, &m_shadowFramebuffer);

    if (m_pointShadowCubemaps.size() < casterCount) {
        std::size_t previousSize = m_pointShadowCubemaps.size();
        m_pointShadowCubemaps.resize(casterCount, 0);
        for (std::size_t i = previousSize; i < casterCount; ++i) {
            glGenTextures(1, &m_pointShadowCubemaps[i]);
            glBindTexture(GL_TEXTURE_CUBE_MAP, m_pointShadowCubemaps[i]);
            for (int face = 0; face < 6; ++face) {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                    0,
                    GL_DEPTH_COMPONENT32F,
                    kShadowMapResolution,
                    kShadowMapResolution,
                    0,
                    GL_DEPTH_COMPONENT,
                    GL_FLOAT,
                    nullptr);
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        }
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        m_pointShadowResourcesDirty = true;
    }

    if (m_pointShadowSampler == 0) {
        glGenSamplers(1, &m_pointShadowSampler);
        glSamplerParameteri(m_pointShadowSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_pointShadowSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_pointShadowSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_pointShadowSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_pointShadowSampler, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(m_pointShadowSampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(m_pointShadowSampler, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    }

    ensurePointShadowFallbackTexture();
}

void LightManager::ensureShadowFallbackTexture()
{
    if (m_shadowDummyTexture != 0)
        return;

    glGenTextures(1, &m_shadowDummyTexture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_shadowDummyTexture);
    const float depthOne = 1.0f;
    glTexImage3D(GL_TEXTURE_2D_ARRAY,
        0,
        GL_DEPTH_COMPONENT24,
        1,
        1,
        1,
        0,
        GL_DEPTH_COMPONENT,
        GL_FLOAT,
        &depthOne);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    const float borderColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void LightManager::ensurePointShadowFallbackTexture()
{
    if (m_pointShadowDummyTexture != 0)
        return;

    glGenTextures(1, &m_pointShadowDummyTexture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_pointShadowDummyTexture);
    const float depthOne = 1.0f;
    for (int face = 0; face < 6; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            0,
            GL_DEPTH_COMPONENT32F,
            1,
            1,
            0,
            GL_DEPTH_COMPONENT,
            GL_FLOAT,
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

void LightManager::destroyShadowResources()
{
    if (m_shadowFramebuffer != 0) {
        glDeleteFramebuffers(1, &m_shadowFramebuffer);
        m_shadowFramebuffer = 0;
    }
    if (m_shadowMapArray != 0) {
        glDeleteTextures(1, &m_shadowMapArray);
        m_shadowMapArray = 0;
    }
    if (m_shadowMatricesBuffer != 0) {
        glDeleteBuffers(1, &m_shadowMatricesBuffer);
        m_shadowMatricesBuffer = 0;
    }
    if (m_shadowDummyTexture != 0) {
        glDeleteTextures(1, &m_shadowDummyTexture);
        m_shadowDummyTexture = 0;
    }
    m_gpuBinding.directionalShadowFallback = 0;
    m_shadowArrayLayers = 0;
    m_shadowMatrices.clear();
    m_shadowParams.clear();
    m_shadowDebugLayers.clear();
    m_shadowDebugDirty = true;
    destroyShadowDebugResources();
}

void LightManager::destroyPointShadowResources()
{
    if (!m_pointShadowCubemaps.empty()) {
        glDeleteTextures(static_cast<GLsizei>(m_pointShadowCubemaps.size()), m_pointShadowCubemaps.data());
        m_pointShadowCubemaps.clear();
    }
    if (m_pointShadowSampler != 0) {
        glDeleteSamplers(1, &m_pointShadowSampler);
        m_pointShadowSampler = 0;
    }
    if (m_pointShadowDummyTexture != 0) {
        glDeleteTextures(1, &m_pointShadowDummyTexture);
        m_pointShadowDummyTexture = 0;
    }
    if (m_pointShadowViewProjUBO != 0) {
        glDeleteBuffers(1, &m_pointShadowViewProjUBO);
        m_pointShadowViewProjUBO = 0;
    }
    m_pointShadowInstancedShader = Shader();
    m_pointShadowInstancedShaderReady = false;
    m_pointShadowModelLocation = -1;
    m_pointShadowEntries.clear();
    m_pointShadowResourcesDirty = true;
    m_gpuBinding.pointShadowCount = 0;
    m_gpuBinding.pointShadowFallback = 0;
    m_gpuBinding.pointShadowTextures.fill(0);
}

void LightManager::ensureShadowDebugShader()
{
    if (m_shadowDebugShaderReady)
        return;

    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/shadow_debug.vert");
    builder.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/shadow_debug.frag");
    m_shadowDebugShader = builder.build();
    m_shadowDebugShaderReady = true;
}

void LightManager::ensureShadowDebugResources()
{
    ensureShadowDebugShader();

    if (m_shadowDebugTexture == 0) {
        glGenTextures(1, &m_shadowDebugTexture);
    }
    glBindTexture(GL_TEXTURE_2D, m_shadowDebugTexture);
    glTexImage2D(GL_TEXTURE_2D,
        0,
        GL_RGBA16F,
        m_shadowDebugResolution.x,
        m_shadowDebugResolution.y,
        0,
        GL_RGBA,
        GL_FLOAT,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (m_shadowDebugFramebuffer == 0)
        glGenFramebuffers(1, &m_shadowDebugFramebuffer);

    if (m_shadowDebugSampler == 0) {
        glGenSamplers(1, &m_shadowDebugSampler);
        glSamplerParameteri(m_shadowDebugSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glSamplerParameteri(m_shadowDebugSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glSamplerParameteri(m_shadowDebugSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_shadowDebugSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        glSamplerParameteri(m_shadowDebugSampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glSamplerParameterfv(m_shadowDebugSampler, GL_TEXTURE_BORDER_COLOR, glm::value_ptr(kShadowBorderColor));
    }

    if (m_shadowDebugVao == 0) {
        glGenVertexArrays(1, &m_shadowDebugVao);
        glGenBuffers(1, &m_shadowDebugVbo);
        glBindVertexArray(m_shadowDebugVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_shadowDebugVbo);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(kShadowDebugTriangle.size() * sizeof(float)),
            kShadowDebugTriangle.data(),
            GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
        glBindVertexArray(0);
    }
}

void LightManager::destroyShadowDebugResources()
{
    if (m_shadowDebugFramebuffer != 0) {
        glDeleteFramebuffers(1, &m_shadowDebugFramebuffer);
        m_shadowDebugFramebuffer = 0;
    }
    if (m_shadowDebugTexture != 0) {
        glDeleteTextures(1, &m_shadowDebugTexture);
        m_shadowDebugTexture = 0;
    }
    if (m_shadowDebugVao != 0) {
        glDeleteVertexArrays(1, &m_shadowDebugVao);
        m_shadowDebugVao = 0;
    }
    if (m_shadowDebugVbo != 0) {
        glDeleteBuffers(1, &m_shadowDebugVbo);
        m_shadowDebugVbo = 0;
    }
    if (m_shadowDebugSampler != 0) {
        glDeleteSamplers(1, &m_shadowDebugSampler);
        m_shadowDebugSampler = 0;
    }
    m_shadowDebugShader = Shader();
    m_shadowDebugShaderReady = false;
}

void LightManager::updateShadowDebugPreview()
{
    if (m_shadowDebugLayers.empty())
        return;
    if (m_shadowMapArray == 0)
        return;

    ensureShadowDebugResources();

    if (m_shadowDebugSelectedLayer < 0 || m_shadowDebugSelectedLayer >= static_cast<int>(m_shadowDebugLayers.size()))
        return;

    const ShadowDebugLayer& layer = m_shadowDebugLayers[static_cast<std::size_t>(m_shadowDebugSelectedLayer)];

    GLint previousViewport[4];
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    GLint prevDrawBuffer = 0;
    GLint prevReadBuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_BUFFER, &prevDrawBuffer);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowDebugFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_shadowDebugTexture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, m_shadowDebugResolution.x, m_shadowDebugResolution.y);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    // glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    m_shadowDebugShader.bind();
    const GLint locShadow = m_shadowDebugShader.getUniformLocation("uShadowMap");
    const GLint locLayer = m_shadowDebugShader.getUniformLocation("uLayer");
    const GLint locNear = m_shadowDebugShader.getUniformLocation("uNearPlane");
    const GLint locFar = m_shadowDebugShader.getUniformLocation("uFarPlane");
    const GLint locLinearize = m_shadowDebugShader.getUniformLocation("uLinearize");
    const GLint locContrast = m_shadowDebugShader.getUniformLocation("uContrast");

    glUniform1i(locShadow, 0);
    glUniform1i(locLayer, layer.layerIndex);
    glUniform1f(locNear, layer.nearPlane);
    glUniform1f(locFar, layer.farPlane);
    glUniform1i(locLinearize, m_shadowDebugLinearize ? 1 : 0);
    glUniform1f(locContrast, m_shadowDebugContrast);

    TextureUnits::assertNotEnvUnit(0);
    glBindTextureUnit(0, m_shadowMapArray);
    glBindSampler(0, m_shadowDebugSampler);

    glBindVertexArray(m_shadowDebugVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glBindSampler(0, 0);
    TextureUnits::assertNotEnvUnit(0);
    glBindTextureUnit(0, 0);

    if (blendEnabled)
        glEnable(GL_BLEND);
    if (depthTestEnabled)
        glEnable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));

    if (prevDrawFbo == 0 && prevDrawBuffer == GL_NONE)
        glDrawBuffer(GL_BACK);
    else
        glDrawBuffer(prevDrawBuffer);

    if (prevReadFbo == 0 && prevReadBuffer == GL_NONE)
        glReadBuffer(GL_BACK);
    else
        glReadBuffer(prevReadBuffer);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    m_shadowDebugDirty = false;
}

void LightManager::drawShadowDebugPanel()
{
    ImGui::TextWrapped("Inspect the rendered depth maps for each shadow-casting light. Select a layer to preview its shadow map and tweak the visualization to diagnose bias or range issues.");
    ImGui::Spacing();

    if (m_shadowDebugLayers.empty()) {
        ImGui::TextDisabled("No shadow maps rendered yet.");
        ImGui::TextWrapped("Enable shadows on a spot light and render the scene to populate this view.");
        return;
    }

    if (m_shadowDebugSelectedLayer < 0 || m_shadowDebugSelectedLayer >= static_cast<int>(m_shadowDebugLayers.size()))
        m_shadowDebugSelectedLayer = 0;

    std::vector<std::string> labels;
    labels.reserve(m_shadowDebugLayers.size());
    for (const ShadowDebugLayer& layer : m_shadowDebugLayers) {
        std::string name;
        if (layer.lightIndex >= 0 && layer.lightIndex < static_cast<int>(m_lights.size())) {
            const Light& light = m_lights[static_cast<std::size_t>(layer.lightIndex)];
            name = light.name.empty() ? defaultLabel(light.type) : light.name;
        } else {
            name = "Layer " + std::to_string(layer.layerIndex);
        }
        name += " (";
        name += (layer.type == LightType::Spot) ? "Spot" : "Point";
        name += ")";
        labels.push_back(std::move(name));
    }

    auto comboGetter = [](void* data, int idx, const char** out_text) -> bool {
        const auto* vec = static_cast<const std::vector<std::string>*>(data);
        if (idx < 0 || idx >= static_cast<int>(vec->size()))
            return false;
        *out_text = (*vec)[static_cast<std::size_t>(idx)].c_str();
        return true;
    };

    int selection = m_shadowDebugSelectedLayer;
    if (ImGui::Combo("Shadow Caster", &selection, comboGetter, &labels, static_cast<int>(labels.size()))) {
        m_shadowDebugSelectedLayer = selection;
        m_shadowDebugDirty = true;
    }

    const ShadowDebugLayer& layer = m_shadowDebugLayers[static_cast<std::size_t>(m_shadowDebugSelectedLayer)];
    const Light* light = (layer.lightIndex >= 0 && layer.lightIndex < static_cast<int>(m_lights.size()))
        ? &m_lights[static_cast<std::size_t>(layer.lightIndex)]
        : nullptr;

    ImGui::Separator();
    ImGui::Text("Light: %s", light ? light->name.c_str() : "Unassigned");
    ImGui::Text("Type: %s", layer.type == LightType::Spot ? "Spot" : "Point");
    ImGui::Text("Near/Far: %.2f / %.2f", static_cast<double>(layer.nearPlane), static_cast<double>(layer.farPlane));
    ImGui::Text("Bias: %.5f", static_cast<double>(layer.bias));
    ImGui::Text("Resolution: %d x %d", layer.resolution, layer.resolution);

    bool linearize = m_shadowDebugLinearize;
    if (ImGui::Checkbox("Linearize depth", &linearize)) {
        m_shadowDebugLinearize = linearize;
        m_shadowDebugDirty = true;
    }

    float contrast = m_shadowDebugContrast;
    if (ImGui::SliderFloat("Contrast", &contrast, 0.5f, 3.0f, "%.2f")) {
        m_shadowDebugContrast = contrast;
        m_shadowDebugDirty = true;
    }

    ImGui::Separator();
    ImGui::TextWrapped("Tip: Depth values close to 0.0 indicate surfaces very close to the light. Linearized mode remaps the depth range to world-space distance so you can spot clipping or incorrect near/far planes.");

    if (m_shadowDebugDirty)
        updateShadowDebugPreview();

    if (m_shadowDebugTexture != 0) {
        float availableWidth = ImGui::GetContentRegionAvail().x;
        float aspect = static_cast<float>(m_shadowDebugResolution.y) / static_cast<float>(m_shadowDebugResolution.x);
        ImVec2 size(availableWidth, availableWidth * aspect);
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(m_shadowDebugTexture)), size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    } else {
        ImGui::TextDisabled("Preview not available.");
    }
}

void LightManager::bindShadowFramebuffer(const ShadowEntry& entry)
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadowMapArray, 0, entry.layerIndex);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
}

void LightManager::bindLayeredShadowFramebuffer()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_shadowMapArray, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
}

void LightManager::renderShadowGeometry(bool layeredPass,
    MeshManager& meshManager,
    ProceduralFloor* floorPtr,
    bool pointPass,
    const glm::mat4* lightViewProj,
    const glm::vec3& lightPos,
    float nearPlane,
    float farPlane,
    int shadowLayerCount)
{
    ensureShadowShader();
    m_shadowShader.bind();

    const GLint locModel = m_shadowShader.getUniformLocation("modelMatrix");
    const GLint locIsPoint = m_shadowShader.getUniformLocation("uIsPointLight");
    const GLint locLightPos = m_shadowShader.getUniformLocation("uPointLightPosition");
    const GLint locNear = m_shadowShader.getUniformLocation("uPointLightNear");
    const GLint locFar = m_shadowShader.getUniformLocation("uPointLightFar");
    const GLint locLayered = m_shadowShader.getUniformLocation("uLayeredPass");
    const GLint locLayerCount = m_shadowShader.getUniformLocation("uShadowLayerCount");
    const GLint locPointViewProj = m_shadowShader.getUniformLocation("uPointLightViewProj");

    if (locLayered >= 0)
        glUniform1i(locLayered, layeredPass ? 1 : 0);
    if (!pointPass && locLayerCount >= 0)
        glUniform1i(locLayerCount, shadowLayerCount);
    else if (pointPass && locLayerCount >= 0)
        glUniform1i(locLayerCount, 0);

    if (locIsPoint >= 0)
        glUniform1i(locIsPoint, pointPass ? 1 : 0);
    if (pointPass) {
        if (lightViewProj && locPointViewProj >= 0)
            glUniformMatrix4fv(locPointViewProj, 1, GL_FALSE, glm::value_ptr(*lightViewProj));
        if (locLightPos >= 0)
            glUniform3fv(locLightPos, 1, glm::value_ptr(lightPos));
        if (locNear >= 0)
            glUniform1f(locNear, nearPlane);
        if (locFar >= 0)
            glUniform1f(locFar, farPlane);
    }

    const bool bindShadowMatrices = (!pointPass && shadowLayerCount > 0 && m_shadowMatricesBuffer != 0);
    if (bindShadowMatrices)
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_shadowMatricesBuffer);

    auto& instances = meshManager.instances();
    for (MeshInstance& instance : instances) {
        const glm::mat4& instanceTransform = instance.transform();
        for (MeshDrawItem& item : instance.drawItems()) {
            const glm::mat4 model = instanceTransform * item.nodeTransform;
            if (locModel >= 0)
                glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(model));
            item.geometry.draw(m_shadowShader);
        }
    }

    if (bindShadowMatrices)
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);

    (void)floorPtr;
}

void LightManager::uploadShadowMatrices(const ShadowEntry* entries, int layerCount)
{
    if (layerCount <= 0 || entries == nullptr || m_shadowMatricesBuffer == 0)
        return;

    const int count = std::min(layerCount, kMaxShadowLights);
    std::vector<ShadowUniform> uniformData(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const ShadowEntry& entry = entries[i];
        uniformData[static_cast<std::size_t>(i)].matrix = entry.projectionMatrix * entry.viewMatrix;
        const float invResolution = 1.0f / static_cast<float>(kShadowMapResolution);
        const float typeValue = (entry.type == LightType::Spot) ? 1.0f : 0.0f;
        uniformData[static_cast<std::size_t>(i)].params = glm::vec4(entry.nearPlane, entry.farPlane, invResolution, typeValue);
    }

    glBindBuffer(GL_UNIFORM_BUFFER, m_shadowMatricesBuffer);
    glBufferData(GL_UNIFORM_BUFFER,
        static_cast<GLsizeiptr>(uniformData.size() * sizeof(ShadowUniform)),
        uniformData.data(),
        GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void LightManager::renderPointShadowInstanced(const PointShadowEntry& entry,
    MeshManager& meshManager,
    ProceduralFloor* floorPtr)
{
    (void)floorPtr;

    ensurePointShadowInstancedShader();
    m_pointShadowInstancedShader.bind();

    const float nearPlane = entry.nearPlane;
    const float farPlane = entry.farPlane;
    const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

    std::array<glm::mat4, 6> vpMatrices {};
    for (int face = 0; face < 6; ++face) {
        const glm::mat4 view = glm::lookAt(entry.lightPosition,
            entry.lightPosition + kPointShadowDirections[static_cast<std::size_t>(face)],
            kPointShadowUps[static_cast<std::size_t>(face)]);
        vpMatrices[static_cast<std::size_t>(face)] = projection * view;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, entry.cubemap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    glViewport(0, 0, entry.resolution, entry.resolution);
    glClear(GL_DEPTH_BUFFER_BIT);

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    if (m_pointShadowViewProjUBO == 0)
        glGenBuffers(1, &m_pointShadowViewProjUBO);

    glBindBuffer(GL_UNIFORM_BUFFER, m_pointShadowViewProjUBO);
    glBufferData(GL_UNIFORM_BUFFER,
        static_cast<GLsizeiptr>(vpMatrices.size() * sizeof(glm::mat4)),
        vpMatrices.data(),
        GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    glBindBufferBase(GL_UNIFORM_BUFFER, 4, m_pointShadowViewProjUBO);

    auto& instances = meshManager.instances();
    for (MeshInstance& instance : instances) {
        const glm::mat4& instanceTransform = instance.transform();
        for (MeshDrawItem& item : instance.drawItems()) {
            const glm::mat4 model = instanceTransform * item.nodeTransform;
            if (m_pointShadowModelLocation >= 0)
                glUniformMatrix4fv(m_pointShadowModelLocation, 1, GL_FALSE, glm::value_ptr(model));
            item.geometry.drawInstanced(6);
        }
    }

    glBindBufferBase(GL_UNIFORM_BUFFER, 4, 0);

    GLCHK();
}

void LightManager::renderPointShadowFaces(GLuint cubemap,
    int resolution,
    const glm::vec3& lightPos,
    float nearPlane,
    float farPlane,
    float slopeBias,
    float constantBias,
    MeshManager& meshManager,
    ProceduralFloor* floorPtr)
{
    (void)slopeBias;
    (void)constantBias;

    const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    for (int face = 0; face < 6; ++face) {
        const GLenum attachmentFace = static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, attachmentFace, cubemap, 0);
        glViewport(0, 0, resolution, resolution);
        glClear(GL_DEPTH_BUFFER_BIT);

        const glm::mat4 view = glm::lookAt(lightPos,
            lightPos + kPointShadowDirections[static_cast<std::size_t>(face)],
            kPointShadowUps[static_cast<std::size_t>(face)]);
        const glm::mat4 lightViewProj = projection * view;
        renderShadowGeometry(false,
            meshManager,
            floorPtr,
            true,
            &lightViewProj,
            lightPos,
            nearPlane,
            farPlane);
    }

    GLCHK();
}

void LightManager::uploadShadowData(const std::vector<ShadowEntry>& entries, const std::vector<PointShadowEntry>& pointEntries)
{
    ensureShadowFallbackTexture();
    ensurePointShadowFallbackTexture();

    if (entries.empty()) {
        m_gpuBinding.shadowMatricesUBO = 0;
        m_gpuBinding.directionalShadowTexture = 0;
        m_gpuBinding.directionalShadowFallback = m_shadowDummyTexture;
        m_gpuBinding.directionalLightCount = 0;
    } else {
        uploadShadowMatrices(entries.data(), static_cast<int>(entries.size()));

        m_gpuBinding.shadowMatricesUBO = m_shadowMatricesBuffer;
        m_gpuBinding.directionalShadowTexture = m_shadowMapArray;
        m_gpuBinding.directionalShadowFallback = m_shadowDummyTexture;
        m_gpuBinding.directionalLightCount = static_cast<int>(entries.size());
        m_shadowResourcesDirty = false;
    }

    m_gpuBinding.pointShadowTextures.fill(0);
    m_gpuBinding.pointShadowCount = static_cast<int>(pointEntries.size());
    for (std::size_t i = 0; i < pointEntries.size() && i < m_gpuBinding.pointShadowTextures.size(); ++i) {
        m_gpuBinding.pointShadowTextures[i] = pointEntries[i].cubemap;
    }
    m_gpuBinding.pointShadowFallback = m_pointShadowDummyTexture;
}

void LightManager::renderShadowMaps(const glm::mat4& /*cameraView*/,
    const glm::mat4& /*cameraProjection*/,
    const glm::vec3& cameraPosition,
    MeshManager& meshManager,
    ProceduralFloor* floorPtr)
{
    ensureShadowLayerMapping();

    std::fill(m_shadowLayerForLight.begin(), m_shadowLayerForLight.end(), -1);

    std::vector<int> spotIndices;
    std::vector<int> pointIndices;
    spotIndices.reserve(kMaxShadowLights);
    pointIndices.reserve(kMaxShadowLights);

    for (std::size_t i = 0; i < m_lights.size(); ++i) {
        const Light& light = m_lights[i];
        if (!light.enabled || !light.castsShadows)
            continue;

        switch (light.type) {
        case LightType::Spot:
            if (spotIndices.size() < kMaxShadowLights)
                spotIndices.push_back(static_cast<int>(i));
            break;
        case LightType::Point:
            if (pointIndices.size() < kMaxShadowLights)
                pointIndices.push_back(static_cast<int>(i));
            break;
        }
    }

    std::vector<ShadowEntry> entries;
    entries.reserve(spotIndices.size());
    for (int index : spotIndices) {
        entries.push_back(buildShadowEntry(index, m_lights[static_cast<std::size_t>(index)], cameraPosition));
    }

    ensureShadowResources(entries.size());
    ensurePointShadowResources(pointIndices.size());

    m_pointShadowEntries.clear();
    m_pointShadowEntries.reserve(pointIndices.size());
    for (std::size_t i = 0; i < pointIndices.size(); ++i) {
        const int lightIndex = pointIndices[i];
        const Light& light = m_lights[static_cast<std::size_t>(lightIndex)];

        PointShadowEntry entry;
        entry.lightIndex = lightIndex;
        entry.cubemap = m_pointShadowCubemaps[i];
        entry.resolution = kShadowMapResolution;
        entry.lightPosition = light.position;
        entry.nearPlane = std::max(light.shadowNearPlane, 0.01f);
        entry.farPlane = std::max(light.shadowFarPlane, entry.nearPlane + 0.1f);
        entry.slopeBias = 0.0f;
        entry.constantBias = light.shadowBias;
        m_pointShadowEntries.push_back(entry);

        if (lightIndex >= 0 && lightIndex < static_cast<int>(m_shadowLayerForLight.size()))
            m_shadowLayerForLight[static_cast<std::size_t>(lightIndex)] = static_cast<int>(i);
    }

    if (entries.empty() && m_pointShadowEntries.empty()) {
        uploadShadowData(entries, m_pointShadowEntries);
        m_shadowDebugLayers.clear();
        m_shadowDebugDirty = true;
        return;
    }

    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    GLint prevDrawBuffer = 0;
    GLint prevReadBuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_BUFFER, &prevDrawBuffer);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);

    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);

    GLCHK();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    const int shadowLayerCount = static_cast<int>(entries.size());

    if (!entries.empty()) {
        for (std::size_t layer = 0; layer < entries.size(); ++layer) {
            ShadowEntry& entry = entries[layer];
            entry.layerIndex = static_cast<int>(layer);
            if (entry.lightIndex >= 0 && entry.lightIndex < static_cast<int>(m_shadowLayerForLight.size()))
                m_shadowLayerForLight[static_cast<std::size_t>(entry.lightIndex)] = entry.layerIndex;
        }

        uploadShadowMatrices(entries.data(), shadowLayerCount);
        GLCHK();

    assert(m_shadowArrayLayers >= static_cast<std::size_t>(shadowLayerCount));

        bool layeredReady = false;
        if (m_useLayeredShadows) {
            bindLayeredShadowFramebuffer();
            GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
                std::fprintf(stderr,
                    "Shadow FBO incomplete: 0x%X (layers=%d)\n",
                    fbStatus,
                    shadowLayerCount);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
            } else {
                layeredReady = true;
                glViewport(0, 0, kShadowMapResolution, kShadowMapResolution);
                glClear(GL_DEPTH_BUFFER_BIT);
                GLCHK();
                renderShadowGeometry(true,
                    meshManager,
                    floorPtr,
                    false,
                    nullptr,
                    glm::vec3(0.0f),
                    0.1f,
                    100.0f,
                    shadowLayerCount);
                GLCHK();
            }
        }

        if (!layeredReady) {
            for (std::size_t layer = 0; layer < entries.size(); ++layer) {
                ShadowEntry& entry = entries[layer];
                uploadShadowMatrices(&entry, 1);
                bindShadowFramebuffer(entry);
                glViewport(0, 0, kShadowMapResolution, kShadowMapResolution);
                glClear(GL_DEPTH_BUFFER_BIT);
                GLCHK();
                renderShadowGeometry(false,
                    meshManager,
                    floorPtr,
                    false,
                    nullptr,
                    glm::vec3(0.0f),
                    0.1f,
                    100.0f,
                    1);
                GLCHK();
            }
        }
    }

    for (const PointShadowEntry& entry : m_pointShadowEntries) {
        if (m_usePointInstancedShadows) {
            renderPointShadowInstanced(entry, meshManager, floorPtr);
        } else {
            renderPointShadowFaces(entry.cubemap,
                entry.resolution,
                entry.lightPosition,
                entry.nearPlane,
                entry.farPlane,
                entry.slopeBias,
                entry.constantBias,
                meshManager,
                floorPtr);
        }
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
    if (prevDrawFbo == 0 && prevDrawBuffer == GL_NONE)
        glDrawBuffer(GL_BACK);
    else
        glDrawBuffer(prevDrawBuffer);
    if (prevReadFbo == 0 && prevReadBuffer == GL_NONE)
        glReadBuffer(GL_BACK);
    else
        glReadBuffer(prevReadBuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glCullFace(GL_BACK);

    GLCHK();

    uploadShadowData(entries, m_pointShadowEntries);

    m_shadowDebugLayers.clear();
    m_shadowDebugLayers.reserve(entries.size());
    for (const ShadowEntry& entry : entries) {
        ShadowDebugLayer layerInfo;
        layerInfo.layerIndex = entry.layerIndex;
        layerInfo.lightIndex = entry.lightIndex;
        layerInfo.type = entry.type;
        layerInfo.nearPlane = entry.nearPlane;
        layerInfo.farPlane = entry.farPlane;
        layerInfo.resolution = kShadowMapResolution;
        if (entry.lightIndex >= 0 && entry.lightIndex < static_cast<int>(m_lights.size())) {
            const Light& srcLight = m_lights[static_cast<std::size_t>(entry.lightIndex)];
            layerInfo.bias = srcLight.shadowBias;
        }
        m_shadowDebugLayers.push_back(layerInfo);
    }

    if (m_shadowDebugSelectedLayer >= static_cast<int>(m_shadowDebugLayers.size()))
        m_shadowDebugSelectedLayer = static_cast<int>(m_shadowDebugLayers.empty() ? 0 : m_shadowDebugLayers.size() - 1);
    if (m_shadowDebugSelectedLayer < 0 && !m_shadowDebugLayers.empty())
        m_shadowDebugSelectedLayer = 0;
    m_shadowDebugDirty = true;
    m_gpuDirty = true;
}
