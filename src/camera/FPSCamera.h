// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

class FPSCamera {
public:
	FPSCamera();

	void setPosition(const glm::vec3& position);
	void setYaw(float yawDegrees);
	void setPitch(float pitchDegrees);
	void setMovementSpeed(float unitsPerSecond);
	void setMouseSensitivity(float degreesPerPixel);
	void setEyeHeight(float eyeHeight);

	[[nodiscard]] glm::vec3 getPosition() const;
	[[nodiscard]] float getYaw() const;
	[[nodiscard]] float getPitch() const;
	[[nodiscard]] float getMovementSpeed() const;
	[[nodiscard]] float getMouseSensitivity() const;
	[[nodiscard]] float getEyeHeight() const;

	// direction.x = strafing (right +, left -)
	// direction.y = vertical (up +, down -)
	// direction.z = forward/backward (forward +, backward -)
	void move(const glm::vec3& direction, float deltaTimeSeconds);
	void addYawPitch(float yawOffsetDeg, float pitchOffsetDeg);
	void clampHeight(float minimumY);

	[[nodiscard]] glm::mat4 getViewMatrix() const;
	[[nodiscard]] glm::vec3 getForward() const;
	[[nodiscard]] glm::vec3 getRight() const;
	[[nodiscard]] glm::vec3 getUp() const;

private:
	void updateVectors();

	glm::vec3 m_position { 0.0f, 1.8f, 4.0f };
	glm::vec3 m_forward { 0.0f, 0.0f, -1.0f };
	glm::vec3 m_right { 1.0f, 0.0f, 0.0f };
	float m_yawDegrees { -90.0f };
	float m_pitchDegrees { 0.0f };

	float m_movementSpeed { 5.0f };
	float m_mouseSensitivity { 0.1f };
	float m_eyeHeight { 1.7f };

	float m_minPitch { -89.0f };
	float m_maxPitch { 89.0f };
};
