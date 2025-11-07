// SPDX-License-Identifier: MIT
#include "camera/CameraPath.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/compatibility.hpp>
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kEpsilon = 1e-5f;
constexpr float kTangentEpsilon = 1e-5f;
}

void CameraPath::setLoopEnabled(bool loop)
{
    if (m_loop == loop)
        return;
    m_loop = loop;
    markDirty();
}

void CameraPath::clear()
{
    if (m_keys.empty())
        return;
    m_keys.clear();
    markDirty();
}

std::size_t CameraPath::addKeyframe(const CameraKeyframe& keyframe)
{
    auto it = std::lower_bound(m_keys.begin(), m_keys.end(), keyframe.time, [](const CameraKeyframe& k, float t) {
        return k.time < t;
    });
    const std::size_t index = static_cast<std::size_t>(std::distance(m_keys.begin(), it));
    m_keys.insert(it, keyframe);
    markDirty();
    return index;
}

void CameraPath::removeKeyframe(std::size_t index)
{
    if (index >= m_keys.size())
        return;
    m_keys.erase(m_keys.begin() + static_cast<std::ptrdiff_t>(index));
    markDirty();
}

void CameraPath::updateKeyframe(std::size_t index, const CameraKeyframe& keyframe)
{
    if (index >= m_keys.size())
        return;
    m_keys.erase(m_keys.begin() + static_cast<std::ptrdiff_t>(index));
    addKeyframe(keyframe);
}

const CameraKeyframe& CameraPath::key(std::size_t index) const
{
    static const CameraKeyframe kDefault {};
    if (m_keys.empty())
        return kDefault;
    if (index >= m_keys.size())
        index = m_keys.size() - 1;
    return m_keys[index];
}

float CameraPath::startTime() const
{
    if (m_keys.empty())
        return 0.0f;
    return m_keys.front().time;
}

float CameraPath::endTime() const
{
    if (m_keys.empty())
        return 0.0f;
    return m_keys.back().time;
}

float CameraPath::duration() const
{
    if (m_keys.size() < 2)
        return 0.0f;
    return glm::max(endTime() - startTime(), kEpsilon);
}

CameraPath::Sample CameraPath::sample(float timeSeconds) const
{
    Sample result;
    if (m_keys.empty())
        return result;

    if (m_keys.size() == 1) {
        result.position = m_keys.front().position;
        result.rotation = m_keys.front().rotation;
        result.fov = m_keys.front().fov;
        result.segmentIndex = 0;
        result.localT = 0.0f;
        return result;
    }

    const float clampedTime = clampTime(timeSeconds);
    float localT = 0.0f;
    auto [indexA, indexB] = findSegment(clampedTime, localT);

    const CameraKeyframe& k0 = m_keys[indexA];
    const CameraKeyframe& k1 = m_keys[indexB];

    if (m_keys.size() == 2) {
        result.position = glm::mix(k0.position, k1.position, localT);
    } else {
        const std::size_t idxPrev = (indexA == 0) ? (m_loop ? (m_keys.size() - 1) : indexA) : (indexA - 1);
        const std::size_t idxNext = (indexB + 1 < m_keys.size()) ? (indexB + 1) : (m_loop ? 0 : indexB);
        const glm::vec3& p0 = m_keys[idxPrev].position;
        const glm::vec3& p1 = k0.position;
        const glm::vec3& p2 = k1.position;
        const glm::vec3& p3 = m_keys[idxNext].position;
        result.position = catmullRom(p0, p1, p2, p3, localT);
    }

    result.rotation = glm::slerp(k0.rotation, k1.rotation, localT);
    result.fov = glm::mix(k0.fov, k1.fov, localT);
    result.segmentIndex = indexA;
    result.localT = localT;
    return result;
}

glm::vec3 CameraPath::samplePosition(float timeSeconds) const
{
    return sample(timeSeconds).position;
}

glm::vec3 CameraPath::samplePositionNormalized(float normalizedTime) const
{
    if (m_keys.empty())
        return glm::vec3(0.0f);
    if (m_keys.size() == 1)
        return m_keys.front().position;

    const float durationSeconds = duration();
    if (durationSeconds <= kEpsilon)
        return m_keys.back().position;

    float wrapped = glm::clamp(normalizedTime, 0.0f, 1.0f);
    if (m_loop) {
        wrapped = std::fmod(normalizedTime, 1.0f);
        if (wrapped < 0.0f)
            wrapped += 1.0f;
    }

    const float timeValue = startTime() + wrapped * durationSeconds;
    return samplePosition(timeValue);
}

glm::vec3 CameraPath::sampleTangent(float timeSeconds) const
{
    if (m_keys.size() < 2)
        return glm::vec3(0.0f, 1.0f, 0.0f);

    const float clampedTime = clampTime(timeSeconds);
    float localT = 0.0f;
    auto [indexA, indexB] = findSegment(clampedTime, localT);

    const CameraKeyframe& k0 = m_keys[indexA];
    const CameraKeyframe& k1 = m_keys[indexB];

    if (m_keys.size() == 2) {
        const glm::vec3 dir = k1.position - k0.position;
        if (glm::length2(dir) <= kTangentEpsilon)
            return glm::vec3(0.0f, 1.0f, 0.0f);
        return glm::normalize(dir);
    }

    const std::size_t idxPrev = (indexA == 0) ? (m_loop ? (m_keys.size() - 1) : indexA) : (indexA - 1);
    const std::size_t idxNext = (indexB + 1 < m_keys.size()) ? (indexB + 1) : (m_loop ? 0 : indexB);
    const glm::vec3& p0 = m_keys[idxPrev].position;
    const glm::vec3& p1 = k0.position;
    const glm::vec3& p2 = k1.position;
    const glm::vec3& p3 = m_keys[idxNext].position;

    const float t = glm::clamp(localT, 0.0f, 1.0f);
    const float t2 = t * t;

    glm::vec3 tangent = 0.5f * (
        (-p0 + p2)
        + 2.0f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t
        + 3.0f * (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t2);

    if (glm::length2(tangent) <= kTangentEpsilon)
        return glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(tangent);
}

glm::vec3 CameraPath::catmullRom(const glm::vec3& p0,
                                  const glm::vec3& p1,
                                  const glm::vec3& p2,
                                  const glm::vec3& p3,
                                  float t)
{
    const float clampedT = glm::clamp(t, 0.0f, 1.0f);
    const float t2 = clampedT * clampedT;
    const float t3 = t2 * clampedT;
    return 0.5f * ((2.0f * p1)
        + (-p0 + p2) * clampedT
        + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
        + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

void CameraPath::markDirty()
{
    ++m_version;
}

float CameraPath::clampTime(float timeSeconds) const
{
    if (m_keys.size() < 2)
        return m_keys.empty() ? 0.0f : m_keys.front().time;

    const float start = startTime();
    const float end = endTime();
    const float len = glm::max(end - start, kEpsilon);

    if (m_loop) {
        const float relative = timeSeconds - start;
        float wrapped = std::fmod(relative, len);
        if (wrapped < 0.0f)
            wrapped += len;
        return start + wrapped;
    }

    return glm::clamp(timeSeconds, start, end);
}

std::pair<std::size_t, std::size_t> CameraPath::findSegment(float timeSeconds, float& localT) const
{
    if (m_keys.size() < 2) {
        localT = 0.0f;
        return { 0, 0 };
    }

    auto upper = std::upper_bound(m_keys.begin(), m_keys.end(), timeSeconds, [](float value, const CameraKeyframe& key) {
        return value < key.time;
    });

    std::size_t indexA = 0;
    if (upper == m_keys.begin()) {
        indexA = 0;
    } else if (upper == m_keys.end()) {
        indexA = m_keys.size() - 2;
    } else {
        indexA = static_cast<std::size_t>(std::distance(m_keys.begin(), upper) - 1);
    }

    std::size_t indexB = glm::min<std::size_t>(indexA + 1, m_keys.size() - 1);

    const CameraKeyframe& a = m_keys[indexA];
    const CameraKeyframe& b = m_keys[indexB];
    const float delta = glm::max(b.time - a.time, kEpsilon);
    localT = glm::clamp((timeSeconds - a.time) / delta, 0.0f, 1.0f);
    return { indexA, indexB };
}
