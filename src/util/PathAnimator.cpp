// SPDX-License-Identifier: MIT
#include "util/PathAnimator.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace {

constexpr double kDoubleEpsilon = 1e-9;

[[nodiscard]] double wrap01(double value)
{
    double wrapped = std::fmod(value, 1.0);
    if (wrapped < 0.0)
        wrapped += 1.0;
    return wrapped;
}

}

void PathAnimator::setPath(const BezierPath* path)
{
    m_path = path;
    m_normalized = wrap01(m_normalized);
    if (std::abs(m_direction) < kDoubleEpsilon)
        m_direction = 1.0;
}

void PathAnimator::setSpeed(float speedMetersPerSecond)
{
    m_speed = speedMetersPerSecond;
}

void PathAnimator::setPaused(bool paused)
{
    m_paused = paused;
}

void PathAnimator::setMode(PathPlaybackMode mode)
{
    if (m_mode != mode) {
        m_mode = mode;
        if (m_mode == PathPlaybackMode::Loop)
            m_direction = 1.0;
    }
}

void PathAnimator::setTimeScale(float scale)
{
    m_timeScale = std::max(scale, 0.0f);
}

void PathAnimator::reset(float normalizedPosition, bool forward)
{
    m_normalized = wrap01(normalizedPosition);
    m_direction = forward ? 1.0 : -1.0;
}

void PathAnimator::setNormalizedPosition(float normalizedPosition)
{
    m_normalized = wrap01(normalizedPosition);
}

void PathAnimator::setDirection(bool forward)
{
    m_direction = forward ? 1.0 : -1.0;
}

void PathAnimator::update(double deltaSeconds)
{
    if (m_paused || m_path == nullptr)
        return;

    const double totalLength = static_cast<double>(m_path->totalLength());
    if (totalLength <= kDoubleEpsilon)
        return;

    const double scaledDelta = std::max(0.0, deltaSeconds) * static_cast<double>(m_timeScale);
    const double distance = static_cast<double>(m_speed) * scaledDelta;
    if (std::abs(distance) <= kDoubleEpsilon)
        return;

    const double deltaNormalized = distance / totalLength;

    if (m_mode == PathPlaybackMode::Loop) {
        m_normalized = wrap01(m_normalized + deltaNormalized);
        return;
    }

    // PingPong
    double next = m_normalized + m_direction * deltaNormalized;
    while (next > 1.0 + kDoubleEpsilon || next < -kDoubleEpsilon) {
        if (next > 1.0) {
            next = 2.0 - next;
            m_direction = -m_direction;
        } else if (next < 0.0) {
            next = -next;
            m_direction = -m_direction;
        }
    }
    m_normalized = std::clamp(next, 0.0, 1.0);
}

PathAnimator::SampleResult PathAnimator::sample() const
{
    SampleResult result;
    if (m_path == nullptr || m_path->totalLength() <= 0.0f)
        return result;

    result.normalized = static_cast<float>(wrap01(m_normalized));
    auto [segmentIndex, segmentT] = m_path->parameterFromNormalized(result.normalized);
    result.segment = segmentIndex;
    result.segmentT = segmentT;
    result.position = m_path->eval(segmentIndex, segmentT);
    result.tangent = m_path->tangent(segmentIndex, segmentT);
    return result;
}
