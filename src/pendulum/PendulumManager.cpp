// SPDX-License-Identifier: MIT

#include "pendulum/PendulumManager.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>
DISABLE_WARNINGS_POP()

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <limits>
#include <numeric>

namespace {
constexpr float kEpsilon = 1e-5f;

[[nodiscard]] glm::vec3 safeNormal(const glm::vec3& v)
{
    const float len2 = glm::length2(v);
    if (len2 < kEpsilon * kEpsilon)
        return glm::vec3(0.0f, -1.0f, 0.0f);
    return v / std::sqrt(len2);
}

}

PendulumManager::PendulumManager() = default;

std::size_t PendulumManager::createPendulum(const std::string& name, std::size_t nodeCount)
{
    PendulumData pendulum;
    pendulum.name = name.empty() ? fmt::format("Pendulum {}", m_nextId) : name;
    pendulum.nodes.resize(std::max<std::size_t>(1, nodeCount));
    initialisePendulumState(pendulum);
    m_pendulums.push_back(std::move(pendulum));
    const std::size_t createdIndex = m_pendulums.size() - 1;
    ++m_nextId;
    return createdIndex;
}

std::size_t PendulumManager::createDemoPendulum()
{
    constexpr std::size_t kDemoNodes = 6;
    const std::size_t index = createPendulum(fmt::format("Chaotic {}", m_nextId), kDemoNodes);
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return index;

    for (std::size_t i = 0; i < pendulum->nodes.size(); ++i) {
        const bool even = (i % 2) == 0;
        pendulum->nodes[i].mass = even ? 1.0f : 1.75f;
        pendulum->nodes[i].length = even ? 1.0f : 0.8f;
    }

    pendulum->rootPosition = glm::vec3(0.0f, 2.0f, 0.0f);
    initialisePendulumState(*pendulum);

    // Provide a small initial perturbation for chaotic behaviour.
    if (!pendulum->nodes.empty()) {
        const float phase = glm::pi<float>() * 0.25f;
        for (std::size_t i = 0; i < pendulum->nodes.size(); ++i) {
            const float angle = phase * static_cast<float>(i + 1);
            const float dx = std::sin(angle) * 0.05f;
            pendulum->nodes[i].position.x += dx;
            pendulum->nodes[i].previousPosition = pendulum->nodes[i].position;
        }
    }

    return index;
}

void PendulumManager::removePendulum(std::size_t index)
{
    if (!hasPendulum(index))
        return;
    m_pendulums.erase(m_pendulums.begin() + static_cast<std::ptrdiff_t>(index));
}

void PendulumManager::clear()
{
    m_pendulums.clear();
}

void PendulumManager::resizeNodes(std::size_t index, std::size_t newCount)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return;

    const std::size_t clampedCount = std::clamp<std::size_t>(newCount, 1, 64);
    pendulum->nodes.resize(clampedCount);
    initialisePendulumState(*pendulum);
}

void PendulumManager::setNodeMass(std::size_t index, std::size_t node, float mass)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum || node >= pendulum->nodes.size())
        return;
    pendulum->nodes[node].mass = std::max(0.01f, mass);
}

void PendulumManager::setNodeLength(std::size_t index, std::size_t node, float length)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum || node >= pendulum->nodes.size())
        return;
    pendulum->nodes[node].length = std::max(0.05f, length);
    initialisePendulumState(*pendulum);
}

void PendulumManager::translateNode(std::size_t index, std::size_t node, const glm::vec3& delta)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum || node >= pendulum->nodes.size())
        return;

    NodeState& movedNode = pendulum->nodes[node];
    movedNode.position += delta;
    movedNode.previousPosition += delta;
    movedNode.velocity = glm::vec3(0.0f);

    for (std::size_t i = node + 1; i < pendulum->nodes.size(); ++i) {
        NodeState& prev = pendulum->nodes[i - 1];
        NodeState& curr = pendulum->nodes[i];
        const glm::vec3 dir = safeNormal(curr.position - prev.position);
        curr.position = prev.position + dir * curr.length;
        curr.previousPosition = curr.position;
        curr.velocity = glm::vec3(0.0f);
    }

    for (std::size_t i = node; i-- > 0;) {
        NodeState& curr = pendulum->nodes[i];
        NodeState& next = pendulum->nodes[i + 1];
        const glm::vec3 dir = safeNormal(curr.position - next.position);
        curr.position = next.position + dir * curr.length;
        curr.previousPosition = curr.position;
        curr.velocity = glm::vec3(0.0f);
    }

    if (!pendulum->rootFrozen && !pendulum->nodes.empty()) {
        const glm::vec3 firstDir = safeNormal(pendulum->nodes.front().position - pendulum->rootPosition);
        pendulum->rootPosition = pendulum->nodes.front().position - firstDir * pendulum->nodes.front().length;
        pendulum->rootVelocity = glm::vec3(0.0f);
    }

    satisfyConstraints(*pendulum, m_settings, 1e-4f, pendulum->rootPosition);
    updateTransforms(*pendulum, m_settings);
}

void PendulumManager::setNodePosition(std::size_t index, std::size_t node, const glm::vec3& position)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum || node >= pendulum->nodes.size())
        return;

    NodeState& state = pendulum->nodes[node];
    const glm::vec3 delta = position - state.position;
    state.position = position;
    state.previousPosition += delta;
    state.velocity = glm::vec3(0.0f);
    updateTransforms(*pendulum, m_settings);
}

void PendulumManager::setRootPosition(std::size_t index, const glm::vec3& position)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return;
    const glm::vec3 delta = position - pendulum->rootPosition;
    pendulum->rootPosition = position;
    pendulum->rootVelocity = glm::vec3(0.0f);
    for (NodeState& node : pendulum->nodes) {
        node.position += delta;
        node.previousPosition += delta;
    }
    updateTransforms(*pendulum, m_settings);
}

void PendulumManager::setRootFrozen(std::size_t index, bool frozen)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return;
    pendulum->rootFrozen = frozen;
    if (frozen)
        pendulum->rootVelocity = glm::vec3(0.0f);
}

void PendulumManager::resetPendulum(std::size_t index)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return;
    initialisePendulumState(*pendulum);
    pendulum->stats.accumulator = 0.0;
}

void PendulumManager::setRenderMeshes(std::size_t index, std::string nodeMeshName, std::string barMeshName)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return;
    pendulum->nodeMeshName = std::move(nodeMeshName);
    pendulum->barMeshName = std::move(barMeshName);
}

void PendulumManager::start(std::size_t index)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return;
    pendulum->running = true;
    pendulum->paused = false;
}

void PendulumManager::pause(std::size_t index)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return;
    pendulum->paused = true;
}

void PendulumManager::togglePause(std::size_t index)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return;
    pendulum->paused = !pendulum->paused;
    pendulum->running = !pendulum->paused;
}

void PendulumManager::stop(std::size_t index)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return;
    pendulum->running = false;
    pendulum->paused = false;
}

void PendulumManager::setSettings(const Settings& settings)
{
    m_settings = settings;
}

void PendulumManager::setIntegrator(Integrator integrator)
{
    m_settings.integrator = integrator;
}

bool PendulumManager::hasPendulum(std::size_t index) const
{
    return index < m_pendulums.size();
}

PendulumManager::PendulumData* PendulumManager::getPendulum(std::size_t index)
{
    if (!hasPendulum(index))
        return nullptr;
    return &m_pendulums[index];
}

const PendulumManager::PendulumData* PendulumManager::getPendulum(std::size_t index) const
{
    if (!hasPendulum(index))
        return nullptr;
    return &m_pendulums[index];
}

const std::string& PendulumManager::nodeMeshName(std::size_t index) const
{
    static const std::string kEmpty;
    const PendulumData* pendulum = getPendulum(index);
    return pendulum ? pendulum->nodeMeshName : kEmpty;
}

const std::string& PendulumManager::barMeshName(std::size_t index) const
{
    static const std::string kEmpty;
    const PendulumData* pendulum = getPendulum(index);
    return pendulum ? pendulum->barMeshName : kEmpty;
}

std::vector<PendulumManager::PendulumSummary> PendulumManager::summaries() const
{
    std::vector<PendulumSummary> summaries;
    summaries.reserve(m_pendulums.size());
    for (std::size_t i = 0; i < m_pendulums.size(); ++i) {
        const PendulumData& pendulum = m_pendulums[i];
        summaries.push_back({
            .index = i,
            .name = pendulum.name,
            .running = pendulum.running,
            .paused = pendulum.paused,
            .rootFrozen = pendulum.rootFrozen,
            .lastStepMilliseconds = pendulum.stats.lastStepMilliseconds,
        });
    }
    return summaries;
}

PendulumManager::RenderPacket PendulumManager::renderPacket(std::size_t index) const
{
    RenderPacket packet;
    if (!hasPendulum(index))
        return packet;
    packet.nodeTransforms = &m_pendulums[index].nodeTransforms;
    packet.barTransforms = &m_pendulums[index].barTransforms;
    return packet;
}

void PendulumManager::update(double deltaSeconds)
{
    if (m_pendulums.empty())
        return;

    const Settings settings = m_settings;
    const float step = std::max(settings.fixedTimeStep, 1e-5f);
    const int substeps = std::max(1, settings.substeps);
    const float subDt = step / static_cast<float>(substeps);

    for (PendulumData& pendulum : m_pendulums) {
        if (pendulum.nodes.empty()) {
            updateTransforms(pendulum, settings);
            continue;
        }

        pendulum.stats.accumulator += deltaSeconds;
        const auto clampAccumulator = [&]() {
            const double maxAccum = step * 5.0;
            if (pendulum.stats.accumulator > maxAccum)
                pendulum.stats.accumulator = maxAccum;
        };
        clampAccumulator();

        if (!pendulum.running || pendulum.paused) {
            updateTransforms(pendulum, settings);
            continue;
        }

        while (pendulum.stats.accumulator >= step) {
            const auto begin = std::chrono::high_resolution_clock::now();
            for (int s = 0; s < substeps; ++s)
                integratePendulum(pendulum, settings, subDt);
            const auto end = std::chrono::high_resolution_clock::now();
            pendulum.stats.lastStepMilliseconds = std::chrono::duration<double, std::milli>(end - begin).count();
            pendulum.stats.accumulator -= step;
        }

        updateTransforms(pendulum, settings);
    }
}

void PendulumManager::forEachPendulum(const std::function<void(const PendulumData&, std::size_t)>& visitor) const
{
    for (std::size_t i = 0; i < m_pendulums.size(); ++i)
        visitor(m_pendulums[i], i);
}

void PendulumManager::forEachPendulum(const std::function<void(PendulumData&, std::size_t)>& visitor)
{
    for (std::size_t i = 0; i < m_pendulums.size(); ++i)
        visitor(m_pendulums[i], i);
}

void PendulumManager::refreshTransforms(std::size_t index)
{
    PendulumData* pendulum = getPendulum(index);
    if (!pendulum)
        return;
    updateTransforms(*pendulum, m_settings);
}

void PendulumManager::initialisePendulumState(PendulumData& pendulum)
{
    const std::size_t count = pendulum.nodes.size();
    if (count == 0)
        return;

    if (pendulum.nodeMeshName.empty())
        pendulum.nodeMeshName = "__pendulum_node__";
    if (pendulum.barMeshName.empty())
        pendulum.barMeshName = "__pendulum_bar__";

    float accumulated = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        NodeState& node = pendulum.nodes[i];
        node.mass = std::max(0.01f, node.mass);
        node.length = std::max(0.05f, node.length);
        accumulated += node.length;
        node.velocity = glm::vec3(0.0f);
        node.position = pendulum.rootPosition + glm::vec3(0.0f, -accumulated, 0.0f);
        node.previousPosition = node.position;
    }

    pendulum.rootVelocity = glm::vec3(0.0f);
    pendulum.nodeTransforms.resize(count);
    pendulum.barTransforms.resize(count);
    updateTransforms(pendulum, m_settings);
}

void PendulumManager::updateTransforms(PendulumData& pendulum, const Settings& settings)
{
    if (pendulum.nodes.empty())
        return;

    const float sphereScale = settings.nodeRadius;
    for (std::size_t i = 0; i < pendulum.nodes.size(); ++i) {
        const glm::vec3 position = pendulum.nodes[i].position;
        glm::mat4 model(1.0f);
        model = glm::translate(model, position);
        model = glm::scale(model, glm::vec3(sphereScale));
        pendulum.nodeTransforms[i] = model;
    }

    for (std::size_t i = 0; i < pendulum.nodes.size(); ++i) {
        const glm::vec3 start = (i == 0) ? pendulum.rootPosition : pendulum.nodes[i - 1].position;
        const glm::vec3 end = pendulum.nodes[i].position;
        glm::vec3 direction = end - start;
        float length = glm::length(direction);
        if (length < kEpsilon) {
            pendulum.barTransforms[i] = glm::mat4(1.0f);
            continue;
        }

        direction /= length;
        const glm::vec3 centre = (start + end) * 0.5f;
        const glm::quat rotation = glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), direction);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), centre);
        transform *= glm::toMat4(rotation);
        transform = glm::scale(transform, glm::vec3(settings.barThickness, length, settings.barThickness));
        pendulum.barTransforms[i] = transform;
    }
}

void PendulumManager::integratePendulum(PendulumData& pendulum, const Settings& settings, float dt)
{
    switch (settings.integrator) {
    case Integrator::SemiImplicitEuler:
        integrateSemiImplicit(pendulum, settings, dt);
        break;
    case Integrator::RungeKutta4:
        integrateRungeKutta4(pendulum, settings, dt);
        break;
    }
}

void PendulumManager::integrateSemiImplicit(PendulumData& pendulum, const Settings& settings, float dt)
{
    const glm::vec3 gravity(0.0f, -settings.gravity, 0.0f);
    const float damping = std::max(0.0f, settings.damping);
    const glm::vec3 rootPrev = pendulum.rootPosition;

    if (!pendulum.rootFrozen) {
        pendulum.rootVelocity += gravity * dt;
        pendulum.rootVelocity *= std::max(0.0f, 1.0f - damping * dt);
        pendulum.rootPosition += pendulum.rootVelocity * dt;
    } else {
        pendulum.rootVelocity = glm::vec3(0.0f);
    }

    for (NodeState& node : pendulum.nodes) {
        node.previousPosition = node.position;
        glm::vec3 acceleration = gravity - (damping * node.velocity);
        node.velocity += acceleration * dt;
        node.position += node.velocity * dt;
    }

    satisfyConstraints(pendulum, settings, dt, rootPrev);
}

void PendulumManager::integrateRungeKutta4(PendulumData& pendulum, const Settings& settings, float dt)
{
    const glm::vec3 gravity(0.0f, -settings.gravity, 0.0f);
    const float damping = std::max(0.0f, settings.damping);
    const glm::vec3 rootPrev = pendulum.rootPosition;

    auto accelerationFor = [&](const glm::vec3& velocity) {
        return gravity - (damping * velocity);
    };

    if (!pendulum.rootFrozen) {
        glm::vec3 v0 = pendulum.rootVelocity;
        glm::vec3 k1 = accelerationFor(v0);
        glm::vec3 k2 = accelerationFor(v0 + 0.5f * dt * k1);
        glm::vec3 k3 = accelerationFor(v0 + 0.5f * dt * k2);
        glm::vec3 k4 = accelerationFor(v0 + dt * k3);
        pendulum.rootVelocity += (dt / 6.0f) * (k1 + 2.0f * k2 + 2.0f * k3 + k4);
        glm::vec3 p1 = v0;
        glm::vec3 p2 = v0 + 0.5f * dt * k1;
        glm::vec3 p3 = v0 + 0.5f * dt * k2;
        glm::vec3 p4 = v0 + dt * k3;
        pendulum.rootPosition += (dt / 6.0f) * (p1 + 2.0f * p2 + 2.0f * p3 + p4);
    } else {
        pendulum.rootVelocity = glm::vec3(0.0f);
    }

    for (NodeState& node : pendulum.nodes) {
        node.previousPosition = node.position;
        const glm::vec3 v0 = node.velocity;
        const glm::vec3 k1 = accelerationFor(v0);
        const glm::vec3 k2 = accelerationFor(v0 + 0.5f * dt * k1);
        const glm::vec3 k3 = accelerationFor(v0 + 0.5f * dt * k2);
        const glm::vec3 k4 = accelerationFor(v0 + dt * k3);
        node.velocity += (dt / 6.0f) * (k1 + 2.0f * k2 + 2.0f * k3 + k4);
        const glm::vec3 p1 = v0;
        const glm::vec3 p2 = v0 + 0.5f * dt * k1;
        const glm::vec3 p3 = v0 + 0.5f * dt * k2;
        const glm::vec3 p4 = v0 + dt * k3;
        node.position += (dt / 6.0f) * (p1 + 2.0f * p2 + 2.0f * p3 + p4);
    }

    satisfyConstraints(pendulum, settings, dt, rootPrev);
}

void PendulumManager::satisfyConstraints(PendulumData& pendulum, const Settings& settings, float dt, const glm::vec3& previousRootPosition)
{
    (void)settings;
    const float rootMass = std::accumulate(pendulum.nodes.begin(), pendulum.nodes.end(), 0.0f, [](float acc, const NodeState& node) {
        return acc + node.mass;
    });

    glm::vec3 currentAnchor = pendulum.rootPosition;
    glm::vec3 anchorPrev = previousRootPosition;

    for (std::size_t i = 0; i < pendulum.nodes.size(); ++i) {
        NodeState& node = pendulum.nodes[i];
        const glm::vec3 targetBase = (i == 0) ? currentAnchor : pendulum.nodes[i - 1].position;
        glm::vec3 delta = node.position - targetBase;
        float distance = glm::length(delta);
        glm::vec3 direction = safeNormal(delta);
        if (distance < kEpsilon)
            distance = node.length;

        const glm::vec3 desiredPosition = targetBase + direction * node.length;
        glm::vec3 correction = desiredPosition - node.position;

        if (i == 0 && !pendulum.rootFrozen) {
            const float totalMass = rootMass + node.mass;
            if (totalMass > kEpsilon) {
                const float rootShare = node.mass / totalMass;
                const float nodeShare = rootMass / totalMass;
                pendulum.rootPosition -= correction * rootShare;
                node.position += correction * nodeShare;
            }
        } else {
            node.position = desiredPosition;
        }

        node.velocity = (node.position - node.previousPosition) / dt;
    }

    if (pendulum.rootFrozen) {
        pendulum.rootVelocity = glm::vec3(0.0f);
    } else {
        pendulum.rootVelocity = (pendulum.rootPosition - anchorPrev) / dt;
    }
}
