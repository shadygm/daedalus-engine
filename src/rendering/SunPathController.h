// SPDX-License-Identifier: MIT
#pragma once

#include "util/BezierPath.h"
#include "util/PathAnimator.h"

#include <glm/vec3.hpp>

#include <cstdint>

class LightManager;

class SunPathController {
public:
    enum class Plane {
        XZ,
        XY,
        YZ
    };

    enum class LightStyle {
        Spot,
        Point
    };

    SunPathController();

    void setLightManager(LightManager* manager);

    void setEnabled(bool enabled);
    void setRenderCurve(bool renderCurve);
    void setShowControlPoints(bool showControlPoints);
    void setShowTangents(bool showTangents);
    void setPaused(bool paused);
    void setPlaybackMode(PathPlaybackMode mode);
    void setLightStyle(LightStyle style);
    void setPlane(Plane plane);
    void setSize(float size);
    void setHeight(float height);
    void setRotationDegrees(float degrees);
    void setSpeed(float speed);
    void setTimeScale(float timeScale);

    void scrubTo(float normalized);

    [[nodiscard]] bool enabled() const { return m_enabled; }
    [[nodiscard]] bool renderCurve() const { return m_renderCurve; }
    [[nodiscard]] bool showControlPoints() const { return m_showControlPoints; }
    [[nodiscard]] bool showTangents() const { return m_showTangents; }
    [[nodiscard]] bool paused() const { return m_paused; }
    [[nodiscard]] PathPlaybackMode playbackMode() const { return m_playbackMode; }
    [[nodiscard]] LightStyle lightStyle() const { return m_lightStyle; }
    [[nodiscard]] Plane plane() const { return m_plane; }
    [[nodiscard]] float size() const { return m_size; }
    [[nodiscard]] float height() const { return m_height; }
    [[nodiscard]] float rotationDegrees() const { return m_rotationDegrees; }
    [[nodiscard]] float speed() const { return m_speed; }
    [[nodiscard]] float timeScale() const { return m_timeScale; }
    [[nodiscard]] float normalizedPosition() const { return static_cast<float>(m_animator.normalizedPosition()); }

    void update(double deltaSeconds);

    [[nodiscard]] const BezierPath& path() const { return m_path; }
    [[nodiscard]] bool hasPath() const { return m_path.segmentCount() > 0; }
    [[nodiscard]] const PathAnimator::SampleResult& lastSample() const { return m_lastSample; }
    [[nodiscard]] std::uint64_t pathVersion() const { return m_pathVersion; }

private:
    void rebuildPath();
    void ensurePath();
    void ensureSunLight();
    void applyLight(const PathAnimator::SampleResult& sample, double deltaSeconds);
    void refreshAnimatorParameters();

    LightManager* m_lightManager { nullptr };

    BezierPath m_path;
    PathAnimator m_animator;
    PathAnimator::SampleResult m_lastSample;
    bool m_hasSample { false };

    bool m_enabled { false };
    bool m_renderCurve { true };
    bool m_showControlPoints { false };
    bool m_showTangents { false };
    bool m_paused { false };
    bool m_pathDirty { true };
    bool m_directionValid { false };
    std::uint64_t m_pathVersion { 0 };

    PathPlaybackMode m_playbackMode { PathPlaybackMode::Loop };
    LightStyle m_lightStyle { LightStyle::Spot };
    Plane m_plane { Plane::XZ };

    float m_size { 8.0f };
    float m_height { 6.0f };
    float m_rotationDegrees { 0.0f };
    float m_speed { 5.0f };
    float m_timeScale { 1.0f };

    float m_pendingScrub { 0.0f };
    bool m_scrubRequested { false };

    glm::vec3 m_smoothedDirection { 0.0f, -1.0f, 0.0f };
};
