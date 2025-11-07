// SPDX-License-Identifier: MIT
#pragma once

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class PendulumManager {
public:
    enum class Integrator {
        SemiImplicitEuler,
        RungeKutta4
    };

    struct Settings {
        float gravity { 9.81f };
        float damping { 0.015f };
        float nodeRadius { 0.18f };
        float barThickness { 0.075f };
        float fixedTimeStep { 1.0f / 120.0f };
        int substeps { 1 };
        Integrator integrator { Integrator::SemiImplicitEuler };
    };

    struct NodeState {
        float mass { 1.0f };
        float length { 1.0f };
        glm::vec3 position { 0.0f, 0.0f, 0.0f };
        glm::vec3 velocity { 0.0f, 0.0f, 0.0f };
        glm::vec3 previousPosition { 0.0f, 0.0f, 0.0f };
    };

    struct RuntimeStats {
        double lastStepMilliseconds { 0.0 };
        double accumulator { 0.0 };
    };

    struct RenderPacket {
        const std::vector<glm::mat4>* nodeTransforms { nullptr };
        const std::vector<glm::mat4>* barTransforms { nullptr };
    };

    struct PendulumData {
        std::string name;
        std::vector<NodeState> nodes;
        glm::vec3 rootPosition { 0.0f, 0.0f, 0.0f };
        glm::vec3 rootVelocity { 0.0f, 0.0f, 0.0f };
        bool rootFrozen { true };
        bool running { false };
        bool paused { false };
        std::string nodeMeshName;
        std::string barMeshName;
        RuntimeStats stats;
        std::vector<glm::mat4> nodeTransforms;
        std::vector<glm::mat4> barTransforms;
    };

    struct PendulumSummary {
        std::size_t index { 0 };
        std::string name;
        bool running { false };
        bool paused { false };
        bool rootFrozen { true };
        double lastStepMilliseconds { 0.0 };
    };

    PendulumManager();

    std::size_t createPendulum(const std::string& name, std::size_t nodeCount);
    std::size_t createDemoPendulum();
    void removePendulum(std::size_t index);
    void clear();

    void resizeNodes(std::size_t index, std::size_t newCount);
    void setNodeMass(std::size_t index, std::size_t node, float mass);
    void setNodeLength(std::size_t index, std::size_t node, float length);
    void translateNode(std::size_t index, std::size_t node, const glm::vec3& delta);
    void setNodePosition(std::size_t index, std::size_t node, const glm::vec3& position);

    void setRootPosition(std::size_t index, const glm::vec3& position);
    void setRootFrozen(std::size_t index, bool frozen);
    void resetPendulum(std::size_t index);
    void setRenderMeshes(std::size_t index, std::string nodeMeshName, std::string barMeshName);

    void start(std::size_t index);
    void pause(std::size_t index);
    void togglePause(std::size_t index);
    void stop(std::size_t index);

    void setSettings(const Settings& settings);
    [[nodiscard]] const Settings& settings() const { return m_settings; }

    void setIntegrator(Integrator integrator);

    [[nodiscard]] bool hasPendulum(std::size_t index) const;
    [[nodiscard]] PendulumData* getPendulum(std::size_t index);
    [[nodiscard]] const PendulumData* getPendulum(std::size_t index) const;
    [[nodiscard]] std::vector<PendulumSummary> summaries() const;
    [[nodiscard]] std::size_t pendulumCount() const { return m_pendulums.size(); }
    [[nodiscard]] const std::string& nodeMeshName(std::size_t index) const;
    [[nodiscard]] const std::string& barMeshName(std::size_t index) const;

    [[nodiscard]] RenderPacket renderPacket(std::size_t index) const;

    void update(double deltaSeconds);
    void refreshTransforms(std::size_t index);

    void forEachPendulum(const std::function<void(const PendulumData&, std::size_t)>& visitor) const;
    void forEachPendulum(const std::function<void(PendulumData&, std::size_t)>& visitor);

private:
    void initialisePendulumState(PendulumData& pendulum);
    void updateTransforms(PendulumData& pendulum, const Settings& settings);
    void integratePendulum(PendulumData& pendulum, const Settings& settings, float dt);
    void integrateSemiImplicit(PendulumData& pendulum, const Settings& settings, float dt);
    void integrateRungeKutta4(PendulumData& pendulum, const Settings& settings, float dt);
    void satisfyConstraints(PendulumData& pendulum, const Settings& settings, float dt, const glm::vec3& previousRootPosition);

private:
    Settings m_settings;
    std::vector<PendulumData> m_pendulums;
    std::size_t m_nextId { 1 };
};
