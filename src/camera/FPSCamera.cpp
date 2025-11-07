// SPDX-License-Identifier: MIT

#include "camera/FPSCamera.h"

#include <glm/common.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/trigonometric.hpp>

#include <limits>

namespace {
constexpr glm::vec3 WORLD_UP { 0.0f, 1.0f, 0.0f };
}

FPSCamera::FPSCamera()
{
	updateVectors();
}

void FPSCamera::setPosition(const glm::vec3& position)
{
	m_position = position;
}

void FPSCamera::setYaw(float yawDegrees)
{
	m_yawDegrees = yawDegrees;
	updateVectors();
}

void FPSCamera::setPitch(float pitchDegrees)
{
	m_pitchDegrees = glm::clamp(pitchDegrees, m_minPitch, m_maxPitch);
	updateVectors();
}

void FPSCamera::setMovementSpeed(float unitsPerSecond)
{
	m_movementSpeed = unitsPerSecond;
}

void FPSCamera::setMouseSensitivity(float degreesPerPixel)
{
	m_mouseSensitivity = degreesPerPixel;
}

void FPSCamera::setEyeHeight(float eyeHeight)
{
	m_eyeHeight = glm::max(0.0f, eyeHeight);
}

glm::vec3 FPSCamera::getPosition() const
{
	return m_position;
}

float FPSCamera::getYaw() const
{
	return m_yawDegrees;
}

float FPSCamera::getPitch() const
{
	return m_pitchDegrees;
}

float FPSCamera::getMovementSpeed() const
{
	return m_movementSpeed;
}

float FPSCamera::getMouseSensitivity() const
{
	return m_mouseSensitivity;
}

float FPSCamera::getEyeHeight() const
{
	return m_eyeHeight;
}

void FPSCamera::move(const glm::vec3& direction, float deltaTimeSeconds)
{
	if (deltaTimeSeconds <= 0.0f)
		return;

	glm::vec3 displacement { 0.0f };

	if (glm::length(direction) > 0.0f) {
		glm::vec3 localDirection = direction;
		if (glm::length(localDirection) > 1.0f)
			localDirection = glm::normalize(localDirection);

		displacement += m_forward * localDirection.z;
		displacement += WORLD_UP * localDirection.y;
		displacement += m_right * (localDirection.x);
	}

	if (glm::length(displacement) > 0.0f) {
		m_position += glm::normalize(displacement) * m_movementSpeed * deltaTimeSeconds;
	}
}

void FPSCamera::addYawPitch(float yawOffsetDeg, float pitchOffsetDeg)
{
	m_yawDegrees += yawOffsetDeg * m_mouseSensitivity;
	m_pitchDegrees = glm::clamp(m_pitchDegrees + pitchOffsetDeg * m_mouseSensitivity, m_minPitch, m_maxPitch);
	updateVectors();
}

void FPSCamera::clampHeight(float minimumY)
{
	m_position.y = glm::max(m_position.y, minimumY + m_eyeHeight);
}

glm::mat4 FPSCamera::getViewMatrix() const
{
	return glm::lookAt(m_position, m_position + m_forward, WORLD_UP);
}

glm::vec3 FPSCamera::getForward() const
{
	return m_forward;
}

glm::vec3 FPSCamera::getRight() const
{
	return m_right;
}

glm::vec3 FPSCamera::getUp() const
{
	return WORLD_UP;
}

void FPSCamera::updateVectors()
{
	const float yawRad = glm::radians(m_yawDegrees);
	const float pitchRad = glm::radians(m_pitchDegrees);

	const glm::vec3 forward {
		glm::cos(pitchRad) * glm::cos(yawRad),
		glm::sin(pitchRad),
		glm::cos(pitchRad) * glm::sin(yawRad)
	};

	m_forward = glm::normalize(forward);
	m_right = glm::normalize(glm::cross(WORLD_UP, m_forward));
	if (glm::length(m_right) < std::numeric_limits<float>::epsilon())
		m_right = glm::vec3(1.0f, 0.0f, 0.0f);
	else
		m_right = glm::normalize(m_right);
}
