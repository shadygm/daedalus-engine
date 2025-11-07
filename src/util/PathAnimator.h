// SPDX-License-Identifier: MIT
#pragma once

#include "util/BezierPath.h"

#include <glm/vec3.hpp>

#include <cstddef>

enum class PathPlaybackMode {
    Loop,
    PingPong
};

class PathAnimator {
public:
    struct SampleResult {
        glm::vec3 position { 0.0f };
        glm::vec3 tangent { 0.0f, 1.0f, 0.0f };
        float normalized { 0.0f };
        std::size_t segment { 0 };
        float segmentT { 0.0f };
    };

    void setPath(const BezierPath* path);
    void setSpeed(float speedMetersPerSecond);
    void setPaused(bool paused);
    void setMode(PathPlaybackMode mode);
    void setTimeScale(float scale);

    void reset(float normalizedPosition = 0.0f, bool forward = true);
    void setNormalizedPosition(float normalizedPosition);
    void setDirection(bool forward);

    void update(double deltaSeconds);

    [[nodiscard]] SampleResult sample() const;
    [[nodiscard]] float normalizedPosition() const { return static_cast<float>(m_normalized); }
    [[nodiscard]] bool paused() const { return m_paused; }
    [[nodiscard]] float speed() const { return m_speed; }
    [[nodiscard]] PathPlaybackMode mode() const { return m_mode; }

private:
    const BezierPath* m_path { nullptr };
    double m_normalized { 0.0 };
    double m_direction { 1.0 };
    float m_speed { 1.0f };
    float m_timeScale { 1.0f };
    bool m_paused { false };
    PathPlaybackMode m_mode { PathPlaybackMode::Loop };
};
