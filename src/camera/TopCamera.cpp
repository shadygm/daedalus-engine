// SPDX-License-Identifier: MIT

#include "camera/TopCamera.h"

#include <glm/common.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/trigonometric.hpp>

#include <limits>

TopCamera::TopCamera()
{
}

void TopCamera::setPosition(const glm::vec3& position)
{
	m_position = position;
	clampHeight();
}

void TopCamera::setMoveSpeed(float unitsPerSecond)
{
	m_moveSpeed = glm::max(0.0f, unitsPerSecond);
}

void TopCamera::setZoomSpeed(float unitsPerScroll)
{
	m_zoomSpeed = glm::max(0.0f, unitsPerScroll);
}

void TopCamera::setHeightLimits(float minHeight, float maxHeight)
{
	m_minHeight = glm::min(minHeight, maxHeight);
	m_maxHeight = glm::max(minHeight, maxHeight);
	clampHeight();
}

void TopCamera::setFocusHeight(float height)
{
	m_focusHeight = height;
}

glm::vec3 TopCamera::getPosition() const
{
	return m_position;
}

glm::vec3 TopCamera::getFocusPoint() const
{
	return { m_position.x, m_focusHeight, m_position.z };
}

float TopCamera::getMoveSpeed() const
{
	return m_moveSpeed;
}

float TopCamera::getZoomSpeed() const
{
	return m_zoomSpeed;
}

float TopCamera::getMinHeight() const
{
	return m_minHeight;
}

float TopCamera::getMaxHeight() const
{
	return m_maxHeight;
}

void TopCamera::move(const glm::vec3& direction, float deltaTimeSeconds)
{
	if (deltaTimeSeconds <= 0.0f || glm::length(direction) == 0.0f)
		return;

	glm::vec3 dir = direction;
	if (glm::length(dir) > 1.0f)
		dir = glm::normalize(dir);

	m_position.x += dir.x * m_moveSpeed * deltaTimeSeconds;
	m_position.z += dir.z * m_moveSpeed * deltaTimeSeconds;
	m_position.y += dir.y * m_moveSpeed * deltaTimeSeconds;
	clampHeight();
}

void TopCamera::zoom(float scrollDelta)
{
	if (glm::abs(scrollDelta) <= std::numeric_limits<float>::epsilon())
		return;

	m_position.y += scrollDelta * m_zoomSpeed;
	clampHeight();
}

void TopCamera::reset(const glm::vec3& position)
{
	m_position = position;
	clampHeight();
}

glm::mat4 TopCamera::getViewMatrix() const
{
	const glm::vec3 forward { 0.0f, -1.0f, 0.0f };
	const glm::vec3 up { 0.0f, 0.0f, -1.0f };
	return glm::lookAt(m_position, m_position + forward, up);
}

void TopCamera::clampHeight()
{
	m_position.y = glm::clamp(m_position.y, m_minHeight, m_maxHeight);
}
