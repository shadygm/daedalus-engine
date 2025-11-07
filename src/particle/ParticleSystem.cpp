#include "particle/ParticleSystem.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <cstdlib>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stb/stb_image.h>

// add randf helper before it's used
static inline float randf() { return static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX); }

// Simple helpers to compile shaders (minimal)
static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[1024]; glGetShaderInfoLog(s, 1024, nullptr, buf); std::fprintf(stderr, "shader compile: %s\n", buf); }
    return s;
}
static GLuint buildProgram(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    GLuint vsid = compileShader(GL_VERTEX_SHADER, vs);
    GLuint fsid = compileShader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, vsid); glAttachShader(p, fsid);
    glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char buf[1024]; glGetProgramInfoLog(p,1024,nullptr,buf); std::fprintf(stderr,"prog link: %s\n",buf); }
    glDeleteShader(vsid); glDeleteShader(fsid);
    return p;
}

// Simple particle shaders (vertex sets gl_PointSize)
static const char* s_vs = R"GLSL(
#version 330 core
layout(location=0) in vec3 inPos;
layout(location=1) in vec4 inColor;
layout(location=2) in float inSize;

out vec4 vColor;

uniform mat4 uView;
uniform mat4 uProj;

void main() {
    vec4 clip = uProj * uView * vec4(inPos, 1.0);
    gl_Position = clip;
    // scale point size by clip.w to keep roughly consistent screen size
    gl_PointSize = inSize / gl_Position.w;
    vColor = inColor;
}
)GLSL";

static const char* s_fs = R"GLSL(
#version 330 core
in vec4 vColor;
out vec4 outColor;

void main() {
    // soft circular point (use gl_PointCoord, range [0,1])
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float d = dot(coord, coord);
    float alpha = 1.0 - smoothstep(0.4, 1.0, d);
    outColor = vec4(vColor.rgb, vColor.a * alpha);
}
)GLSL";

// Textured particle shaders
static const char* s_textured_vs = R"GLSL(
#version 330 core
layout(location=0) in vec3 inPos;
layout(location=1) in vec4 inColor;
layout(location=2) in float inSize;

out vec4 vColor;

uniform mat4 uView;
uniform mat4 uProj;

void main() {
    vec4 clip = uProj * uView * vec4(inPos, 1.0);
    gl_Position = clip;
    gl_PointSize = inSize / gl_Position.w;
    vColor = inColor;
}
)GLSL";

static const char* s_textured_fs = R"GLSL(
#version 330 core
in vec4 vColor;
out vec4 outColor;

uniform sampler2D uTexture;

void main() {
    vec4 texColor = texture(uTexture, gl_PointCoord);
    outColor = texColor * vColor;
}
)GLSL";

ParticleSystem::ParticleSystem() = default;
ParticleSystem::~ParticleSystem() { shutdownGL(); }

void ParticleSystem::buildShader() {
    if (m_program) return;
    m_program = buildProgram(s_vs, s_fs);
    m_texturedProgram = buildProgram(s_textured_vs, s_textured_fs);
}

void ParticleSystem::initGL() {
    if (m_vao) return;
    buildShader();
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // layout: vec3 pos, vec4 color, float size -> stride = 8 * 4 = 32
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,1,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(7*sizeof(float)));
    glBindVertexArray(0);
}

void ParticleSystem::shutdownGL() {
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    if (m_texturedProgram) { glDeleteProgram(m_texturedProgram); m_texturedProgram = 0; }
    if (m_snowTexture) { glDeleteTextures(1, &m_snowTexture); m_snowTexture = 0; }
    if (m_particleTexture) { glDeleteTextures(1, &m_particleTexture); m_particleTexture = 0; }
}

void ParticleSystem::uploadBuffers() {
    if (m_particles.empty()) return;
    std::vector<float> buf; buf.reserve(m_particles.size() * 8);
    for (const Particle& p : m_particles) {
        buf.push_back(p.pos.x); buf.push_back(p.pos.y); buf.push_back(p.pos.z);
        buf.push_back(p.color.r); buf.push_back(p.color.g); buf.push_back(p.color.b); buf.push_back(p.color.a);
        buf.push_back(p.size);
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, buf.size()*sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
}

void ParticleSystem::spawnExplosion(const glm::vec3& center, int count)
{
    m_particles.reserve(m_particles.size() + static_cast<size_t>(count));
    for (int i=0;i<count;++i) {
        Particle p;
        // random direction
        float phi = randf() * glm::two_pi<float>();
        float costheta = randf()*2.0f - 1.0f;
        float theta = acos(costheta);
        glm::vec3 dir = glm::vec3(sin(theta)*cos(phi), cos(theta), sin(theta)*sin(phi));
        float speed = 2.0f + randf()*8.0f;
        p.pos = center;
        p.vel = dir * speed;
        p.life = 1.0f + randf()*1.2f;
        p.size = 20.0f + randf()*40.0f;
        p.color = glm::vec4(1.0f, 0.5f + randf()*0.5f, 0.1f*randf(), 1.0f);
        m_particles.push_back(p);
    }
}

void ParticleSystem::spawnFire(const glm::vec3& center, int count)
{
    m_particles.reserve(m_particles.size() + static_cast<size_t>(count));
    for (int i=0;i<count;++i) {
        Particle p;
        p.pos = center + glm::vec3((randf()-0.5f)*0.3f, 0.0f, (randf()-0.5f)*0.3f);
        p.vel = glm::vec3((randf()-0.5f)*0.5f, 1.0f + randf()*1.0f, (randf()-0.5f)*0.5f);
        p.life = 0.8f + randf()*1.5f;
        p.size = 10.0f + randf()*20.0f;
        p.color = glm::vec4(1.0f, 0.6f + randf()*0.4f, 0.1f*randf(), 1.0f);
        m_particles.push_back(p);
    }
}

void ParticleSystem::spawnMagic(const glm::vec3& center, int count)
{
    m_particles.reserve(m_particles.size() + static_cast<size_t>(count));
    for (int i=0;i<count;++i) {
        Particle p;
        float a = randf() * glm::two_pi<float>();
        float r = 0.1f + randf()*0.6f;
        p.pos = center + glm::vec3(cos(a)*r, randf()*0.6f, sin(a)*r);
        p.vel = glm::vec3((randf()-0.5f)*0.5f, randf()*1.0f, (randf()-0.5f)*0.5f);
        p.life = 0.6f + randf()*1.2f;
        p.size = 8.0f + randf()*24.0f;
        p.color = glm::vec4(0.4f + randf()*0.6f, 0.2f + randf()*0.8f, 1.0f, 1.0f);
        m_particles.push_back(p);
    }
}

void ParticleSystem::spawnMagicAura(const glm::vec3& center, int count, float duration, int rings, MagicAuraShape shape, float riseSpeed)
{
    if (count <= 0) return;
    if (rings <= 0) rings = 1;
    // Create multiple concentric rings to avoid gaps. Particles are distributed equally
    // among rings; each ring has slightly different radius and speed.
    const float baseRadius = 0.45f; // tight ring
    const float ringSpacing = 0.08f; // spacing between rings
    const float twoPi = glm::two_pi<float>();

    m_particles.reserve(m_particles.size() + static_cast<size_t>(count));

    int perRing = count / rings;
    int remainder = count % rings;
    // Distribute particles according to chosen shape
    for (int r = 0; r < rings; ++r) {
        int thisRingCount = perRing + (r < remainder ? 1 : 0);
        // base radius for this ring
        float radius = baseRadius + r * ringSpacing;
        float startOffset = randf() * twoPi;

        for (int i = 0; i < thisRingCount; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(thisRingCount);
            float angle = startOffset + t * twoPi;

            Particle p;
            float thisR = radius + (randf() - 0.5f) * 0.02f;

            switch (shape) {
            case MagicAuraShape::Ring: {
                float y = 0.12f + 0.06f * std::sin(t * 6.0f + r);
                p.pos = center + glm::vec3(std::cos(angle) * thisR, y, std::sin(angle) * thisR);
                glm::vec3 tangential = glm::vec3(-std::sin(angle), 0.0f, std::cos(angle));
                float orbitSpeed = 2.0f + 0.5f * r + (randf() - 0.5f) * 0.6f;
                p.orbitSpeed = orbitSpeed;
                p.anchor = center;
                p.orbitRadius = thisR;
                p.vel = tangential * orbitSpeed + glm::vec3(0.0f, riseSpeed, 0.0f);
                break;
            }
            case MagicAuraShape::Helix: {
                // particles placed along a helix winding upward
                float helixTurns = 4.0f + r;
                float helixT = t * helixTurns;
                float y = (t * duration) * 0.25f + 0.05f * r;
                float a = helixT * twoPi + startOffset;
                p.pos = center + glm::vec3(std::cos(a) * thisR, y, std::sin(a) * thisR);
                glm::vec3 tangential = glm::vec3(-std::sin(a), 0.0f, std::cos(a));
                float orbitSpeed = 2.2f + (randf()-0.5f) * 0.6f;
                p.orbitSpeed = orbitSpeed;
                p.anchor = center;
                p.orbitRadius = thisR;
                p.vel = tangential * orbitSpeed + glm::vec3(0.0f, riseSpeed, 0.0f);
                break;
            }
            case MagicAuraShape::Torus: {
                // approximate torus: major radius = thisR, minor radius small
                float minorR = 0.12f + 0.02f * r;
                float theta = angle;
                float phi = t * twoPi + startOffset;
                // torus paramization
                float x = (thisR + minorR * std::cos(phi)) * std::cos(theta);
                float z = (thisR + minorR * std::cos(phi)) * std::sin(theta);
                float y = minorR * std::sin(phi) * 0.6f;
                p.pos = center + glm::vec3(x, y, z);
                // tangential approximate
                glm::vec3 tangential = glm::vec3(-std::sin(theta), 0.0f, std::cos(theta));
                float orbitSpeed = 1.8f + (randf()-0.5f) * 0.6f;
                p.orbitSpeed = orbitSpeed;
                p.anchor = center;
                p.orbitRadius = thisR;
                p.vel = tangential * orbitSpeed + glm::vec3(0.0f, riseSpeed * 0.6f, 0.0f);
                break;
            }
            case MagicAuraShape::Spiral: default: {
                // flat spiral that expands outward and rises slowly
                float spiralTurns = 5.0f;
                float ang = t * spiralTurns * twoPi + startOffset;
                float rad = 0.1f + t * (thisR + 0.25f);
                float y = 0.08f + t * 0.6f;
                p.pos = center + glm::vec3(std::cos(ang) * rad, y, std::sin(ang) * rad);
                glm::vec3 tangential = glm::vec3(-std::sin(ang), 0.0f, std::cos(ang));
                float orbitSpeed = 2.0f + (randf()-0.5f) * 0.6f;
                p.orbitSpeed = orbitSpeed;
                p.anchor = center;
                p.orbitRadius = rad;
                p.vel = tangential * orbitSpeed + glm::vec3(0.0f, riseSpeed, 0.0f);
                break;
            }
            }

            p.life = duration + (randf() - 0.5f) * 0.8f;
            p.size = 10.0f + randf() * 12.0f;
            glm::vec3 col = glm::vec3(0.15f + randf() * 0.4f, 0.3f + randf() * 0.5f, 0.55f + randf() * 0.45f);
            col = glm::clamp(col, glm::vec3(0.0f), glm::vec3(1.0f));
            p.color = glm::vec4(col, 1.0f);
            p.type = 2;

            m_particles.push_back(p);
        }
    }
}



void ParticleSystem::spawnFirework(const glm::vec3& origin, const glm::vec3& dir, const FireworkParams& params)
{
    // create a single "rocket" particle that will explode when life <= 0
    Particle p;
    p.pos = origin;
    p.vel = glm::normalize(dir) * params.speed;
    p.life = params.fuse; // time until explosion
    p.size = 6.0f;
    p.color = glm::vec4(1.0f, 0.9f, 0.6f, 1.0f);
    p.type = 1; // rocket
    p.firework = params;
    m_particles.push_back(p);
}

void ParticleSystem::enableSnow(bool enable) {
    m_snowEnabled = enable;
    if (!enable) {
        // Remove all snow particles when disabling
        m_particles.erase(
            std::remove_if(m_particles.begin(), m_particles.end(),
                [](const Particle& p) { return p.type == 4; }),
            m_particles.end()
        );
    } else {
        // Load default texture if not already loaded
        if (m_snowTexture == 0) {
            loadSnowTexture(m_snowTextureName);
        }
    }
}

bool ParticleSystem::loadSnowTexture(const std::string& filename) {
    // Delete old texture if exists
    if (m_snowTexture) {
        glDeleteTextures(1, &m_snowTexture);
        m_snowTexture = 0;
    }

    std::string fullPath = std::string(RESOURCE_ROOT) + "/resources/particles/" + filename;
    
    int width, height, channels;
    unsigned char* data = stbi_load(fullPath.c_str(), &width, &height, &channels, 4); // Force 4 channels (RGBA)
    
    if (!data) {
        std::cerr << "Failed to load snow texture: " << fullPath << std::endl;
        m_snowTextureName = "";
        return false;
    }

    glGenTextures(1, &m_snowTexture);
    glBindTexture(GL_TEXTURE_2D, m_snowTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    stbi_image_free(data);
    
    m_snowTextureName = filename;
    std::printf("Loaded snow texture: %s (%dx%d, %d channels)\n", filename.c_str(), width, height, channels);
    
    return true;
}

std::vector<std::string> ParticleSystem::getAvailableParticleTextures() const {
    std::vector<std::string> textures;
    std::string particleDir = std::string(RESOURCE_ROOT) + "/resources/particles";
    
    try {
        if (std::filesystem::exists(particleDir) && std::filesystem::is_directory(particleDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(particleDir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    // Convert extension to lowercase for comparison
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
                        textures.push_back(entry.path().filename().string());
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning particles directory: " << e.what() << std::endl;
    }
    
    // Sort alphabetically
    std::sort(textures.begin(), textures.end());
    
    return textures;
}

bool ParticleSystem::loadParticleTexture(const std::string& filename) {
    // Delete old texture if exists
    if (m_particleTexture) {
        glDeleteTextures(1, &m_particleTexture);
        m_particleTexture = 0;
    }

    if (filename.empty()) {
        m_particleTextureName = "";
        m_useParticleTexture = false;
        return true;
    }

    std::string fullPath = std::string(RESOURCE_ROOT) + "/resources/particles/" + filename;
    
    int width, height, channels;
    unsigned char* data = stbi_load(fullPath.c_str(), &width, &height, &channels, 4); // Force 4 channels (RGBA)
    
    if (!data) {
        std::cerr << "Failed to load particle texture: " << fullPath << std::endl;
        m_particleTextureName = "";
        m_useParticleTexture = false;
        return false;
    }

    glGenTextures(1, &m_particleTexture);
    glBindTexture(GL_TEXTURE_2D, m_particleTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    stbi_image_free(data);
    
    m_particleTextureName = filename;
    std::printf("Loaded particle texture: %s (%dx%d, %d channels)\n", filename.c_str(), width, height, channels);
    
    return true;
}

void ParticleSystem::updateSnow(float dt, const glm::vec3& cameraPosition) {
    if (!m_snowEnabled) return;

    m_lastSnowCameraPos = cameraPosition;
    
    // Spawn new snowflakes based on intensity
    m_snowSpawnAccumulator += dt * m_snowIntensity;
    int spawnCount = static_cast<int>(m_snowSpawnAccumulator);
    m_snowSpawnAccumulator -= static_cast<float>(spawnCount);

    for (int i = 0; i < spawnCount; ++i) {
        Particle p;
        // Random position in area around camera
        float offsetX = (randf() - 0.5f) * m_snowArea;
        float offsetZ = (randf() - 0.5f) * m_snowArea;
        p.pos = cameraPosition + glm::vec3(offsetX, m_snowHeight, offsetZ);
        
        // Slight random variation in fall velocity
        float speedVariation = 0.8f + randf() * 0.4f; // 0.8 to 1.2
        p.vel = glm::vec3(0.0f, -m_snowSpeed * speedVariation, 0.0f);
        
        // Add slight wind effect
        p.vel.x += (randf() - 0.5f) * 2.0f;
        p.vel.z += (randf() - 0.5f) * 2.0f;
        
        p.life = m_snowHeight / m_snowSpeed + 2.0f; // enough time to fall
        p.size = m_snowFlakeSize + (randf() - 0.5f) * 2.0f;
        
        // Blue-ish transparent color
        p.color = glm::vec4(0.6f, 0.7f, 0.9f, 0.4f + randf() * 0.3f);
        p.type = 4; // snow type
        
        m_particles.push_back(p);
    }
}

void ParticleSystem::update(float dt) {
    // collect explosion events (pos + params) so we can add explosion particles without corrupting iteration
    std::vector<std::pair<glm::vec3, FireworkParams>> explodeEvents;

    for (auto it = m_particles.begin(); it != m_particles.end();) {
        it->life -= dt;
        if (it->life <= 0.0f) {
            if (it->type == 1) {
                // rocket expired -> schedule explosion at this pos with its params
                explodeEvents.emplace_back(it->pos, it->firework);
            }
            it = m_particles.erase(it);
            continue;
        }
        // physics
        if (it->type == 1) {
            // rocket: mild drag so it slows slightly, and slight upward curve optional
            it->vel *= 0.995f;
            it->vel += glm::vec3(0.0f, 0.5f, 0.0f) * dt; // slight rise
            it->pos += it->vel * dt;
        } else if (it->type == 2) {
            // magic orbital particles: create tangential motion around anchor
            // compute radial vector from anchor to particle
            glm::vec3 radial = it->pos - it->anchor;
            float rlen = glm::length(radial);
            glm::vec3 radialDir = rlen > 1e-6f ? radial / rlen : glm::vec3(1.0f, 0.0f, 0.0f);
            // tangent (around Y axis)
            glm::vec3 tangent = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), radialDir));

            // desired tangential velocity
            glm::vec3 vTang = tangent * it->orbitSpeed;
            // small radial correction to keep particles near orbitRadius
            float radialError = it->orbitRadius - rlen;
            glm::vec3 vRadial = radialDir * (radialError * 4.0f);
            // gentle upward drift
            glm::vec3 vUp = glm::vec3(0.0f, 0.35f, 0.0f);

            // combine (no heavy gravity)
            it->vel = vTang + vRadial + vUp;
            it->pos += it->vel * dt;
        } else if (it->type == 4) {
            // Snow particle: simple falling with gravity
            it->pos += it->vel * dt;
            // Remove snow particles that fall below a certain threshold or move too far from camera
            if (it->pos.y < m_lastSnowCameraPos.y - 10.0f) {
                it->life = 0.0f; // mark for removal
            }
            float distFromCamera = glm::length(glm::vec2(it->pos.x - m_lastSnowCameraPos.x, 
                                                         it->pos.z - m_lastSnowCameraPos.z));
            if (distFromCamera > m_snowArea * 0.7f) {
                it->life = 0.0f; // mark for removal if too far from camera
            }
        } else {
            // generic particles: gravity
            it->vel += glm::vec3(0.0f, -9.8f, 0.0f) * dt * 0.25f;
            it->pos += it->vel * dt;
        }

        // fade alpha relative to remaining life (we assume initial life <= 2s)
        // For longer-lived magic particles we scale alpha differently
        float alpha = glm::clamp(it->life, 0.0f, 1.0f);
        if (it->type == 2) {
            // magic aura uses slower fade
            alpha = glm::clamp(it->life / 6.0f, 0.0f, 1.0f);
        }
        it->color.a = alpha;
        ++it;
    }

    // spawn explosions for each rocket that expired
    for (const auto& ev : explodeEvents) {
        const glm::vec3& pos = ev.first;
        const FireworkParams& params = ev.second;
        int burstCount = params.burstCount;
        m_particles.reserve(m_particles.size() + static_cast<size_t>(burstCount));
        for (int i = 0; i < burstCount; ++i) {
            Particle q;
            // random spherical direction biased outward
            float phi = randf() * glm::two_pi<float>();
            float cosT = randf() * 2.0f - 1.0f;
            float theta = std::acos(cosT);
            glm::vec3 dir = glm::vec3(std::sin(theta)*std::cos(phi), std::cos(theta), std::sin(theta)*std::sin(phi));
            float speed = 4.0f + randf() * 12.0f;
            q.pos = pos;
            q.vel = dir * speed;
            q.life = 0.8f + randf() * 1.6f;
            q.size = params.minSize + randf() * (params.maxSize - params.minSize);
            // colorful palette around baseColor
            glm::vec3 base = params.baseColor;
            glm::vec3 col = base + glm::vec3((randf()-0.5f)*params.colorSpread, (randf()-0.5f)*params.colorSpread, (randf()-0.5f)*params.colorSpread);
            col = glm::clamp(col, glm::vec3(0.0f), glm::vec3(1.0f));
            q.color = glm::vec4(col, 1.0f);
            q.type = 0;
            m_particles.push_back(q);
        }
    }

    uploadBuffers();
}

void ParticleSystem::draw(const glm::mat4& view, const glm::mat4& proj) {
    if (m_particles.empty()) return;
    
    // Count snow vs non-snow particles
    size_t snowCount = 0;
    size_t nonSnowCount = 0;
    for (const auto& p : m_particles) {
        if (p.type == 4) snowCount++;
        else nonSnowCount++;
    }
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glDepthMask(GL_FALSE);

    // Draw non-snow particles
    if (nonSnowCount > 0) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Additive blending for fire/magic effects
        
        // Build buffer with only non-snow particles
        std::vector<float> buf;
        buf.reserve(nonSnowCount * 8);
        for (const Particle& p : m_particles) {
            if (p.type != 4) {
                buf.push_back(p.pos.x); buf.push_back(p.pos.y); buf.push_back(p.pos.z);
                buf.push_back(p.color.r); buf.push_back(p.color.g); buf.push_back(p.color.b); buf.push_back(p.color.a);
                buf.push_back(p.size);
            }
        }
        
        if (!buf.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferData(GL_ARRAY_BUFFER, buf.size()*sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
            
            // Use textured shader if particle texture is loaded and enabled
            if (m_useParticleTexture && m_particleTexture && m_texturedProgram) {
                glUseProgram(m_texturedProgram);
                GLint locV = glGetUniformLocation(m_texturedProgram, "uView"); if (locV>=0) glUniformMatrix4fv(locV,1,GL_FALSE, glm::value_ptr(view));
                GLint locP = glGetUniformLocation(m_texturedProgram, "uProj"); if (locP>=0) glUniformMatrix4fv(locP,1,GL_FALSE, glm::value_ptr(proj));
                GLint locT = glGetUniformLocation(m_texturedProgram, "uTexture"); 
                if (locT>=0) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, m_particleTexture);
                    glUniform1i(locT, 0);
                }
            } else {
                // Use regular shader
                glUseProgram(m_program);
                GLint locV = glGetUniformLocation(m_program, "uView"); if (locV>=0) glUniformMatrix4fv(locV,1,GL_FALSE, glm::value_ptr(view));
                GLint locP = glGetUniformLocation(m_program, "uProj"); if (locP>=0) glUniformMatrix4fv(locP,1,GL_FALSE, glm::value_ptr(proj));
            }
            
            glBindVertexArray(m_vao);
            glDrawArrays(GL_POINTS, 0, (GLsizei)nonSnowCount);
            glBindVertexArray(0);
        }
    }
    
    // Draw snow particles with textured shader
    if (snowCount > 0 && m_snowTexture && m_texturedProgram) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // Alpha blending for snow
        glUseProgram(m_texturedProgram);
        GLint locV = glGetUniformLocation(m_texturedProgram, "uView"); if (locV>=0) glUniformMatrix4fv(locV,1,GL_FALSE, glm::value_ptr(view));
        GLint locP = glGetUniformLocation(m_texturedProgram, "uProj"); if (locP>=0) glUniformMatrix4fv(locP,1,GL_FALSE, glm::value_ptr(proj));
        GLint locT = glGetUniformLocation(m_texturedProgram, "uTexture"); 
        if (locT>=0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_snowTexture);
            glUniform1i(locT, 0);
        }

        // Build buffer with only snow particles
        std::vector<float> buf;
        buf.reserve(snowCount * 8);
        for (const Particle& p : m_particles) {
            if (p.type == 4) {
                buf.push_back(p.pos.x); buf.push_back(p.pos.y); buf.push_back(p.pos.z);
                buf.push_back(p.color.r); buf.push_back(p.color.g); buf.push_back(p.color.b); buf.push_back(p.color.a);
                buf.push_back(p.size);
            }
        }
        if (!buf.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
            glBufferData(GL_ARRAY_BUFFER, buf.size()*sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
            glBindVertexArray(m_vao);
            glDrawArrays(GL_POINTS, 0, (GLsizei)snowCount);
            glBindVertexArray(0);
        }
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDisable(GL_BLEND);
    glUseProgram(0);
}
