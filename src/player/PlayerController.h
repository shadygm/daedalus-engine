#pragma once
#include <glm/vec3.hpp>
#include "terrain/ProceduralFloor.h"

class PlayerController {
public:
    struct Params {
        float gravity = -20.0f; // m/s^2
        float jumpImpulse = 8.0f;
        float radius = 0.4f;
        float height = 1.8f; // eye height
    };

    void setParams(const Params& p) { m_params = p; }
    const Params& params() const { return m_params; }

    void setPosition(const glm::vec3& pos) { m_position = pos; }
    // Returns feet position (ground contact point when grounded)
    glm::vec3 position() const { return m_position; }
    // Returns eye position (feet + height)
    glm::vec3 eyePosition() const { return m_position + glm::vec3(0,m_params.height,0); }

    void update(float dt, const ProceduralFloor* floor, bool jumpRequest);
    // Accumulate horizontal movement input (camera-relative) before update.
    void applyMoveInput(const glm::vec3& camForward, const glm::vec3& camRight, const glm::vec3& moveInput, float moveSpeed, float dt);

    bool grounded() const { return m_grounded; }
    glm::vec3 velocity() const { return m_velocity; }

    // Apply an instantaneous vertical impulse to the player (adds to vertical velocity)
    void applyVerticalImpulse(float v);
    // Start a sustained levitation effect: apply upward acceleration for `duration` seconds
    void startLevitation(float duration, float upwardAccel);

private:
    Params m_params;
    // Feet position (not eye)
    glm::vec3 m_position {0,3,0};
    glm::vec3 m_velocity {0,0,0};
    bool m_grounded = false;
    glm::vec3 m_pendingDisplacement {0.0f};
    // Levitation state
    float m_levitationTimeRemaining {0.0f};
    float m_levitationAccel {0.0f};
};
