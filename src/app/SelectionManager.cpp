// SPDX-License-Identifier: MIT

#include "app/SelectionManager.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/glm.hpp>
DISABLE_WARNINGS_POP()

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float kEpsilon = 1e-5f;
}

void SelectionManager::beginFrame()
{
    m_entries.clear();
    if (m_selection.has_value()) {
        // Keep selection alive but it will be refreshed when the matching entry is re-added.
        m_selection->bounds = {};
        m_selection->center = glm::vec3(0.0f);
        m_selection->radius = 0.0f;
    }
}

void SelectionManager::addSelectable(const SelectableEntry& entry)
{
    m_entries.push_back(entry);
    if (m_selection && m_selection->id == entry.id) {
        m_selection->shape = entry.shape;
        m_selection->bounds = entry.bounds;
        m_selection->center = entry.center;
        m_selection->radius = entry.radius;
        m_selection->name = entry.name;
    }
}

std::optional<SelectionManager::HitResult> SelectionManager::pick(const Ray& ray, float maxDistance) const
{
    float closest = maxDistance;
    std::optional<HitResult> best;

    for (const SelectableEntry& entry : m_entries) {
        std::optional<float> distance;
        switch (entry.shape) {
        case Shape::Aabb:
            distance = intersectAabb(ray, entry.bounds);
            break;
        case Shape::Sphere:
            distance = intersectSphere(ray, entry.center, entry.radius);
            break;
        }

        if (!distance.has_value())
            continue;
        const float d = *distance;
        if (d < 0.0f || d > closest)
            continue;

        closest = d;
        HitResult hit;
        hit.id = entry.id;
        hit.name = entry.name;
        hit.shape = entry.shape;
        hit.bounds = entry.bounds;
        hit.center = entry.center;
        hit.radius = entry.radius;
        hit.distance = d;
        hit.hitPoint = ray.origin + ray.direction * d;
        best = hit;
    }

    return best;
}

void SelectionManager::setSelection(const HitResult& hit)
{
    m_selection = hit;
    if (m_drag.active) {
        m_drag.currentPoint = hit.hitPoint;
        m_drag.planePoint = hit.hitPoint;
    }
}

void SelectionManager::clearSelection()
{
    m_selection.reset();
    endDrag();
}

bool SelectionManager::beginDrag(const Ray& ray, DragMode mode, const glm::vec3&)
{
    if (!m_selection)
        return false;

    const float rayLength = glm::length(ray.direction);
    glm::vec3 rayDir = rayLength > kEpsilon ? ray.direction / rayLength
                                            : glm::vec3(0.0f, 0.0f, -1.0f);

    m_drag.active = true;
    m_drag.mode = mode;
    m_drag.currentPoint.reset();

    if (mode == DragMode::Ground) {
        m_drag.planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        m_drag.planePoint = m_selection->hitPoint;
        m_drag.currentPoint = m_selection->hitPoint;
        updateDrag(ray);
        return true;
    }

    if (mode == DragMode::Vertical) {
        const glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 side = glm::cross(rayDir, worldUp);
        if (glm::length(side) < kEpsilon)
            side = glm::vec3(1.0f, 0.0f, 0.0f);

        glm::vec3 verticalNormal = glm::normalize(glm::cross(side, worldUp));
        m_drag.planeNormal = verticalNormal;
        m_drag.planePoint = m_selection->hitPoint;
        m_drag.currentPoint = m_selection->hitPoint;
        updateDrag(ray);
        return true;
    }

    m_drag.active = false;
    return false;
}

std::optional<glm::vec3> SelectionManager::updateDrag(const Ray& ray)
{
    if (!m_drag.active || !m_selection)
        return std::nullopt;

    const std::optional<glm::vec3> intersection = intersectDragPlane(ray);
    if (!intersection.has_value())
        return std::nullopt;

    glm::vec3 newPoint = *intersection;

    constexpr float kMaxDragDistance = 100.0f;
    glm::vec3 fromCamera = newPoint - ray.origin;
    const float distance = glm::length(fromCamera);
    if (distance > kMaxDragDistance && distance > kEpsilon) {
        fromCamera = fromCamera / distance * kMaxDragDistance;
        newPoint = ray.origin + fromCamera;
    }

    if (!m_drag.currentPoint.has_value()) {
        m_drag.currentPoint = newPoint;
        m_selection->hitPoint = newPoint;
        return glm::vec3(0.0f);
    }

    const glm::vec3 delta = newPoint - *m_drag.currentPoint;
    m_drag.currentPoint = newPoint;
    m_selection->hitPoint = newPoint;
    return delta;
}

void SelectionManager::endDrag()
{
    m_drag.active = false;
    m_drag.currentPoint.reset();
}

std::optional<float> SelectionManager::intersectAabb(const Ray& ray, const BoundingBox& bounds)
{
    const glm::vec3 invDir = 1.0f / ray.direction;
    const glm::vec3 t0 = (bounds.min - ray.origin) * invDir;
    const glm::vec3 t1 = (bounds.max - ray.origin) * invDir;

    const glm::vec3 tMinVec = glm::min(t0, t1);
    const glm::vec3 tMaxVec = glm::max(t0, t1);

    const float tMin = std::max(std::max(tMinVec.x, tMinVec.y), tMinVec.z);
    const float tMax = std::min(std::min(tMaxVec.x, tMaxVec.y), tMaxVec.z);

    if (tMax < 0.0f || tMin > tMax)
        return std::nullopt;

    return tMin >= 0.0f ? tMin : tMax;
}

std::optional<float> SelectionManager::intersectSphere(const Ray& ray, const glm::vec3& center, float radius)
{
    const glm::vec3 oc = ray.origin - center;
    const float a = glm::dot(ray.direction, ray.direction);
    const float b = 2.0f * glm::dot(oc, ray.direction);
    const float c = glm::dot(oc, oc) - radius * radius;
    const float discriminant = b * b - 4.0f * a * c;
    if (discriminant < 0.0f)
        return std::nullopt;

    const float sqrtDisc = std::sqrt(discriminant);
    const float invDenom = 0.5f / a;
    const float t0 = (-b - sqrtDisc) * invDenom;
    const float t1 = (-b + sqrtDisc) * invDenom;

    if (t0 >= 0.0f)
        return t0;
    if (t1 >= 0.0f)
        return t1;
    return std::nullopt;
}

std::optional<glm::vec3> SelectionManager::intersectDragPlane(const Ray& ray) const
{
    if (!m_drag.active)
        return std::nullopt;

    const float denom = glm::dot(m_drag.planeNormal, ray.direction);
    if (std::abs(denom) < kEpsilon)
        return std::nullopt;

    const float t = glm::dot(m_drag.planePoint - ray.origin, m_drag.planeNormal) / denom;
    if (t < 0.0f)
        return std::nullopt;

    return ray.origin + ray.direction * t;
}

