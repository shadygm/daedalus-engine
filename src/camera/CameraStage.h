// SPDX-License-Identifier: MIT

#pragma once

#include "camera/FPSCamera.h"
#include "camera/TopCamera.h"

#include <functional>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

class Window;

class CameraStage {
public:
	enum class Mode {
		FirstPerson,
		TopView,
		FreeCam
	};

	using GroundHeightProvider = std::function<float(const glm::vec3&)>;

	CameraStage(Window& window, GroundHeightProvider groundHeightProvider = nullptr);

	void update(float deltaTimeSeconds);
	void drawImGuiPanel();

	void setMode(Mode mode);
	[[nodiscard]] Mode getMode() const;

	[[nodiscard]] glm::mat4 getViewMatrix() const;
	[[nodiscard]] glm::vec3 getPosition() const;

	[[nodiscard]] const FPSCamera& getFpsCamera() const;
	[[nodiscard]] FPSCamera& getFpsCamera();
	[[nodiscard]] const TopCamera& getTopCamera() const;
	[[nodiscard]] TopCamera& getTopCamera();

	// Returns true if a jump was requested this frame (edge of space key), then clears the flag.
	[[inline]] bool consumeJumpRequested() { bool r = m_jumpRequested; m_jumpRequested = false; return r; }
	// Retrieve and clear last frame's horizontal move vector (x strafe, z forward)
	[[inline]] glm::vec3 consumeMoveInput() { glm::vec3 r = m_lastMoveInput; r.x *= -1; m_lastMoveInput = glm::vec3(0.0f); return r; }

	void setGroundHeightProvider(GroundHeightProvider provider);
	void setTopCameraPosition(const glm::vec3& position);
	void setTopCameraHeight(float height);
	void resetMouseTracking();

private:
	void updateFirstPerson(float deltaTimeSeconds);
	void updateTopView(float deltaTimeSeconds);
	void updateFreeCam(float deltaTimeSeconds);
	void ensureMouseCapture(bool captureRequested);

	Window& m_window;

	FPSCamera m_fpsCamera;
	TopCamera m_topCamera;

	GroundHeightProvider m_groundHeightProvider;

	Mode m_mode { Mode::FirstPerson };
	bool m_mouseCaptured { false };
	bool m_firstMouseUpdate { true };
	glm::vec2 m_lastCursorPos { 0.0f, 0.0f };
	bool m_spacePressedLastFrame { false };
	double m_lastSpaceTapTime { -1.0 };
	int m_spaceTapCount { 0 };
	bool m_jumpRequested { false }; // set on space key press edge while in first-person mode.
	glm::vec3 m_lastMoveInput { 0.0f }; // horizontal input captured each frame (x,stride; z,forward)

	bool m_fpsInputPaused { false };

	float m_zoomStep { 0.5f };
};
