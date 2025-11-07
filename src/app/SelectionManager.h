// SPDX-License-Identifier: MIT
#pragma once

#include "mesh/MeshInstance.h"

#include <framework/ray.h>

#include <glm/vec3.hpp>

#include <optional>
#include <string>
#include <vector>

class SelectionManager {
public:
    enum class Type {
        MeshInstance,
        PendulumNode,
        Light
    };

    enum class Shape {
        Aabb,
        Sphere
    };

    enum class DragMode {
        Ground,
        Vertical
    };

    struct Identifier {
        Type type { Type::MeshInstance };
        std::size_t primary { 0 };
        std::size_t secondary { 0 };

        [[nodiscard]] bool operator==(const Identifier& other) const
        {
            return type == other.type && primary == other.primary && secondary == other.secondary;
        }
    };

    struct SelectableEntry {
        Identifier id {};
        std::string name;
        Shape shape { Shape::Aabb };
        BoundingBox bounds {};
        glm::vec3 center { 0.0f };
        float radius { 0.0f };
    };

    struct HitResult {
        Identifier id {};
        std::string name;
        Shape shape { Shape::Aabb };
        BoundingBox bounds {};
        glm::vec3 center { 0.0f };
        float radius { 0.0f };
        float distance { 0.0f };
        glm::vec3 hitPoint { 0.0f };
    };

    void beginFrame();
    void addSelectable(const SelectableEntry& entry);

    [[nodiscard]] std::optional<HitResult> pick(const Ray& ray, float maxDistance) const;

    void setSelection(const HitResult& hit);
    void clearSelection();

    [[nodiscard]] const std::optional<HitResult>& selection() const { return m_selection; }

    bool beginDrag(const Ray& ray, DragMode mode, const glm::vec3& = glm::vec3(0.0f));
    [[nodiscard]] std::optional<glm::vec3> updateDrag(const Ray& ray);
    void endDrag();

    [[nodiscard]] bool dragging() const { return m_drag.active; }
    [[nodiscard]] const std::optional<HitResult>& dragSelection() const { return m_selection; }
    [[nodiscard]] const std::optional<glm::vec3>& dragPoint() const { return m_drag.currentPoint; }

private:
    struct DragState {
        bool active { false };
        DragMode mode { DragMode::Ground };
        glm::vec3 planeNormal { 0.0f, 1.0f, 0.0f };
        glm::vec3 planePoint { 0.0f };
        std::optional<glm::vec3> currentPoint;
    };

    static std::optional<float> intersectAabb(const Ray& ray, const BoundingBox& bounds);
    static std::optional<float> intersectSphere(const Ray& ray, const glm::vec3& center, float radius);
    [[nodiscard]] std::optional<glm::vec3> intersectDragPlane(const Ray& ray) const;

    std::vector<SelectableEntry> m_entries;
    std::optional<HitResult> m_selection;
    DragState m_drag;
};

