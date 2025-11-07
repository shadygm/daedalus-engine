// SPDX-License-Identifier: MIT
#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

struct CameraKeyframe {
    glm::vec3 position { 0.0f };
    glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
    float fov { 80.0f };
    float time { 0.0f };
};

class CameraPath {
public:
    struct Sample {
        glm::vec3 position { 0.0f };
        glm::quat rotation { 1.0f, 0.0f, 0.0f, 0.0f };
        float fov { 80.0f };
        std::size_t segmentIndex { 0 };
        float localT { 0.0f };
    };

    [[nodiscard]] bool empty() const { return m_keys.empty(); }
    [[nodiscard]] std::size_t keyCount() const { return m_keys.size(); }
    [[nodiscard]] const std::vector<CameraKeyframe>& keys() const { return m_keys; }

    [[nodiscard]] std::uint64_t version() const { return m_version; }

    [[nodiscard]] bool loopEnabled() const { return m_loop; }
    void setLoopEnabled(bool loop);

    void clear();

    std::size_t addKeyframe(const CameraKeyframe& keyframe);
    void removeKeyframe(std::size_t index);
    void updateKeyframe(std::size_t index, const CameraKeyframe& keyframe);
    [[nodiscard]] const CameraKeyframe& key(std::size_t index) const;

    [[nodiscard]] float startTime() const;
    [[nodiscard]] float endTime() const;
    [[nodiscard]] float duration() const;

    [[nodiscard]] Sample sample(float timeSeconds) const;
    [[nodiscard]] glm::vec3 samplePosition(float timeSeconds) const;
    [[nodiscard]] glm::vec3 samplePositionNormalized(float normalizedTime) const;
    [[nodiscard]] glm::vec3 sampleTangent(float timeSeconds) const;

    static glm::vec3 catmullRom(const glm::vec3& p0,
                                const glm::vec3& p1,
                                const glm::vec3& p2,
                                const glm::vec3& p3,
                                float t);

private:
    void markDirty();
    [[nodiscard]] float clampTime(float timeSeconds) const;
    [[nodiscard]] std::pair<std::size_t, std::size_t> findSegment(float timeSeconds, float& localT) const;

    std::vector<CameraKeyframe> m_keys;
    bool m_loop { false };
    std::uint64_t m_version { 0 };
};
