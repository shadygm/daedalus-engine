#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <array>
#include <filesystem>
#include <framework/opengl_includes.h>
#include <framework/shader.h>

class WaterRenderer {
public:
    struct Wave {
        float amplitude = 0.3f;     // meters
        float wavelength = 6.0f;    // meters
        float directionDeg = 0.0f;  // degrees from +X towards +Z
        float steepness = 0.5f;     // 0..1 typically
        float speed = 1.0f;         // units/sec (phase speed)
    };

    struct Settings {
        bool enabled = false;
        float levelY = 0.0f;        // water plane base height
        float planeSize = 64.0f;    // extent in world units
        int resolution = 200;       // grid resolution per side

        glm::vec3 color = glm::vec3(0.0f, 0.4f, 0.6f);
        float opacity = 0.6f;

        float specularStrength = 0.3f;
        float shininess = 64.0f;

        // Fresnel and tint
        float fresnelStrength = 1.0f;   // scales Schlick fresnel
        glm::vec3 shallowColor = glm::vec3(0.15f, 0.45f, 0.55f);
        glm::vec3 deepColor = glm::vec3(0.02f, 0.12f, 0.20f);
        float depthRange = 3.0f;        // meters from surface to reach deep tint


        int waveCount = 3;              // active waves
        std::array<Wave, 4> waves;      // up to 4 waves
        float timeScale = 1.0f;         // global time scale

        // Detail normal maps (micro ripples)
        bool detailEnabled = false;
        float tile1 = 0.5f;
        float tile2 = 0.7f;
        glm::vec2 dir1 = glm::vec2(1.0f, 0.3f);
        glm::vec2 dir2 = glm::vec2(-0.8f, -0.5f);
        float speed1 = 0.05f;
        float speed2 = 0.04f;
        float strength1 = 1.0f;
        float strength2 = 0.8f;
        float detailBlend = 2.0f;  // strength of detail perturbation
    };

    WaterRenderer();
    ~WaterRenderer();

    void initGL(const std::filesystem::path& shaderDir);
    void shutdown();

    void ensureMesh();

    void draw(const glm::mat4& view,
              const glm::mat4& proj,
              const glm::vec3& cameraPos,
              const glm::vec3& lightPos,
              const glm::vec3& lightColor,
              const glm::vec3& ambientColor,
              float ambientStrength,
              float timeSeconds);

    void drawImGuiPanel();

    Settings& settings() { return m_settings; }
    const Settings& settings() const { return m_settings; }

private:
    struct GridVertex { glm::vec2 uv; };

    void destroyMesh();

    Settings m_settings;

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLsizei m_indexCount = 0;

    Shader m_shader;
    std::filesystem::path m_shaderDir;

    // Detail normal map textures
    GLuint m_detailNormal1 = 0;
    GLuint m_detailNormal2 = 0;

    // cached for reallocation
    int m_builtResolution = -1;
};
