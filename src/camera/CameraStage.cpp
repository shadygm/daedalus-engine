// SPDX-License-Identifier: MIT

#include "camera/CameraStage.h"

#include "camera/FPSCamera.h"
#include "camera/TopCamera.h"

#include <framework/window.h>

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <imgui/imgui.h>
DISABLE_WARNINGS_POP()

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <limits>
#include <utility>

namespace {
constexpr float DEFAULT_DELTA_TIME = 1.0f / 60.0f;
}

CameraStage::CameraStage(Window& window, GroundHeightProvider groundHeightProvider)
	: m_window(window)
	, m_groundHeightProvider(std::move(groundHeightProvider))
{
	// Place the top camera above the initial FPS position.
	glm::vec3 initialTopPosition = m_fpsCamera.getPosition();
	initialTopPosition.y = glm::max(initialTopPosition.y + 20.0f, m_topCamera.getMinHeight());
	m_topCamera.reset(initialTopPosition);
	if (m_groundHeightProvider)
		m_topCamera.setFocusHeight(m_groundHeightProvider(m_fpsCamera.getPosition()));
	else
		m_topCamera.setFocusHeight(0.0f);

	m_window.registerScrollCallback([this](const glm::vec2& offset) {
		if (m_mode == Mode::TopView) {
			const float zoomDelta = -offset.y * m_zoomStep;
			if (glm::abs(zoomDelta) > std::numeric_limits<float>::epsilon())
				m_topCamera.zoom(zoomDelta);
		}
	});

	m_window.registerKeyCallback([this](int key, int, int action, int) {
		if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS && (m_mode == Mode::FirstPerson || m_mode == Mode::FreeCam)) {
			m_fpsInputPaused = !m_fpsInputPaused;
			ensureMouseCapture(!m_fpsInputPaused);
		}
	});
}

void CameraStage::update(float deltaTimeSeconds)
{
	const float dt = deltaTimeSeconds > 0.0f ? deltaTimeSeconds : DEFAULT_DELTA_TIME;

	switch (m_mode) {
	case Mode::FirstPerson:
		updateFirstPerson(dt);
		break;
	case Mode::TopView:
		updateTopView(dt);
		break;
	case Mode::FreeCam:
		updateFreeCam(dt);
		break;
	}
}

void CameraStage::drawImGuiPanel()
{
	const Mode currentMode = m_mode;
	if (ImGui::RadioButton("First-Person", currentMode == Mode::FirstPerson))
		setMode(Mode::FirstPerson);
	ImGui::SameLine();
	if (ImGui::RadioButton("Top-View", currentMode == Mode::TopView))
		setMode(Mode::TopView);
	ImGui::SameLine();
	if (ImGui::RadioButton("Free-Cam", currentMode == Mode::FreeCam))
		setMode(Mode::FreeCam);

	const glm::vec3 position = getPosition();
	ImGui::Separator();
	ImGui::Text("Position: (%.2f, %.2f, %.2f)", static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z));

	if (m_mode == Mode::FirstPerson || m_mode == Mode::FreeCam) {
		ImGui::Text("Yaw: %.1f", static_cast<double>(m_fpsCamera.getYaw()));
		ImGui::Text("Pitch: %.1f", static_cast<double>(m_fpsCamera.getPitch()));
		float speed = m_fpsCamera.getMovementSpeed();
		if (ImGui::SliderFloat("Move speed", &speed, 1.0f, 20.0f))
			m_fpsCamera.setMovementSpeed(speed);
		float sensitivity = m_fpsCamera.getMouseSensitivity();
		if (ImGui::SliderFloat("Mouse sensitivity", &sensitivity, 0.01f, 1.0f))
			m_fpsCamera.setMouseSensitivity(sensitivity);
	} else {
		const glm::vec3 topPosition = m_topCamera.getPosition();
		ImGui::Text("Height: %.1f", static_cast<double>(topPosition.y));
		float panSpeed = m_topCamera.getMoveSpeed();
		if (ImGui::SliderFloat("Pan speed", &panSpeed, 1.0f, 100.0f))
			m_topCamera.setMoveSpeed(panSpeed);
		float zoomSpeed = m_topCamera.getZoomSpeed();
		if (ImGui::SliderFloat("Zoom speed", &zoomSpeed, 1.0f, 100.0f))
			m_topCamera.setZoomSpeed(zoomSpeed);
		float minHeight = m_topCamera.getMinHeight();
		float maxHeight = m_topCamera.getMaxHeight();
		bool heightChanged = false;
		if (ImGui::DragFloat("Min height", &minHeight, 0.5f, 0.0f, maxHeight - 0.5f))
			heightChanged = true;
		if (ImGui::DragFloat("Max height", &maxHeight, 0.5f, minHeight + 0.5f, 1000.0f))
			heightChanged = true;
		if (heightChanged)
			m_topCamera.setHeightLimits(minHeight, maxHeight);
		if (ImGui::Button("Reset Top Camera")) {
			glm::vec3 resetPos = m_fpsCamera.getPosition();
			resetPos.y = glm::clamp(resetPos.y + 20.0f, m_topCamera.getMinHeight(), m_topCamera.getMaxHeight());
			m_topCamera.reset(resetPos);
			if (m_groundHeightProvider)
				m_topCamera.setFocusHeight(m_groundHeightProvider(resetPos));
		}
	}
}

void CameraStage::setMode(Mode mode)
{
	if (m_mode == mode)
		return;

	const Mode previousMode = m_mode;
	m_mode = mode;
	m_firstMouseUpdate = true;
	m_lastMoveInput = glm::vec3(0.0f);
	m_jumpRequested = false;
	m_spaceTapCount = 0;
	m_lastSpaceTapTime = -1.0;
	m_spacePressedLastFrame = false;

	if (m_mode == Mode::TopView) {
		glm::vec3 topPosition = m_topCamera.getPosition();
		const glm::vec3 fpsPosition = m_fpsCamera.getPosition();
		topPosition.x = fpsPosition.x;
		topPosition.z = fpsPosition.z;
		m_topCamera.setPosition(topPosition);
		if (m_groundHeightProvider)
			m_topCamera.setFocusHeight(m_groundHeightProvider(fpsPosition));
		else
			m_topCamera.setFocusHeight(0.0f);
		ensureMouseCapture(false);
	} else if (m_mode == Mode::FirstPerson) {
		m_fpsInputPaused = false;
		ensureMouseCapture(true);
		// Align FPS camera horizontally with top-focus for smooth switch.
		const glm::vec3 focus = m_topCamera.getFocusPoint();
		glm::vec3 currentPos = m_fpsCamera.getPosition();
		currentPos.x = focus.x;
		currentPos.z = focus.z;
		m_fpsCamera.setPosition(currentPos);
	} else if (m_mode == Mode::FreeCam) {
		m_fpsInputPaused = false;
		ensureMouseCapture(true);
		if (previousMode == Mode::TopView) {
			glm::vec3 startPos = m_topCamera.getPosition();
			m_fpsCamera.setPosition(startPos);
		}
	}
}

CameraStage::Mode CameraStage::getMode() const
{
	return m_mode;
}

glm::mat4 CameraStage::getViewMatrix() const
{
	return m_mode == Mode::TopView ? m_topCamera.getViewMatrix() : m_fpsCamera.getViewMatrix();
}

glm::vec3 CameraStage::getPosition() const
{
	return m_mode == Mode::TopView ? m_topCamera.getPosition() : m_fpsCamera.getPosition();
}

const FPSCamera& CameraStage::getFpsCamera() const
{
	return m_fpsCamera;
}

FPSCamera& CameraStage::getFpsCamera()
{
	return m_fpsCamera;
}

const TopCamera& CameraStage::getTopCamera() const
{
	return m_topCamera;
}

TopCamera& CameraStage::getTopCamera()
{
	return m_topCamera;
}

void CameraStage::setGroundHeightProvider(GroundHeightProvider provider)
{
	m_groundHeightProvider = std::move(provider);
}

void CameraStage::setTopCameraPosition(const glm::vec3& position)
{
	m_topCamera.setPosition(position);
}

void CameraStage::setTopCameraHeight(float height)
{
	glm::vec3 position = m_topCamera.getPosition();
	position.y = height;
	m_topCamera.setPosition(position);
}

void CameraStage::resetMouseTracking()
{
	m_firstMouseUpdate = true;
}

void CameraStage::updateFirstPerson(float deltaTimeSeconds)
{
	ensureMouseCapture(!m_fpsInputPaused);

	if (m_fpsInputPaused)
		return;

	glm::vec3 inputDir { 0.0f };
	if (m_window.isKeyPressed(GLFW_KEY_W))
		inputDir.z += 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_S))
		inputDir.z -= 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_D))
		inputDir.x += 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_A))
		inputDir.x -= 1.0f;
	// Store for external physics/controller to consume
	m_lastMoveInput = inputDir;

	// Jump / double-tap toggle detection
	bool spaceNow = m_window.isKeyPressed(GLFW_KEY_SPACE);
	if (spaceNow && !m_spacePressedLastFrame) {
		// On press edge
		double t = ImGui::GetTime();
		const double threshold = 0.35; // seconds for double tap
		if (m_lastSpaceTapTime >= 0.0 && (t - m_lastSpaceTapTime) <= threshold) {
			// Double tap detected: toggle to TopView (fly)
			m_spaceTapCount = 0;
			m_lastSpaceTapTime = -1.0;
			setMode(Mode::TopView);
			// Suppress jump on toggle
			m_jumpRequested = false;
		} else {
			// First tap
			m_lastSpaceTapTime = t;
			m_spaceTapCount = 1;
			// For now, mark jump; will be cleared if a double tap occurs before threshold
			m_jumpRequested = true;
		}
	}
	// If time elapsed exceeds threshold without second tap, keep single jump
	if (m_spaceTapCount == 1) {
		double t = ImGui::GetTime();
		const double threshold = 0.35;
		if (m_lastSpaceTapTime >= 0.0 && (t - m_lastSpaceTapTime) > threshold) {
			// finalize single tap; reset tracking
			m_spaceTapCount = 0;
			m_lastSpaceTapTime = -1.0;
		}
	}
	m_spacePressedLastFrame = spaceNow;

	const glm::vec2 cursorPos = m_window.getCursorPos();
	if (m_firstMouseUpdate) {
		m_lastCursorPos = cursorPos;
		m_firstMouseUpdate = false;
	}

	const glm::vec2 cursorDelta = cursorPos - m_lastCursorPos;
	m_lastCursorPos = cursorPos;

	m_fpsCamera.addYawPitch(cursorDelta.x, cursorDelta.y);

	// Ground clamping removed; external controller sets vertical position.
}

void CameraStage::updateTopView(float deltaTimeSeconds)
{
	ensureMouseCapture(false);

	const glm::vec2 cursorPos = m_window.getCursorPos();
	if (m_firstMouseUpdate) {
		m_lastCursorPos = cursorPos;
		m_firstMouseUpdate = false;
	}

	glm::vec3 moveDir { 0.0f };
	if (m_window.isKeyPressed(GLFW_KEY_W))
		moveDir.z -= 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_S))
		moveDir.z += 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_D))
		moveDir.x += 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_A))
		moveDir.x -= 1.0f;
	bool spaceNow = m_window.isKeyPressed(GLFW_KEY_SPACE);
	if (spaceNow)
		moveDir.y += 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_LEFT_SHIFT) || m_window.isKeyPressed(GLFW_KEY_RIGHT_SHIFT))
		moveDir.y -= 1.0f;

	m_topCamera.move(moveDir, deltaTimeSeconds);

	if (m_groundHeightProvider) {
		const glm::vec3 focus = m_topCamera.getFocusPoint();
		m_topCamera.setFocusHeight(m_groundHeightProvider(focus));
	}

	// Double-tap on space in TopView toggles back to FirstPerson
	if (spaceNow && !m_spacePressedLastFrame) {
		double t = ImGui::GetTime();
		const double threshold = 0.35;
		if (m_lastSpaceTapTime >= 0.0 && (t - m_lastSpaceTapTime) <= threshold) {
			m_spaceTapCount = 0;
			m_lastSpaceTapTime = -1.0;
			setMode(Mode::FirstPerson);
		} else {
			m_lastSpaceTapTime = t;
			m_spaceTapCount = 1;
		}
	}
	if (m_spaceTapCount == 1) {
		double t = ImGui::GetTime();
		const double threshold = 0.35;
		if (m_lastSpaceTapTime >= 0.0 && (t - m_lastSpaceTapTime) > threshold) {
			m_spaceTapCount = 0;
			m_lastSpaceTapTime = -1.0;
		}
	}
	m_spacePressedLastFrame = spaceNow;

	m_lastCursorPos = cursorPos;
}

void CameraStage::updateFreeCam(float deltaTimeSeconds)
{
	ensureMouseCapture(!m_fpsInputPaused);

	if (m_fpsInputPaused) {
		m_lastMoveInput = glm::vec3(0.0f);
		m_jumpRequested = false;
		return;
	}

	glm::vec3 moveDir { 0.0f };
	if (m_window.isKeyPressed(GLFW_KEY_W))
		moveDir.z += 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_S))
		moveDir.z -= 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_D))
		moveDir.x -= 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_A))
		moveDir.x += 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_SPACE))
		moveDir.y += 1.0f;
	if (m_window.isKeyPressed(GLFW_KEY_LEFT_SHIFT) || m_window.isKeyPressed(GLFW_KEY_RIGHT_SHIFT) ||
		m_window.isKeyPressed(GLFW_KEY_LEFT_CONTROL) || m_window.isKeyPressed(GLFW_KEY_RIGHT_CONTROL))
		moveDir.y -= 1.0f;

	m_fpsCamera.move(moveDir, deltaTimeSeconds);

	const glm::vec2 cursorPos = m_window.getCursorPos();
	if (m_firstMouseUpdate) {
		m_lastCursorPos = cursorPos;
		m_firstMouseUpdate = false;
	}

	const glm::vec2 cursorDelta = cursorPos - m_lastCursorPos;
	m_lastCursorPos = cursorPos;

	m_fpsCamera.addYawPitch(cursorDelta.x, cursorDelta.y);

	m_lastMoveInput = glm::vec3(0.0f);
	m_jumpRequested = false;
	m_spacePressedLastFrame = m_window.isKeyPressed(GLFW_KEY_SPACE);
}

void CameraStage::ensureMouseCapture(bool captureRequested)
{
	if (captureRequested && !m_mouseCaptured) {
		m_window.setMouseCapture(true);
		m_mouseCaptured = true;
		m_firstMouseUpdate = true;
	} else if (!captureRequested && m_mouseCaptured) {
		m_window.setMouseCapture(false);
		m_mouseCaptured = false;
		m_firstMouseUpdate = true;
	}
}
