// SPDX-License-Identifier: MIT
#pragma once

#include "camera/CameraPath.h"

#include <optional>

class FPSCamera;

class CameraPathPlayer {
public:
    void setPath(const CameraPath* path);

    void setPlaying(bool playing);
    void play();
    void pause();
    void stop();
    void toggle();

    [[nodiscard]] bool playing() const { return m_playing; }

    void setSpeed(float speed);
    [[nodiscard]] float speed() const { return m_speed; }

    void setPlayhead(float absoluteTimeSeconds);
    [[nodiscard]] float playhead() const { return m_playhead; }

    void update(float deltaSeconds);

    [[nodiscard]] std::optional<CameraPath::Sample> currentSample() const;

    void applyToCamera(FPSCamera& camera) const;

private:
    void syncPlayhead();

    const CameraPath* m_path { nullptr };
    float m_playhead { 0.0f };
    float m_speed { 1.0f };
    bool m_playing { false };
    std::uint64_t m_cachedPathVersion { 0 };
};
