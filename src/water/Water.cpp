#include "water/Water.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui/imgui.h>
#include <stb/stb_image.h>
DISABLE_WARNINGS_POP()

#include <vector>
#include <iostream>
#include <cmath>

namespace {
constexpr float PI = 3.14159265358979323846f;
}

WaterRenderer::WaterRenderer()
{
    // Default distinct wave directions
    m_settings.waves[0].directionDeg = 0.0f;
    m_settings.waves[0].amplitude = 0.35f;
    m_settings.waves[0].wavelength = 8.0f;
    m_settings.waves[0].steepness = 0.6f;
    m_settings.waves[0].speed = 1.0f;

    m_settings.waves[1].directionDeg = 45.0f;
    m_settings.waves[1].amplitude = 0.25f;
    m_settings.waves[1].wavelength = 5.0f;
    m_settings.waves[1].steepness = 0.5f;
    m_settings.waves[1].speed = 1.2f;

    m_settings.waves[2].directionDeg = -30.0f;
    m_settings.waves[2].amplitude = 0.18f;
    m_settings.waves[2].wavelength = 3.5f;
    m_settings.waves[2].steepness = 0.55f;
    m_settings.waves[2].speed = 1.6f;

    m_settings.waves[3].directionDeg = 90.0f;
    m_settings.waves[3].amplitude = 0.12f;
    m_settings.waves[3].wavelength = 2.8f;
    m_settings.waves[3].steepness = 0.45f;
    m_settings.waves[3].speed = 1.8f;
}

WaterRenderer::~WaterRenderer()
{
    shutdown();
}

void WaterRenderer::initGL(const std::filesystem::path& shaderDir)
{
    m_shaderDir = shaderDir;
    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, shaderDir / "water.vert");
    builder.addStage(GL_FRAGMENT_SHADER, shaderDir / "water.frag");
    m_shader = builder.build();
    ensureMesh();

    // Load detail normal maps
    auto loadNormalMap = [](const std::string& path) -> GLuint {
        int width, height, channels;
        unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 3);
        if (!data) {
            std::cerr << "Failed to load texture: " << path << std::endl;
            return 0;
        }

        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        stbi_image_free(data);
        return texture;
    };

    m_detailNormal1 = loadNormalMap(RESOURCE_ROOT "/resources/water1/Water_001_NORM.jpg");
    m_detailNormal2 = loadNormalMap(RESOURCE_ROOT "/resources/water2/Water_002_NORM.jpg");
}

void WaterRenderer::shutdown()
{
    destroyMesh();
    
    if (m_detailNormal1) {
        glDeleteTextures(1, &m_detailNormal1);
        m_detailNormal1 = 0;
    }
    if (m_detailNormal2) {
        glDeleteTextures(1, &m_detailNormal2);
        m_detailNormal2 = 0;
    }
}

void WaterRenderer::ensureMesh()
{
    if (m_vao && m_builtResolution == m_settings.resolution)
        return;

    destroyMesh();

    const int res = std::max(2, m_settings.resolution);
    const int verticesPerSide = res + 1;

    std::vector<GridVertex> vertices;
    vertices.reserve(verticesPerSide * verticesPerSide);
    for (int z = 0; z < verticesPerSide; ++z) {
        for (int x = 0; x < verticesPerSide; ++x) {
            vertices.push_back({ glm::vec2(static_cast<float>(x) / static_cast<float>(res), static_cast<float>(z) / static_cast<float>(res)) });
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(res * res * 6);
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(z * verticesPerSide + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + verticesPerSide;
            const uint32_t i3 = i2 + 1;
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }
    }
    m_indexCount = static_cast<GLsizei>(indices.size());

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GridVertex), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GridVertex), reinterpret_cast<void*>(0));

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    m_builtResolution = m_settings.resolution;
}

void WaterRenderer::destroyMesh()
{
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_indexCount = 0;
    m_builtResolution = -1;
}

void WaterRenderer::draw(const glm::mat4& view,
                         const glm::mat4& proj,
                         const glm::vec3& cameraPos,
                         const glm::vec3& lightPos,
                         const glm::vec3& lightColor,
                         const glm::vec3& ambientColor,
                         float ambientStrength,
                         float timeSeconds)
{
    if (!m_settings.enabled)
        return;

    ensureMesh();

    m_shader.bind();

    // Transforms
    if (int loc = m_shader.getUniformLocation("u_view"); loc >= 0)
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
    if (int loc = m_shader.getUniformLocation("u_proj"); loc >= 0)
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(proj));

    // Water params
    if (int loc = m_shader.getUniformLocation("u_levelY"); loc >= 0)
        glUniform1f(loc, m_settings.levelY);
    if (int loc = m_shader.getUniformLocation("u_size"); loc >= 0)
        glUniform1f(loc, m_settings.planeSize);
    if (int loc = m_shader.getUniformLocation("u_time"); loc >= 0)
        glUniform1f(loc, timeSeconds * m_settings.timeScale);
    if (int loc = m_shader.getUniformLocation("u_waveCount"); loc >= 0)
        glUniform1i(loc, std::max(0, std::min(m_settings.waveCount, (int)m_settings.waves.size())));

    // Wave arrays
    {
        std::array<glm::vec2, 4> dirs{};
        std::array<float, 4> amps{};
        std::array<float, 4> wl{};
        std::array<float, 4> steep{};
        std::array<float, 4> speeds{};
        for (int i = 0; i < 4; ++i) {
            const auto& w = m_settings.waves[i];
            const float rad = w.directionDeg * PI / 180.0f;
            dirs[i] = glm::normalize(glm::vec2(std::cos(rad), std::sin(rad)));
            amps[i] = w.amplitude;
            wl[i] = std::max(0.001f, w.wavelength);
            steep[i] = w.steepness;
            speeds[i] = w.speed;
        }
        if (int loc = m_shader.getUniformLocation("u_dirs"); loc >= 0)
            glUniform2fv(loc, 4, glm::value_ptr(dirs[0]));
        if (int loc = m_shader.getUniformLocation("u_amps"); loc >= 0)
            glUniform1fv(loc, 4, amps.data());
        if (int loc = m_shader.getUniformLocation("u_wavelengths"); loc >= 0)
            glUniform1fv(loc, 4, wl.data());
        if (int loc = m_shader.getUniformLocation("u_steepness"); loc >= 0)
            glUniform1fv(loc, 4, steep.data());
        if (int loc = m_shader.getUniformLocation("u_speeds"); loc >= 0)
            glUniform1fv(loc, 4, speeds.data());
    }

    // Lighting
    if (int loc = m_shader.getUniformLocation("u_lightPos"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(lightPos));
    if (int loc = m_shader.getUniformLocation("u_lightColor"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(lightColor));
    if (int loc = m_shader.getUniformLocation("u_ambientColor"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(ambientColor));
    if (int loc = m_shader.getUniformLocation("u_ambientStrength"); loc >= 0)
        glUniform1f(loc, ambientStrength);
    if (int loc = m_shader.getUniformLocation("u_cameraPos"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(cameraPos));

    // Surface material & shading params
    if (int loc = m_shader.getUniformLocation("u_waterColor"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(m_settings.color));
    if (int loc = m_shader.getUniformLocation("u_opacity"); loc >= 0)
        glUniform1f(loc, m_settings.opacity);
    if (int loc = m_shader.getUniformLocation("u_specStrength"); loc >= 0)
        glUniform1f(loc, m_settings.specularStrength);
    if (int loc = m_shader.getUniformLocation("u_shininess"); loc >= 0)
        glUniform1f(loc, m_settings.shininess);
    if (int loc = m_shader.getUniformLocation("u_fresnelStrength"); loc >= 0)
        glUniform1f(loc, m_settings.fresnelStrength);
    if (int loc = m_shader.getUniformLocation("u_shallowColor"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(m_settings.shallowColor));
    if (int loc = m_shader.getUniformLocation("u_deepColor"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(m_settings.deepColor));
    if (int loc = m_shader.getUniformLocation("u_depthRange"); loc >= 0)
        glUniform1f(loc, m_settings.depthRange);

    // Detail normal maps
    if (int loc = m_shader.getUniformLocation("u_detailEnabled"); loc >= 0)
        glUniform1i(loc, m_settings.detailEnabled ? 1 : 0);
    
    if (m_settings.detailEnabled) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_detailNormal1);
        if (int loc = m_shader.getUniformLocation("u_detailNormal1"); loc >= 0)
            glUniform1i(loc, 0);
        
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_detailNormal2);
        if (int loc = m_shader.getUniformLocation("u_detailNormal2"); loc >= 0)
            glUniform1i(loc, 1);
        
        if (int loc = m_shader.getUniformLocation("u_tile1"); loc >= 0)
            glUniform1f(loc, m_settings.tile1);
        if (int loc = m_shader.getUniformLocation("u_tile2"); loc >= 0)
            glUniform1f(loc, m_settings.tile2);
        if (int loc = m_shader.getUniformLocation("u_dir1"); loc >= 0)
            glUniform2fv(loc, 1, glm::value_ptr(m_settings.dir1));
        if (int loc = m_shader.getUniformLocation("u_dir2"); loc >= 0)
            glUniform2fv(loc, 1, glm::value_ptr(m_settings.dir2));
        if (int loc = m_shader.getUniformLocation("u_speed1"); loc >= 0)
            glUniform1f(loc, m_settings.speed1);
        if (int loc = m_shader.getUniformLocation("u_speed2"); loc >= 0)
            glUniform1f(loc, m_settings.speed2);
        if (int loc = m_shader.getUniformLocation("u_strength1"); loc >= 0)
            glUniform1f(loc, m_settings.strength1);
        if (int loc = m_shader.getUniformLocation("u_strength2"); loc >= 0)
            glUniform1f(loc, m_settings.strength2);
        if (int loc = m_shader.getUniformLocation("u_detailBlend"); loc >= 0)
            glUniform1f(loc, m_settings.detailBlend);
    }

    // (Fog removed per feedback)

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void WaterRenderer::drawImGuiPanel()
{
    if (ImGui::Checkbox("Enable Water", &m_settings.enabled)) {
        // no-op
    }
    ImGui::SliderFloat("Water Level (Y)", &m_settings.levelY, -20.0f, 20.0f, "%.2f");
    if (ImGui::SliderFloat("Plane Size", &m_settings.planeSize, 1.0f, 2048.0f, "%.1f")) {
        // size update handled in shader
    }
    int res = m_settings.resolution;
    if (ImGui::SliderInt("Resolution", &res, 2, 1024)) {
        m_settings.resolution = res;
        ensureMesh();
    }

    ImGui::ColorEdit3("Water Color", &m_settings.color.x);
    ImGui::SliderFloat("Opacity", &m_settings.opacity, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Specular Strength", &m_settings.specularStrength, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Shininess", &m_settings.shininess, 1.0f, 256.0f, "%.0f");
    ImGui::SliderFloat("Time Scale", &m_settings.timeScale, 0.0f, 4.0f, "%.2f");

    ImGui::Separator();
    ImGui::TextUnformatted("Fresnel & Tint");
    ImGui::SliderFloat("Fresnel Strength", &m_settings.fresnelStrength, 0.0f, 2.0f, "%.2f");
    ImGui::ColorEdit3("Shallow Color", &m_settings.shallowColor.x);
    ImGui::ColorEdit3("Deep Color", &m_settings.deepColor.x);
    ImGui::SliderFloat("Depth Range", &m_settings.depthRange, 0.1f, 20.0f, "%.2f");

    ImGui::Separator();
    ImGui::TextUnformatted("Detail Normal Maps");
    ImGui::Checkbox("Enable Detail", &m_settings.detailEnabled);
    if (m_settings.detailEnabled) {
        ImGui::SliderFloat("Tile 1", &m_settings.tile1, 0.05f, 5.0f, "%.2f");
        ImGui::SliderFloat("Tile 2", &m_settings.tile2, 0.05f, 5.0f, "%.2f");
        ImGui::SliderFloat2("Direction 1", &m_settings.dir1.x, -2.0f, 2.0f, "%.2f");
        ImGui::SliderFloat2("Direction 2", &m_settings.dir2.x, -2.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Speed 1", &m_settings.speed1, 0.0f, 0.5f, "%.3f");
        ImGui::SliderFloat("Speed 2", &m_settings.speed2, 0.0f, 0.5f, "%.3f");
        ImGui::SliderFloat("Strength 1", &m_settings.strength1, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("Strength 2", &m_settings.strength2, 0.0f, 3.0f, "%.2f");
        ImGui::SliderFloat("Detail Blend", &m_settings.detailBlend, 0.0f, 5.0f, "%.2f");
    }

    // (Fog controls removed)

    ImGui::Separator();
    int wc = m_settings.waveCount;
    if (ImGui::SliderInt("Wave Count", &wc, 0, 4)) {
        m_settings.waveCount = wc;
    }

    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        if (ImGui::TreeNodeEx("Wave", ImGuiTreeNodeFlags_DefaultOpen, "Wave %d", i + 1)) {
            auto& w = m_settings.waves[i];
            ImGui::SliderFloat("Amplitude", &w.amplitude, 0.0f, 5.0f, "%.3f");
            ImGui::SliderFloat("Wavelength", &w.wavelength, 0.1f, 64.0f, "%.2f");
            ImGui::SliderFloat("Direction (deg)", &w.directionDeg, -180.0f, 180.0f, "%.1f");
            ImGui::SliderFloat("Steepness", &w.steepness, 0.0f, 1.2f, "%.2f");
            ImGui::SliderFloat("Speed", &w.speed, 0.0f, 8.0f, "%.2f");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}
