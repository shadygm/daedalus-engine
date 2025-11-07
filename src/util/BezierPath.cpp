// SPDX-License-Identifier: MIT
#include "util/BezierPath.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>

namespace {

constexpr float kEpsilon = 1e-4f;

[[nodiscard]] float clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

[[nodiscard]] float safeReciprocal(float v)
{
    return std::abs(v) < kEpsilon ? 0.0f : 1.0f / v;
}

}

void BezierPath::setSegments(std::vector<CubicBezier> segments, std::size_t lutResolution)
{
    m_segments = std::move(segments);
    m_segmentLuts.clear();
    m_segmentLuts.reserve(m_segments.size());

    if (m_segments.empty()) {
        m_totalLength = 0.0f;
        return;
    }

    const std::size_t resolution = std::max<std::size_t>(lutResolution, 8);
    m_totalLength = 0.0f;

    for (const CubicBezier& segment : m_segments) {
        SegmentLUT lut;
        lut.samples.reserve(resolution + 1);
        lut.samples.push_back(Sample { 0.0f, 0.0f });

        glm::vec3 prev = evaluateSegment(segment, 0.0f);
        float accumulated = 0.0f;

        for (std::size_t i = 1; i <= resolution; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(resolution);
            glm::vec3 p = evaluateSegment(segment, t);
            accumulated += glm::length(p - prev);
            lut.samples.push_back(Sample { t, accumulated });
            prev = p;
        }

        lut.length = accumulated;
        m_totalLength += accumulated;
        m_segmentLuts.push_back(std::move(lut));
    }
}

float BezierPath::segmentLength(std::size_t segmentIndex) const
{
    if (segmentIndex >= m_segmentLuts.size())
        return 0.0f;
    return m_segmentLuts[segmentIndex].length;
}

const BezierPath::SegmentLUT& BezierPath::segmentLUT(std::size_t segmentIndex) const
{
    static SegmentLUT s_empty {};
    if (segmentIndex >= m_segmentLuts.size())
        return s_empty;
    return m_segmentLuts[segmentIndex];
}

glm::vec3 BezierPath::eval(std::size_t segmentIndex, float t) const
{
    if (segmentIndex >= m_segments.size())
        return glm::vec3(0.0f);
    return evaluateSegment(m_segments[segmentIndex], clamp01(t));
}

glm::vec3 BezierPath::tangent(std::size_t segmentIndex, float t) const
{
    if (segmentIndex >= m_segments.size())
        return glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 tan = evaluateSegmentTangent(m_segments[segmentIndex], clamp01(t));
    const float len = glm::length(tan);
    if (len < kEpsilon)
        return glm::vec3(0.0f, 1.0f, 0.0f);
    return tan / len;
}

glm::vec3 BezierPath::sample(float normalizedArc) const
{
    auto [segment, t] = parameterFromNormalized(normalizedArc);
    return eval(segment, t);
}

glm::vec3 BezierPath::sampleTangent(float normalizedArc) const
{
    auto [segment, t] = parameterFromNormalized(normalizedArc);
    return tangent(segment, t);
}

std::pair<std::size_t, float> BezierPath::parameterFromNormalized(float normalizedArc) const
{
    if (m_segments.empty() || m_totalLength <= kEpsilon)
        return { 0, 0.0f };

    float clamped = normalizedArc - std::floor(normalizedArc);
    if (clamped < 0.0f)
        clamped += 1.0f;

    const float targetLength = clamped * m_totalLength;

    float accumulated = 0.0f;
    for (std::size_t i = 0; i < m_segmentLuts.size(); ++i) {
        const float segLength = m_segmentLuts[i].length;
        if (targetLength <= accumulated + segLength || i == m_segmentLuts.size() - 1) {
            const float localLength = targetLength - accumulated;
            const float t = segmentParameterFromArcLength(i, localLength);
            return { i, t };
        }
        accumulated += segLength;
    }

    return { m_segmentLuts.size() - 1, 1.0f };
}

glm::vec3 BezierPath::evaluateSegment(const CubicBezier& segment, float t)
{
    const float u = 1.0f - t;
    const float uu = u * u;
    const float tt = t * t;
    return (uu * u) * segment.p0
        + (3.0f * uu * t) * segment.p1
        + (3.0f * u * tt) * segment.p2
        + (tt * t) * segment.p3;
}

glm::vec3 BezierPath::evaluateSegmentTangent(const CubicBezier& segment, float t)
{
    const float u = 1.0f - t;
    const float uu = u * u;
    const float tt = t * t;

    glm::vec3 derivative =
        3.0f * uu * (segment.p1 - segment.p0)
        + 6.0f * u * t * (segment.p2 - segment.p1)
        + 3.0f * tt * (segment.p3 - segment.p2);
    return derivative;
}

float BezierPath::segmentParameterFromArcLength(std::size_t segmentIndex, float arcLength) const
{
    if (segmentIndex >= m_segmentLuts.size())
        return 0.0f;

    const SegmentLUT& lut = m_segmentLuts[segmentIndex];
    if (lut.samples.empty() || lut.length <= kEpsilon)
        return 0.0f;

    const float clampedArc = std::clamp(arcLength, 0.0f, lut.length);

    auto it = std::lower_bound(lut.samples.begin(), lut.samples.end(), clampedArc,
        [](const Sample& sample, float value) { return sample.arcLength < value; });

    if (it == lut.samples.begin())
        return it->t;
    if (it == lut.samples.end())
        return lut.samples.back().t;

    const Sample& curr = *it;
    const Sample& prev = *(it - 1);
    const float segmentArc = curr.arcLength - prev.arcLength;
    if (segmentArc <= kEpsilon)
        return curr.t;

    const float alpha = (clampedArc - prev.arcLength) * safeReciprocal(segmentArc);
    float t = prev.t + alpha * (curr.t - prev.t);
    t = clamp01(t);

    return t;
}