#include "player/PlayerController.h"
#include <glm/glm.hpp>

void PlayerController::applyMoveInput(const glm::vec3& camForward, const glm::vec3& camRight, const glm::vec3& moveInput, float moveSpeed, float dt) {
    if (dt <= 0.0f) return;
    glm::vec3 f = glm::normalize(glm::vec3(camForward.x, 0.0f, camForward.z));
    glm::vec3 r = glm::normalize(glm::vec3(camRight.x,   0.0f, camRight.z));
    glm::vec3 wish = f * moveInput.z + r * moveInput.x;
    if (glm::length(wish) > 1e-6f) wish = glm::normalize(wish);
    m_pendingDisplacement += wish * moveSpeed * dt;
}

void PlayerController::update(float dt, const ProceduralFloor* floor, bool jumpRequest) {
    if (dt <= 0) return;
    // Apply accumulated horizontal displacement first
    if (glm::length(m_pendingDisplacement) > 0.0f) {
        m_position += m_pendingDisplacement;
        m_pendingDisplacement = glm::vec3(0.0f);
    }

    // Gravity
    // Gravity
    // If levitating, apply upward acceleration instead of letting gravity fully dominate.
    if (m_levitationTimeRemaining > 0.0f) {
        m_velocity.y += m_levitationAccel * dt;
        m_levitationTimeRemaining -= dt;
    } else {
        m_velocity.y += m_params.gravity * dt;
    }

    // Integrate vertical only (horizontal done by pending displacement)
    m_position.y += m_velocity.y * dt;

    if (floor) {
        // Collision (bottom sphere centered at feet + radius)
        glm::vec3 bottomCenter = m_position + glm::vec3(0, m_params.radius, 0);
        float pen; glm::vec3 n;
        if (floor->testSphereCollision(bottomCenter, m_params.radius, pen, n)) {
            // adjust position upward
            // Move feet up by penetration
            m_position.y += pen;
            m_velocity.y = 0.0f;
            m_grounded = true;
            if (jumpRequest) {
                m_velocity.y = m_params.jumpImpulse;
                m_grounded = false;
            }
        } else {
            m_grounded = false;
        }

        // Ensure feet never below sampled terrain height (robustness)
        float groundH = floor->heightAt(m_position.x, m_position.z);
        if (m_position.y < groundH) {
            m_position.y = groundH;
            if (m_velocity.y < 0) m_velocity.y = 0;
            m_grounded = true;
        }
    } else {
        constexpr float kGroundHeight = 0.0f;
        if (m_position.y <= kGroundHeight) {
            m_position.y = kGroundHeight;
            if (m_velocity.y < 0.0f)
                m_velocity.y = 0.0f;
            m_grounded = true;
            if (jumpRequest) {
                m_velocity.y = m_params.jumpImpulse;
                m_grounded = false;
            }
        } else {
            m_grounded = false;
        }
    }
}

void PlayerController::applyVerticalImpulse(float v) {
    m_velocity.y += v;
    // if we apply an upward impulse, we are not grounded
    m_grounded = false;
}

void PlayerController::startLevitation(float duration, float upwardAccel) {
    if (duration <= 0.0f) return;
    m_levitationTimeRemaining = duration;
    m_levitationAccel = upwardAccel;
    // ensure player is considered airborne
    m_grounded = false;
}
