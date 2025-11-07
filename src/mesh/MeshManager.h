// SPDX-License-Identifier: MIT

#pragma once

#include "mesh/MeshInstance.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <optional>
#include <vector>

struct MeshData;

class MeshManager {
public:
    explicit MeshManager(const std::filesystem::path& meshDirectory, bool normalizeOnLoad = false);

    void refreshAvailableMeshes();

    [[nodiscard]] const std::vector<std::filesystem::path>& availableMeshes() const;

    bool loadMeshByIndex(std::size_t index);
    bool loadMeshFromPath(const std::filesystem::path& path);
    bool addMeshFromData(const std::filesystem::path& sourcePath, const std::vector<MeshData>& meshes);
    void removeMesh(std::size_t instanceIndex);

    [[nodiscard]] std::vector<MeshInstance>& instances();
    [[nodiscard]] const std::vector<MeshInstance>& instances() const;
    [[nodiscard]] std::vector<MeshInstance>& getInstances() { return m_instances; }
    [[nodiscard]] const std::vector<MeshInstance>& getInstances() const { return m_instances; }

    void setSelectedInstance(int index);
    [[nodiscard]] int selectedInstanceIndex() const;
    [[nodiscard]] MeshInstance* selectedInstance();
    [[nodiscard]] const MeshInstance* selectedInstance() const;

    [[nodiscard]] MeshInstance* findInstanceByName(const std::string& name);
    [[nodiscard]] const MeshInstance* findInstanceByName(const std::string& name) const;

    void addPrimitiveSphere(const std::string& name,
        float radius = 1.0f,
        int segments = 16,
        int rings = 16);

    void addPrimitiveCube(const std::string& name,
        float size = 1.0f);

    [[nodiscard]] std::optional<std::size_t> createSpherePrimitive(const std::string& name,
        float radius,
        int latitudeSegments,
        int longitudeSegments,
        const glm::vec3& baseColor,
        float roughness,
        float metallic);

    [[nodiscard]] std::optional<std::size_t> createQuadPrimitive(const std::string& name,
        float width,
        float height,
        const glm::vec3& baseColor,
        float roughness,
        float metallic,
        bool doubleSided = true);

    [[nodiscard]] std::optional<std::size_t> createBoxPrimitive(const std::string& name,
        const glm::vec3& extents,
        const glm::vec3& baseColor,
        float roughness,
        float metallic,
        bool doubleSided = true);

    [[nodiscard]] std::optional<std::size_t> pickInstance(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
    [[nodiscard]] BoundingBox computeWorldBounds(const MeshInstance& instance) const;

    void drawImGui();
    void drawImGuiPanel();

private:
    [[nodiscard]] bool isLoaded(const std::filesystem::path& path) const;

private:
    std::filesystem::path m_meshDirectory;
    bool m_normalizeOnLoad { false };

    std::vector<std::filesystem::path> m_availableMeshes;
    std::vector<MeshInstance> m_instances;
    int m_selectedInstance { -1 };
};
