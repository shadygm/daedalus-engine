// SPDX-License-Identifier: MIT
#include "camera/CameraPathPlayer.h"

#include "camera/CameraPath.h"
#include "camera/FPSCamera.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>

#include <cmath>

namespace {
constexpr float kEpsilon = 1e-5f;
constexpr glm::vec3 kForward { 0.0f, 0.0f, -1.0f };
}

void CameraPathPlayer::setPath(const CameraPath* path)
{
    m_path = path;
    m_cachedPathVersion = path ? path->version() : 0;
    if (m_path && !m_path->empty())
        m_playhead = m_path->startTime();
    else
        m_playhead = 0.0f;
    syncPlayhead();
}

void CameraPathPlayer::setPlaying(bool playing)
{
    if (!m_path || m_path->keyCount() < 2)
        playing = false;
    m_playing = playing;
}

void CameraPathPlayer::play()
{
    setPlaying(true);
}

void CameraPathPlayer::pause()
{
    m_playing = false;
}

void CameraPathPlayer::stop()
{
    m_playing = false;
    if (m_path && !m_path->empty())
        m_playhead = m_path->startTime();
    else
        m_playhead = 0.0f;
}

void CameraPathPlayer::toggle()
{
    setPlaying(!m_playing);
}

void CameraPathPlayer::setSpeed(float speed)
{
    m_speed = glm::max(speed, 0.0f);
}

void CameraPathPlayer::setPlayhead(float absoluteTimeSeconds)
{
    m_playhead = absoluteTimeSeconds;
    syncPlayhead();
}

void CameraPathPlayer::update(float deltaSeconds)
{
    if (m_path && m_cachedPathVersion != m_path->version()) {
        m_cachedPathVersion = m_path->version();
        syncPlayhead();
    }

    if (!m_playing)
        return;
    if (!m_path || m_path->keyCount() < 2) {
        m_playing = false;
        return;
    }

    const float clampedDelta = glm::max(deltaSeconds, 0.0f);
    if (clampedDelta <= kEpsilon)
        return;

    const float delta = clampedDelta * m_speed;
    if (delta <= kEpsilon)
        return;

    const float start = m_path->startTime();
    const float end = m_path->endTime();
    const float duration = m_path->duration();
    if (duration <= kEpsilon) {
        m_playhead = start;
        m_playing = false;
        return;
    }

    m_playhead += delta;

    if (m_path->loopEnabled()) {
        float relative = m_playhead - start;
        relative = std::fmod(relative, duration);
        if (relative < 0.0f)
            relative += duration;
        m_playhead = start + relative;
    } else if (m_playhead >= end) {
        m_playhead = end;
        m_playing = false;
    }
}

std::optional<CameraPath::Sample> CameraPathPlayer::currentSample() const
{
    if (!m_path || m_path->empty())
        return std::nullopt;
    return m_path->sample(m_playhead);
}

void CameraPathPlayer::applyToCamera(FPSCamera& camera) const
{
    const auto sampleOpt = currentSample();
    if (!sampleOpt)
        return;

    const CameraPath::Sample& sample = *sampleOpt;

    glm::vec3 forward = glm::rotate(sample.rotation, kForward);
    if (glm::length2(forward) <= kEpsilon)
        forward = kForward;
    else
        forward = glm::normalize(forward);

    const float yaw = glm::degrees(std::atan2(forward.z, forward.x));
    const float pitch = glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));

    camera.setPosition(sample.position);
    camera.setYaw(yaw);
    camera.setPitch(pitch);
}

void CameraPathPlayer::syncPlayhead()
{
    if (!m_path || m_path->empty()) {
        m_playhead = 0.0f;
        m_playing = false;
        return;
    }

    const float start = m_path->startTime();
    const float end = m_path->endTime();
    const float duration = m_path->duration();

    if (m_path->loopEnabled() && duration > kEpsilon) {
        float relative = m_playhead - start;
        relative = std::fmod(relative, duration);
        if (relative < 0.0f)
            relative += duration;
        m_playhead = start + relative;
    } else {
        m_playhead = glm::clamp(m_playhead, start, end);
    }

    if (m_path->keyCount() < 2)
        m_playing = false;
}
