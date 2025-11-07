// SPDX-License-Identifier: MIT
#include "rendering/SunPathController.h"

#include "rendering/LightManager.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/compatibility.hpp>
#include <glm/gtx/norm.hpp>

#include <array>
#include <cmath>
#include <vector>

namespace {

constexpr std::size_t kLutResolution = 512;
constexpr float kDirectionEpsilon = 1e-4f;
constexpr float kDefaultSpotInner = 18.0f;
constexpr float kDefaultSpotOuter = 32.0f;

struct PlaneFrame {
    glm::vec3 axisU { 1.0f, 0.0f, 0.0f };
    glm::vec3 axisV { 0.0f, 1.0f, 0.0f };
    glm::vec3 normal { 0.0f, 0.0f, 1.0f };
};

[[nodiscard]] PlaneFrame buildFrame(SunPathController::Plane plane, float rotationRadians)
{
    PlaneFrame frame;
    switch (plane) {
    case SunPathController::Plane::XZ:
        frame.axisU = glm::vec3(1.0f, 0.0f, 0.0f);
        frame.axisV = glm::vec3(0.0f, 0.0f, 1.0f);
        frame.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        break;
    case SunPathController::Plane::XY:
        frame.axisU = glm::vec3(1.0f, 0.0f, 0.0f);
        frame.axisV = glm::vec3(0.0f, 1.0f, 0.0f);
        frame.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        break;
    case SunPathController::Plane::YZ:
        frame.axisU = glm::vec3(0.0f, 1.0f, 0.0f);
        frame.axisV = glm::vec3(0.0f, 0.0f, 1.0f);
        frame.normal = glm::vec3(1.0f, 0.0f, 0.0f);
        break;
    }

    const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), rotationRadians, frame.normal);
    const glm::mat3 rotation3 = glm::mat3(rotation);
    frame.axisU = rotation3 * frame.axisU;
    frame.axisV = rotation3 * frame.axisV;
    return frame;
}

[[nodiscard]] glm::vec2 evaluateLemniscate2D(float param)
{
    // Lemniscate of Gerono: x = sin(u), y = 0.5 * sin(2u)
    return glm::vec2(std::sin(param), 0.5f * std::sin(2.0f * param));
}

[[nodiscard]] glm::vec2 derivativeLemniscate2D(float param)
{
    // Derivative with respect to parameter u
    return glm::vec2(std::cos(param), std::cos(2.0f * param));
}

[[nodiscard]] std::vector<CubicBezier> buildLemniscateSegments(float scale, float height, SunPathController::Plane plane, float rotationDegrees)
{
    const std::array<float, 5> params = {
        -glm::half_pi<float>(),
        0.0f,
        glm::half_pi<float>(),
        glm::pi<float>(),
        3.0f * glm::half_pi<float>()
    };

    const float rotationRadians = glm::radians(rotationDegrees);
    const PlaneFrame frame = buildFrame(plane, rotationRadians);

    auto mapPoint = [&](const glm::vec2& p) {
        return frame.axisU * (p.x * scale)
            + frame.axisV * (p.y * scale)
            + frame.normal * height;
    };

    auto mapDerivative = [&](const glm::vec2& d) {
        return frame.axisU * (d.x * scale)
            + frame.axisV * (d.y * scale);
    };

    std::vector<CubicBezier> segments;
    segments.reserve(params.size() - 1);

    for (std::size_t i = 0; i < params.size() - 1; ++i) {
        const float u0 = params[i];
        const float u1 = params[i + 1];
        const float delta = u1 - u0;

        const glm::vec2 p0_2d = evaluateLemniscate2D(u0);
        const glm::vec2 p3_2d = evaluateLemniscate2D(u1);
        const glm::vec2 d0_2d = derivativeLemniscate2D(u0);
        const glm::vec2 d1_2d = derivativeLemniscate2D(u1);

        CubicBezier segment;
        segment.p0 = mapPoint(p0_2d);
        segment.p3 = mapPoint(p3_2d);
        const glm::vec3 d0 = mapDerivative(d0_2d) * (delta / 3.0f);
        const glm::vec3 d1 = mapDerivative(d1_2d) * (delta / 3.0f);
        segment.p1 = segment.p0 + d0;
        segment.p2 = segment.p3 - d1;
        segments.push_back(segment);
    }

    return segments;
}

[[nodiscard]] glm::vec3 slerpDirection(const glm::vec3& from, const glm::vec3& to, float t)
{
    if (glm::length2(from) < kDirectionEpsilon)
        return glm::normalize(to);
    if (glm::length2(to) < kDirectionEpsilon)
        return glm::normalize(from);

    float dot = glm::clamp(glm::dot(glm::normalize(from), glm::normalize(to)), -1.0f, 1.0f);
    const float angle = std::acos(dot);
    if (angle < 1e-4f)
        return glm::normalize(glm::mix(from, to, t));

    const float sinAngle = std::sin(angle);
    const float weightA = std::sin((1.0f - t) * angle) / sinAngle;
    const float weightB = std::sin(t * angle) / sinAngle;
    return glm::normalize(weightA * from + weightB * to);
}

} // namespace

SunPathController::SunPathController()
{
    m_animator.setTimeScale(1.0f);
    m_animator.setSpeed(m_speed);
    m_animator.setMode(m_playbackMode);
    m_animator.setPaused(m_paused);
    m_animator.setPath(&m_path);
}

void SunPathController::setLightManager(LightManager* manager)
{
    m_lightManager = manager;
}

void SunPathController::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (!m_enabled)
        m_directionValid = false;
}

void SunPathController::setRenderCurve(bool renderCurve)
{
    m_renderCurve = renderCurve;
}

void SunPathController::setShowControlPoints(bool showControlPoints)
{
    m_showControlPoints = showControlPoints;
}

void SunPathController::setShowTangents(bool showTangents)
{
    m_showTangents = showTangents;
}

void SunPathController::setPaused(bool paused)
{
    m_paused = paused;
    m_animator.setPaused(paused);
}

void SunPathController::setPlaybackMode(PathPlaybackMode mode)
{
    if (m_playbackMode == mode)
        return;
    m_playbackMode = mode;
    m_animator.setMode(mode);
}

void SunPathController::setLightStyle(LightStyle style)
{
    m_lightStyle = style;
}

void SunPathController::setPlane(Plane plane)
{
    if (m_plane == plane)
        return;
    m_plane = plane;
    m_pathDirty = true;
}

void SunPathController::setSize(float size)
{
    const float clamped = std::max(size, 0.1f);
    if (std::abs(clamped - m_size) < 1e-4f)
        return;
    m_size = clamped;
    m_pathDirty = true;
}

void SunPathController::setHeight(float height)
{
    if (std::abs(height - m_height) < 1e-4f)
        return;
    m_height = height;
    m_pathDirty = true;
}

void SunPathController::setRotationDegrees(float degrees)
{
    if (std::abs(degrees - m_rotationDegrees) < 1e-4f)
        return;
    m_rotationDegrees = degrees;
    m_pathDirty = true;
}

void SunPathController::setSpeed(float speed)
{
    m_speed = speed;
    m_animator.setSpeed(speed);
}

void SunPathController::setTimeScale(float timeScale)
{
    m_timeScale = std::max(timeScale, 0.0f);
    m_animator.setTimeScale(m_timeScale);
}

void SunPathController::scrubTo(float normalized)
{
    m_pendingScrub = glm::fract(normalized);
    if (m_pendingScrub < 0.0f)
        m_pendingScrub += 1.0f;
    m_scrubRequested = true;
    m_directionValid = false;
}

void SunPathController::update(double deltaSeconds)
{
    ensurePath();
    refreshAnimatorParameters();

    if (!m_enabled || m_lightManager == nullptr || !hasPath())
        return;

    if (m_scrubRequested) {
        m_animator.setNormalizedPosition(m_pendingScrub);
        m_scrubRequested = false;
    }

    m_animator.update(deltaSeconds);
    m_lastSample = m_animator.sample();
    m_hasSample = true;

    ensureSunLight();
    applyLight(m_lastSample, deltaSeconds);
}

void SunPathController::ensurePath()
{
    if (!m_pathDirty)
        return;

    const float preservedNormalized = m_animator.normalizedPosition();
    const bool wasValid = hasPath();

    std::vector<CubicBezier> segments = buildLemniscateSegments(m_size, m_height, m_plane, m_rotationDegrees);
    m_path.setSegments(std::move(segments), kLutResolution);
    m_animator.setPath(&m_path);
    m_animator.setNormalizedPosition(preservedNormalized);
    m_pathDirty = false;
    ++m_pathVersion;
    if (!wasValid)
        m_directionValid = false;
}

void SunPathController::ensureSunLight()
{
    if (m_lightManager == nullptr)
        return;

    LightManager::Light* existing = m_lightManager->findLightByName("Sun");
    LightManager::LightType desiredType = (m_lightStyle == LightStyle::Spot) ? LightManager::LightType::Spot : LightManager::LightType::Point;
    LightManager::Light& light = m_lightManager->ensureLight("Sun", desiredType);

    if (existing == nullptr) {
        light.enabled = true;
        light.color = glm::vec3(1.0f, 0.95f, 0.85f);
        light.intensity = 5.0f;
        light.range = 50.0f;
        light.innerConeDegrees = kDefaultSpotInner;
        light.outerConeDegrees = kDefaultSpotOuter;
        light.castsShadows = false;
    }

    if (light.type != desiredType) {
        light.type = desiredType;
        if (desiredType == LightManager::LightType::Spot) {
            light.innerConeDegrees = kDefaultSpotInner;
            light.outerConeDegrees = kDefaultSpotOuter;
            light.range = std::max(light.range, 20.0f);
        }
    }

    light.enabled = m_enabled;
    m_lightManager->markDirty();
}

void SunPathController::applyLight(const PathAnimator::SampleResult& sample, double deltaSeconds)
{
    if (m_lightManager == nullptr)
        return;

    LightManager::Light* light = m_lightManager->findLightByName("Sun");
    if (!light)
        return;

    light->position = sample.position;

    if (light->type == LightManager::LightType::Spot) {
        const glm::vec3 targetDir = glm::normalize(-sample.tangent);
        if (!m_directionValid) {
            m_smoothedDirection = targetDir;
            m_directionValid = true;
        } else {
            const float smoothing = glm::clamp(static_cast<float>(deltaSeconds) * 5.0f, 0.0f, 1.0f);
            m_smoothedDirection = slerpDirection(m_smoothedDirection, targetDir, smoothing);
        }
        if (glm::length2(m_smoothedDirection) > kDirectionEpsilon)
            light->direction = glm::normalize(m_smoothedDirection);
    }

    m_lightManager->markDirty();
}

void SunPathController::refreshAnimatorParameters()
{
    m_animator.setPaused(m_paused || !m_enabled);
    m_animator.setMode(m_playbackMode);
    m_animator.setSpeed(m_speed);
    m_animator.setTimeScale(m_timeScale);
}