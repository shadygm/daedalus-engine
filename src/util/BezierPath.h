// SPDX-License-Identifier: MIT
#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <utility>
#include <vector>

struct CubicBezier {
    glm::vec3 p0 { 0.0f };
    glm::vec3 p1 { 0.0f };
    glm::vec3 p2 { 0.0f };
    glm::vec3 p3 { 0.0f };
};

class BezierPath {
public:
    struct Sample {
        float t { 0.0f };
        float arcLength { 0.0f };
    };

    struct SegmentLUT {
        std::vector<Sample> samples;
        float length { 0.0f };
    };

    void setSegments(std::vector<CubicBezier> segments, std::size_t lutResolution = 256);

    [[nodiscard]] std::size_t segmentCount() const { return m_segments.size(); }
    [[nodiscard]] float totalLength() const { return m_totalLength; }
    [[nodiscard]] float segmentLength(std::size_t segmentIndex) const;

    [[nodiscard]] const std::vector<CubicBezier>& segments() const { return m_segments; }
    [[nodiscard]] const SegmentLUT& segmentLUT(std::size_t segmentIndex) const;

    [[nodiscard]] glm::vec3 eval(std::size_t segmentIndex, float t) const;
    [[nodiscard]] glm::vec3 tangent(std::size_t segmentIndex, float t) const;

    [[nodiscard]] glm::vec3 sample(float normalizedArc) const;
    [[nodiscard]] glm::vec3 sampleTangent(float normalizedArc) const;

    [[nodiscard]] std::pair<std::size_t, float> parameterFromNormalized(float normalizedArc) const;

    [[nodiscard]] static glm::vec3 evaluateSegment(const CubicBezier& segment, float t);
    [[nodiscard]] static glm::vec3 evaluateSegmentTangent(const CubicBezier& segment, float t);

private:
    [[nodiscard]] float segmentParameterFromArcLength(std::size_t segmentIndex, float arcLength) const;

    std::vector<CubicBezier> m_segments;
    std::vector<SegmentLUT> m_segmentLuts;
    float m_totalLength { 0.0f };
};
