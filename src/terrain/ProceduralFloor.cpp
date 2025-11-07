#include "terrain/ProceduralFloor.h"

#include "rendering/TextureUnits.h"

#include "rendering/RenderStats.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <stdexcept>

namespace {
struct GridVertex {
    glm::vec2 uv;
};

constexpr GLuint kHeightImageBinding = 0;

inline bool hasCurrentContext()
{
    return glfwGetCurrentContext() != nullptr;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Failed to open shader file: " + path.string());
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

GLuint compileComputeProgram(const std::filesystem::path& shaderPath)
{
    const std::string source = readFile(shaderPath);
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(logLength), '\0');
        glGetShaderInfoLog(shader, logLength, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Compute shader compilation failed: " + log);
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDetachShader(program, shader);
    glDeleteShader(shader);

    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLength = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
        std::string log(static_cast<size_t>(logLength), '\0');
        glGetProgramInfoLog(program, logLength, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Compute program link failed: " + log);
    }

    return program;
}

inline uint32_t fastHash(int x, int y, uint32_t seed)
{
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 0x27d4eb2dU;
    h = (h << 16) ^ (h >> 16);
    h ^= static_cast<uint32_t>(y) * 0x165667b1U;
    h *= 0x9e3779b9U;
    h ^= h >> 15;
    return h;
}

inline float gradientDot(uint32_t hash, float x, float y)
{
    constexpr std::array<glm::vec2, 8> gradients = {
        glm::vec2(1.0f, 0.0f), glm::vec2(-1.0f, 0.0f), glm::vec2(0.0f, 1.0f), glm::vec2(0.0f, -1.0f),
        glm::vec2(0.70710678f, 0.70710678f), glm::vec2(-0.70710678f, 0.70710678f),
        glm::vec2(0.70710678f, -0.70710678f), glm::vec2(-0.70710678f, -0.70710678f)
    };
    return glm::dot(gradients[hash & 7u], glm::vec2(x, y));
}

inline float fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float perlinHeightSample(const ProceduralFloor::Settings& settings, const glm::vec2& worldPos)
{
    const glm::vec2 scaled = worldPos * settings.frequency;
    const glm::ivec2 cell = glm::floor(scaled);
    const glm::vec2 local = scaled - glm::vec2(cell);

    const glm::vec2 fadeXY(fade(local.x), fade(local.y));

    auto cornerDot = [&](int offsetX, int offsetY) {
        const uint32_t h = fastHash(cell.x + offsetX, cell.y + offsetY, settings.seed);
        const float dx = local.x - static_cast<float>(offsetX);
        const float dy = local.y - static_cast<float>(offsetY);
        return gradientDot(h, dx, dy);
    };

    const float n00 = cornerDot(0, 0);
    const float n10 = cornerDot(1, 0);
    const float n01 = cornerDot(0, 1);
    const float n11 = cornerDot(1, 1);

    const float nx0 = glm::mix(n00, n10, fadeXY.x);
    const float nx1 = glm::mix(n01, n11, fadeXY.x);
    const float n = glm::mix(nx0, nx1, fadeXY.y);
    return glm::clamp(n * settings.amplitude, -settings.amplitude, settings.amplitude);
}
}

ProceduralFloor::ProceduralFloor()
{
    setSettings(m_settings);
}

ProceduralFloor::~ProceduralFloor()
{
    destroyResources();
}

void ProceduralFloor::setSettings(const Settings& settings)
{
    Settings clamped = settings;
    clamped.chunkResolution = std::max(2, clamped.chunkResolution);
    clamped.radiusChunks = std::max(1, clamped.radiusChunks);
    clamped.chunkSize = std::max(1.0f, clamped.chunkSize);
    clamped.amplitude = glm::clamp(clamped.amplitude, 0.0f, 5.0f);

    const bool changed = clamped.chunkSize != m_settings.chunkSize
        || clamped.chunkResolution != m_settings.chunkResolution
        || clamped.radiusChunks != m_settings.radiusChunks
        || clamped.amplitude != m_settings.amplitude
        || clamped.frequency != m_settings.frequency
        || clamped.seed != m_settings.seed;

    m_settings = clamped;
    if (changed)
        m_dirtySettings = true;
}

void ProceduralFloor::update(const glm::vec3& playerPosition)
{
    if (!hasCurrentContext())
        return;

    if (m_dirtySettings || !m_resourcesReady) {
        destroyResources();
        allocateResources();
        m_dirtySettings = false;
    }

    ++m_frameCounter;
    ensureChunksAround(playerPosition);
    recycleInactiveChunks();
}

void ProceduralFloor::allocateResources()
{
    if (!hasCurrentContext())
        return;

    destroyResources();

    const int res = m_settings.chunkResolution;
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

    glGenBuffers(1, &m_instanceVbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), reinterpret_cast<void*>(0));
    glVertexAttribDivisor(1, 1);

    glBindVertexArray(0);

    const GLsizei texSize = verticesPerSide;
    glGenTextures(1, &m_heightTexture);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_heightTexture);
    m_maxActiveLayers = (2 * m_settings.radiusChunks + 1) * (2 * m_settings.radiusChunks + 1);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_R32F, texSize, texSize, m_maxActiveLayers);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    glGenSamplers(1, &m_heightSampler);
    glSamplerParameteri(m_heightSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(m_heightSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(m_heightSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(m_heightSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);


    m_computeProgram = compileComputeProgram(std::filesystem::path(RESOURCE_ROOT "shaders/terrain_heightmap.comp"));
    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/terrain.vert"));
    builder.addStage(GL_FRAGMENT_SHADER, std::filesystem::path(RESOURCE_ROOT "shaders/terrain.frag"));
    m_drawShader = builder.build();

    m_freeLayers.clear();
    m_freeLayers.reserve(m_maxActiveLayers);
    for (int i = 0; i < m_maxActiveLayers; ++i)
        m_freeLayers.push_back(i);

    m_chunks.clear();
    m_lastPlayerChunk = glm::ivec2(0);
    m_resourcesReady = true;
}

void ProceduralFloor::destroyResources()
{
    if (!hasCurrentContext()) {
        m_instanceVbo = 0;
        m_ebo = 0;
        m_vbo = 0;
        m_vao = 0;
        m_heightTexture = 0;
        m_computeProgram = 0;
        m_drawShader = Shader();
        m_chunks.clear();
        m_freeLayers.clear();
        m_resourcesReady = false;
        return;
    }

    if (m_instanceVbo) { glDeleteBuffers(1, &m_instanceVbo); m_instanceVbo = 0; }
    if (m_ebo) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_heightTexture) { glDeleteTextures(1, &m_heightTexture); m_heightTexture = 0; }
    if (m_computeProgram) { glDeleteProgram(m_computeProgram); m_computeProgram = 0; }
    if (m_heightSampler) { glDeleteSamplers(1, &m_heightSampler); m_heightSampler = 0; }

    m_drawShader = Shader();
    m_chunks.clear();
    m_freeLayers.clear();
    m_resourcesReady = false;
}

glm::ivec2 ProceduralFloor::worldToChunk(const Settings& settings, float x, float z)
{
    const float invSize = 1.0f / settings.chunkSize;
    return glm::ivec2(static_cast<int>(std::floor(x * invSize)), static_cast<int>(std::floor(z * invSize)));
}

glm::vec2 ProceduralFloor::chunkLocalUV(const Settings& settings, const Chunk& chunk, float x, float z)
{
    const float u = (x - chunk.origin.x) / settings.chunkSize;
    const float v = (z - chunk.origin.z) / settings.chunkSize;
    return glm::vec2(u, v);
}

ProceduralFloor::Chunk* ProceduralFloor::findChunk(const glm::ivec2& coord)
{
    auto it = m_chunks.find(coord);
    if (it == m_chunks.end())
        return nullptr;
    return &it->second;
}

void ProceduralFloor::ensureChunksAround(const glm::vec3& playerPosition)
{
    if (!m_resourcesReady)
        return;

    const glm::ivec2 playerChunk = worldToChunk(m_settings, playerPosition.x, playerPosition.z);
    m_lastPlayerChunk = playerChunk;

    const int radius = m_settings.radiusChunks;
    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const glm::ivec2 coord = playerChunk + glm::ivec2(dx, dz);
            Chunk* chunkPtr = findChunk(coord);
            if (chunkPtr) {
                chunkPtr->lastTouched = m_frameCounter;
                continue;
            }
            activateChunk(coord);
        }
    }
}

void ProceduralFloor::activateChunk(const glm::ivec2& coord)
{
    if (m_freeLayers.empty()) {
        auto toRemove = std::min_element(m_chunks.begin(), m_chunks.end(), [](const auto& a, const auto& b) {
            return a.second.lastTouched < b.second.lastTouched;
        });
        if (toRemove != m_chunks.end()) {
            m_freeLayers.push_back(toRemove->second.textureLayer);
            m_chunks.erase(toRemove);
        }
    }

    if (m_freeLayers.empty())
        return;

    Chunk chunk;
    chunk.coord = coord;
    chunk.origin = glm::vec3(coord.x * m_settings.chunkSize, 0.0f, coord.y * m_settings.chunkSize);
    chunk.textureLayer = m_freeLayers.back();
    m_freeLayers.pop_back();
    chunk.heights.resize((m_settings.chunkResolution + 1) * (m_settings.chunkResolution + 1));
    chunk.lastTouched = m_frameCounter;

    dispatchChunkGeneration(chunk);
    readbackChunkHeights(chunk);
    chunk.gpuReady = true;

    m_chunks.emplace(coord, std::move(chunk));
}

void ProceduralFloor::recycleInactiveChunks()
{
    if (m_chunks.empty())
        return;

    const int radius = m_settings.radiusChunks;
    std::vector<glm::ivec2> toRemove;
    toRemove.reserve(m_chunks.size());
    for (const auto& kv : m_chunks) {
        const glm::ivec2 diff = kv.first - m_lastPlayerChunk;
        if (std::abs(diff.x) > radius || std::abs(diff.y) > radius)
            toRemove.push_back(kv.first);
    }

    for (const glm::ivec2& coord : toRemove) {
        auto it = m_chunks.find(coord);
        if (it == m_chunks.end())
            continue;
        m_freeLayers.push_back(it->second.textureLayer);
        m_chunks.erase(it);
    }
}

void ProceduralFloor::dispatchChunkGeneration(Chunk& chunk)
{
    glUseProgram(m_computeProgram);
    glUniform3f(glGetUniformLocation(m_computeProgram, "uChunkOrigin"), chunk.origin.x, chunk.origin.y, chunk.origin.z);
    glUniform1f(glGetUniformLocation(m_computeProgram, "uChunkSize"), m_settings.chunkSize);
    glUniform1f(glGetUniformLocation(m_computeProgram, "uAmplitude"), m_settings.amplitude);
    glUniform1f(glGetUniformLocation(m_computeProgram, "uFrequency"), m_settings.frequency);
    glUniform1i(glGetUniformLocation(m_computeProgram, "uResolution"), m_settings.chunkResolution);
    glUniform1ui(glGetUniformLocation(m_computeProgram, "uSeed"), m_settings.seed);
    glUniform1i(glGetUniformLocation(m_computeProgram, "uLayer"), chunk.textureLayer);

    glBindImageTexture(kHeightImageBinding, m_heightTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32F);

    const int side = m_settings.chunkResolution + 1;
    constexpr int groupSize = 16;
    const GLuint dispatchX = static_cast<GLuint>((side + groupSize - 1) / groupSize);
    const GLuint dispatchY = static_cast<GLuint>((side + groupSize - 1) / groupSize);
    glDispatchCompute(dispatchX, dispatchY, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

void ProceduralFloor::readbackChunkHeights(Chunk& chunk)
{
    const int res = m_settings.chunkResolution;
    const int side = res + 1;
    for (int z = 0; z < side; ++z) {
        for (int x = 0; x < side; ++x) {
            const glm::vec2 uv(static_cast<float>(x) / static_cast<float>(res), static_cast<float>(z) / static_cast<float>(res));
            const glm::vec2 worldXZ = glm::vec2(chunk.origin.x, chunk.origin.z) + uv * m_settings.chunkSize;
            chunk.heights[z * side + x] = perlinHeightSample(m_settings, worldXZ);
        }
    }
}

float ProceduralFloor::sampleCpuHeight(const Settings& settings, const Chunk& chunk, const glm::vec2& uv)
{
    const int res = settings.chunkResolution;
    const int side = res + 1;

    const float u = glm::clamp(uv.x, 0.0f, 1.0f);
    const float v = glm::clamp(uv.y, 0.0f, 1.0f);
    const float fx = u * static_cast<float>(res);
    const float fz = v * static_cast<float>(res);

    const int x0 = static_cast<int>(std::floor(fx));
    const int z0 = static_cast<int>(std::floor(fz));
    const int x1 = std::min(x0 + 1, res);
    const int z1 = std::min(z0 + 1, res);

    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);

    const float h00 = chunk.heights[z0 * side + x0];
    const float h10 = chunk.heights[z0 * side + x1];
    const float h01 = chunk.heights[z1 * side + x0];
    const float h11 = chunk.heights[z1 * side + x1];

    const float hx0 = glm::mix(h00, h10, tx);
    const float hx1 = glm::mix(h01, h11, tx);
    return glm::mix(hx0, hx1, tz);
}

float ProceduralFloor::heightAt(float x, float z) const
{
    const glm::ivec2 coord = worldToChunk(m_settings, x, z);
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end())
        return 0.0f;
    const glm::vec2 uv = chunkLocalUV(m_settings, it->second, x, z);
    return sampleCpuHeight(m_settings, it->second, uv);
}

glm::vec3 ProceduralFloor::normalAt(float x, float z) const
{
    const float step = m_settings.chunkSize / static_cast<float>(m_settings.chunkResolution);
    const float hL = heightAt(x - step, z);
    const float hR = heightAt(x + step, z);
    const float hD = heightAt(x, z - step);
    const float hU = heightAt(x, z + step);
    return glm::normalize(glm::vec3(-(hR - hL) / (2.0f * step), 1.0f, -(hU - hD) / (2.0f * step)));
}

bool ProceduralFloor::testSphereCollision(const glm::vec3& center, float radius, float& outPenetration, glm::vec3& outNormal) const
{
    const float ground = heightAt(center.x, center.z);
    const float bottom = center.y - radius;
    if (bottom < ground) {
        outPenetration = ground - bottom;
        outNormal = normalAt(center.x, center.z);
        return true;
    }
    outPenetration = 0.0f;
    outNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    return false;
}

void ProceduralFloor::draw(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& lightPos, const glm::vec3& lightColor, const glm::vec3& ambientColor, float ambientStrength, const glm::vec3& cameraPos, RenderStats* stats)
{
    glBindSampler(0, 0);
    if (!m_resourcesReady || m_chunks.empty())
        return;

    std::vector<glm::vec4> instanceData;
    instanceData.reserve(m_chunks.size());
    for (const auto& kv : m_chunks) {
        const Chunk& chunk = kv.second;
        if (!chunk.gpuReady)
            continue;
        instanceData.emplace_back(chunk.origin.x, chunk.origin.z, static_cast<float>(chunk.textureLayer), 0.0f);
    }

    if (instanceData.empty())
        return;

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
    glBufferData(GL_ARRAY_BUFFER, instanceData.size() * sizeof(glm::vec4), instanceData.data(), GL_DYNAMIC_DRAW);

    m_drawShader.bind();
    // propagate world curvature state to terrain shader if it exposes the uniforms
    if (const GLint loc = m_drawShader.getUniformLocation("uWorldCurvatureEnabled"); loc >= 0)
        glUniform1i(loc, m_worldCurvatureEnabled ? 1 : 0);
    if (const GLint loc2 = m_drawShader.getUniformLocation("uWorldCurvatureStrength"); loc2 >= 0)
        glUniform1f(loc2, m_worldCurvatureStrength);
    if (const GLint loc = m_drawShader.getUniformLocation("view"); loc >= 0)
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(view));
    if (const GLint loc = m_drawShader.getUniformLocation("projection"); loc >= 0)
        glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(proj));
    if (const GLint loc = m_drawShader.getUniformLocation("uChunkSize"); loc >= 0)
        glUniform1f(loc, m_settings.chunkSize);
    if (const GLint loc = m_drawShader.getUniformLocation("uInvResolution"); loc >= 0)
        glUniform1f(loc, 1.0f / static_cast<float>(m_settings.chunkResolution));
    if (const GLint loc = m_drawShader.getUniformLocation("lightPos"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(lightPos));
    if (const GLint loc = m_drawShader.getUniformLocation("lightColor"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(lightColor));
    if (const GLint loc = m_drawShader.getUniformLocation("ambientColor"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(ambientColor));
    if (const GLint loc = m_drawShader.getUniformLocation("ambientStrength"); loc >= 0)
        glUniform1f(loc, ambientStrength);
    if (const GLint loc = m_drawShader.getUniformLocation("cameraPos"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(cameraPos));

    TextureUnits::assertNotEnvUnit(0);
    glBindTextureUnit(0, m_heightTexture);
    glBindSampler(0, m_heightSampler);
    if (const GLint loc = m_drawShader.getUniformLocation("uHeightTex"); loc >= 0)
        glUniform1i(loc, 0);

    // fog uniforms for terrain shader
    if (const GLint loc = m_drawShader.getUniformLocation("uFogEnabled"); loc >= 0)
        glUniform1i(loc, m_fogEnabled ? 1 : 0);
    if (const GLint loc = m_drawShader.getUniformLocation("uFogColor"); loc >= 0)
        glUniform3fv(loc, 1, glm::value_ptr(m_fogColor));
    if (const GLint loc = m_drawShader.getUniformLocation("uFogDensity"); loc >= 0)
        glUniform1f(loc, m_fogDensity);
    if (const GLint loc = m_drawShader.getUniformLocation("uFogGradient"); loc >= 0)
        glUniform1f(loc, m_fogGradient);

    glBindVertexArray(m_vao);
    const GLsizei instanceCount = static_cast<GLsizei>(instanceData.size());
    glDrawElementsInstanced(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr, instanceCount);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    if (stats) {
        const std::uint64_t trianglesPerInstance = m_indexCount / 3;
        const std::uint64_t totalTriangles = trianglesPerInstance * static_cast<std::uint64_t>(instanceCount);
        stats->addDraw(1, totalTriangles);
    }
}

void ProceduralFloor::drawImGui()
{
    if (ImGui::Begin("Procedural Floor")) {
        drawImGuiPanel();
    }
    ImGui::End();
}

void ProceduralFloor::drawImGuiPanel()
{
    static Settings temp = m_settings;
    if (m_dirtySettings)
        temp = m_settings;

    bool changed = false;
    changed |= ImGui::SliderFloat("Chunk Size", &temp.chunkSize, 8.0f, 128.0f);
    changed |= ImGui::SliderInt("Chunk Resolution", &temp.chunkResolution, 16, 256);
    changed |= ImGui::SliderInt("Radius (chunks)", &temp.radiusChunks, 1, 6);
    changed |= ImGui::SliderFloat("Amplitude", &temp.amplitude, 0.1f, 5.0f);
    changed |= ImGui::SliderFloat("Frequency", &temp.frequency, 0.005f, 0.2f, "%.4f");
    changed |= ImGui::InputScalar("Seed", ImGuiDataType_U32, &temp.seed);

    if (ImGui::Button("Apply"))
        setSettings(temp);
    else if (changed)
        ImGui::TextUnformatted("Apply to commit changes");

    ImGui::Separator();
    ImGui::Text("Active chunks: %zu / %d", m_chunks.size(), m_maxActiveLayers);
}
