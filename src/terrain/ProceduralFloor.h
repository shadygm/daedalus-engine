#pragma once
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <glm/vec2.hpp>
#include <glm/mat4x4.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <functional>
#include <framework/opengl_includes.h>
#include <framework/shader.h>

struct RenderStats;

struct ChunkKeyHash {
    std::size_t operator()(const glm::ivec2& k) const noexcept { return std::hash<int>()(k.x * 73856093 ^ k.y * 19349663); }
};

class ProceduralFloor {
public:
    struct Settings {
        float chunkSize = 32.0f;          // world units per chunk along X/Z
        int chunkResolution = 64;         // quads per side per chunk (=> (res+1)^2 samples)
        int radiusChunks = 3;             // active chunk radius around player
        float amplitude = 5.0f;           // clamp heights to [-amplitude, amplitude]
        float frequency = 0.05f;          // noise frequency scale
        uint32_t seed = 1337u;            // hash seed
    };

    ProceduralFloor();
    ~ProceduralFloor();

    void setSettings(const Settings& settings);
    void update(const glm::vec3& playerPosition);
    void draw(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& lightPos, const glm::vec3& lightColor, const glm::vec3& ambientColor, float ambientStrength, const glm::vec3& cameraPos, RenderStats* stats = nullptr);

    float heightAt(float x, float z) const;
    glm::vec3 normalAt(float x, float z) const;
    bool testSphereCollision(const glm::vec3& center, float radius, float& outPenetration, glm::vec3& outNormal) const;

    const Settings& settings() const { return m_settings; }

    void drawImGui();
    void drawImGuiPanel();

private:
    struct Chunk {
        glm::ivec2 coord {0};
        glm::vec3 origin {0.0f};
        int textureLayer = -1;
        bool gpuReady = false;
        std::vector<float> heights; // CPU cache, size (res+1)^2
        uint64_t lastTouched = 0;
    };

    void allocateResources();
    void destroyResources();
    void ensureChunksAround(const glm::vec3& playerPosition);
    void activateChunk(const glm::ivec2& coord);
    void recycleInactiveChunks();
    void dispatchChunkGeneration(Chunk& chunk);
    void readbackChunkHeights(Chunk& chunk);
    Chunk* findChunk(const glm::ivec2& coord);
    static glm::ivec2 worldToChunk(const Settings& settings, float x, float z);
    static glm::vec2 chunkLocalUV(const Settings& settings, const Chunk& chunk, float x, float z);
    static float sampleCpuHeight(const Settings& settings, const Chunk& chunk, const glm::vec2& uv);

    Settings m_settings;
    bool m_dirtySettings { true };

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLuint m_instanceVbo = 0;
    GLsizei m_indexCount = 0;
    GLuint m_heightSampler = 0;


    GLuint m_heightTexture = 0; // GL_TEXTURE_2D_ARRAY
    GLuint m_computeProgram = 0;
    Shader m_drawShader;
    bool m_resourcesReady = false;

    // world curvature state (applied in the terrain vertex shader if enabled)
    bool m_worldCurvatureEnabled { false };
    float m_worldCurvatureStrength { 0.001f };

    // fog state (applied in terrain fragment shader)
    bool m_fogEnabled { false };
    glm::vec3 m_fogColor { 0.6f, 0.7f, 0.9f };
    float m_fogDensity { 0.01f };
    float m_fogGradient { 1.8f };

public:
    void setWorldCurvatureEnabled(bool enabled) { m_worldCurvatureEnabled = enabled; }
    void setWorldCurvatureStrength(float s) { m_worldCurvatureStrength = s; }
    void setFogEnabled(bool enabled) { m_fogEnabled = enabled; }
    void setFogColor(const glm::vec3& c) { m_fogColor = c; }
    void setFogDensity(float d) { m_fogDensity = d; }
    void setFogGradient(float g) { m_fogGradient = g; }

    int m_maxActiveLayers = 0;
    std::vector<int> m_freeLayers;
    std::unordered_map<glm::ivec2, Chunk, ChunkKeyHash> m_chunks;
    uint64_t m_frameCounter = 0;
    glm::ivec2 m_lastPlayerChunk { 0 };
};
