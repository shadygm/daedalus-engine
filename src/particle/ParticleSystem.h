#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <glad/glad.h>

struct FireworkParams {
    float fuse = 0.9f;            // seconds until explosion
    float speed = 28.0f;          // rocket speed
    int burstCount = 200;         // base number of particles in burst
    float minSize = 6.0f;         // particle size range
    float maxSize = 24.0f;
    glm::vec3 baseColor = glm::vec3(1.0f, 0.6f, 0.2f); // base explosion color
    float colorSpread = 0.6f;     // how much to vary color per particle
};

struct Particle {
    glm::vec3 pos;
    glm::vec3 vel;
    glm::vec4 color;
    float life; // seconds remaining (0 = dead)
    float size;
    int type = 0; // 0 = generic, 1 = rocket (firework), 2 = magic orbit, 4 = snow
    FireworkParams firework; // valid for rocket particles
    // Orbital/magic particle fields (used when type == 2)
    glm::vec3 anchor;     // center point to orbit around
    float orbitRadius = 0.0f; // desired orbit radius
    float orbitSpeed = 0.0f;  // tangential speed
    int ownerId = -1; // id of owning system if applicable
    float phase = 0.0f; // angular phase for structured spirals
};

class ParticleSystem {
public:
    ParticleSystem();
    ~ParticleSystem();

    void initGL();
    void shutdownGL();

    void update(float dt);
    void spawnExplosion(const glm::vec3& center, int count = 200);
    void spawnFire(const glm::vec3& center, int count = 100);
    void spawnMagic(const glm::vec3& center, int count = 150);
    // Spawn a magic aura: spiral/ring of particles around `center` that rise and swirl.
    // Magic aura shapes
    enum class MagicAuraShape {
        Ring = 0,
        Helix = 1,
        Torus = 2,
        Spiral = 3
    };

    // spawnMagicAura: creates a dense aura around center.
    // count: total particles to spawn
    // duration: lifetime of particles (seconds)
    // rings: how many concentric rings to populate (>=1)
    // shape: arrangement of particles (Ring, Helix, Torus, Spiral)
    // riseSpeed: initial upward velocity assigned to particles (m/s)
    void spawnMagicAura(const glm::vec3& center, int count = 300, float duration = 8.0f, int rings = 3, MagicAuraShape shape = MagicAuraShape::Ring, float riseSpeed = 0.5f);
    // new signature: supply params for the rocket
    void spawnFirework(const glm::vec3& origin, const glm::vec3& dir, const FireworkParams& params);

    // Snow system
    void enableSnow(bool enable);
    bool isSnowEnabled() const { return m_snowEnabled; }
    void setSnowIntensity(float intensity) { m_snowIntensity = intensity; }
    float getSnowIntensity() const { return m_snowIntensity; }
    void setSnowArea(float area) { m_snowArea = area; }
    float getSnowArea() const { return m_snowArea; }
    void setSnowFlakeSize(float size) { m_snowFlakeSize = size; }
    float getSnowFlakeSize() const { return m_snowFlakeSize; }
    void setSnowSpeed(float speed) { m_snowSpeed = speed; }
    float getSnowSpeed() const { return m_snowSpeed; }
    void updateSnow(float dt, const glm::vec3& cameraPosition);
    bool loadSnowTexture(const std::string& filename);
    const std::string& getSnowTextureName() const { return m_snowTextureName; }
    std::vector<std::string> getAvailableParticleTextures() const;
    
    // General particle texture (for fireworks, magic, etc.)
    bool loadParticleTexture(const std::string& filename);
    const std::string& getParticleTextureName() const { return m_particleTextureName; }
    bool isUsingParticleTexture() const { return m_useParticleTexture; }
    void setUseParticleTexture(bool use) { m_useParticleTexture = use; }

    void draw(const glm::mat4& view, const glm::mat4& proj);

private:
    std::vector<Particle> m_particles;



    // Snow system state
    bool m_snowEnabled { false };
    float m_snowIntensity { 100.0f }; // particles per second
    float m_snowArea { 40.0f }; // square area around camera
    float m_snowFlakeSize { 8.0f }; // size of snow flakes
    float m_snowSpeed { 20.0f }; // falling speed
    float m_snowHeight { 30.0f }; // spawn height above camera
    float m_snowSpawnAccumulator { 0.0f };
    glm::vec3 m_lastSnowCameraPos { 0.0f };
    GLuint m_snowTexture { 0 };
    std::string m_snowTextureName { "muzzle_04.png" };
    
    // General particle texture (for fireworks, magic, etc.)
    GLuint m_particleTexture { 0 };
    std::string m_particleTextureName { "" };
    bool m_useParticleTexture { false };

    GLuint m_vbo{0};
    GLuint m_vao{0};
    GLuint m_program{0};
    GLuint m_texturedProgram{0};

    void uploadBuffers();
    void buildShader();
};
