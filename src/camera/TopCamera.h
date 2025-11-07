// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

class TopCamera {
public:
	TopCamera();

	void setPosition(const glm::vec3& position);
	void setMoveSpeed(float unitsPerSecond);
	void setZoomSpeed(float unitsPerScroll);
	void setHeightLimits(float minHeight, float maxHeight);
	void setFocusHeight(float height);

	[[nodiscard]] glm::vec3 getPosition() const;
	[[nodiscard]] glm::vec3 getFocusPoint() const;
	[[nodiscard]] float getMoveSpeed() const;
	[[nodiscard]] float getZoomSpeed() const;
	[[nodiscard]] float getMinHeight() const;
	[[nodiscard]] float getMaxHeight() const;

	void move(const glm::vec3& direction, float deltaTimeSeconds);
	void zoom(float scrollDelta);
	void reset(const glm::vec3& position);

	[[nodiscard]] glm::mat4 getViewMatrix() const;

private:
	void clampHeight();

	glm::vec3 m_position { 0.0f, 20.0f, 0.0f };
	float m_moveSpeed { 15.0f };
	float m_zoomSpeed { 10.0f };
	float m_minHeight { 2.0f };
	float m_maxHeight { 300.0f };
	float m_focusHeight { 0.0f };
};
