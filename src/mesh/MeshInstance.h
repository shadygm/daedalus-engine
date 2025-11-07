// SPDX-License-Identifier: MIT

#pragma once

#include "mesh/mesh.h"
#include "rendering/Material.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <string>
#include <vector>

struct BoundingBox {
    glm::vec3 min { 0.0f };
    glm::vec3 max { 0.0f };
};

struct MeshDrawItem {
    GPUMesh geometry;
    RenderMaterial material;
    bool hasUVs { false };
    bool hasSecondaryUVs { false };
    bool hasTangents { false };
    glm::mat4 nodeTransform { 1.0f };
    BoundingBox bounds;

    MeshDrawItem(GPUMesh&& mesh,
        RenderMaterial material = {},
        glm::mat4 node = glm::mat4(1.0f),
        BoundingBox meshBounds = {},
        bool hasPrimaryUVs = false,
        bool hasSecondary = false,
        bool hasTangentData = false)
        : geometry(std::move(mesh))
        , material(std::move(material))
        , hasUVs(hasPrimaryUVs)
        , hasSecondaryUVs(hasSecondary)
        , hasTangents(hasTangentData)
        , nodeTransform(std::move(node))
        , bounds(std::move(meshBounds))
    {
    }

    MeshDrawItem(const MeshDrawItem&) = delete;
    MeshDrawItem& operator=(const MeshDrawItem&) = delete;
    MeshDrawItem(MeshDrawItem&&) = default;
    MeshDrawItem& operator=(MeshDrawItem&&) = default;
};

class MeshInstance {
public:
    MeshInstance(const std::filesystem::path& path, bool normalize = false);
    MeshInstance(const std::filesystem::path& path, std::vector<Mesh>&& meshes);
    MeshInstance(const std::filesystem::path& path, std::vector<MeshDrawItem>&& items);

    [[nodiscard]] const std::string& name() const;
    void setName(std::string name);
    [[nodiscard]] const std::filesystem::path& sourcePath() const;

    [[nodiscard]] const std::vector<MeshDrawItem>& drawItems() const;
    [[nodiscard]] std::vector<MeshDrawItem>& drawItems();

    [[nodiscard]] const glm::mat4& transform() const;
    void setTransform(const glm::mat4& transform);

    [[nodiscard]] const BoundingBox& localBounds() const;

private:
    void initializeFromMeshes(std::vector<Mesh>&& meshes);
    void initializeFromDrawItems(std::vector<MeshDrawItem>&& items);

private:
    std::string m_name;
    std::filesystem::path m_sourcePath;

    std::vector<MeshDrawItem> m_drawItems;
    glm::mat4 m_transform { 1.0f };
    BoundingBox m_localBounds;
};
