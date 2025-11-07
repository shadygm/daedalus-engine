// SPDX-License-Identifier: MIT

#include "mesh/MeshInstance.h"

#include <framework/mesh.h>

#include <glm/common.hpp>

#include <limits>
#include <utility>

namespace {

BoundingBox computeBounds(const Mesh& mesh)
{
    BoundingBox bounds;
    bounds.min = glm::vec3(std::numeric_limits<float>::max());
    bounds.max = glm::vec3(std::numeric_limits<float>::lowest());

    for (const Vertex& vertex : mesh.vertices) {
        bounds.min = glm::min(bounds.min, vertex.position);
        bounds.max = glm::max(bounds.max, vertex.position);
    }

    if (bounds.min.x == std::numeric_limits<float>::max()) {
        bounds.min = glm::vec3(0.0f);
        bounds.max = glm::vec3(0.0f);
    }
    return bounds;
}

void expandBounds(BoundingBox& aggregate, const BoundingBox& addition)
{
    aggregate.min = glm::min(aggregate.min, addition.min);
    aggregate.max = glm::max(aggregate.max, addition.max);
}

RenderMaterial makeRenderMaterialFrom(const Material& sourceMaterial)
{
    RenderMaterial material;
    material.usePBR = false;
    material.baseColor = sourceMaterial.kd;
    material.diffuseColor = sourceMaterial.kd;
    material.specularColor = sourceMaterial.ks;
    if (sourceMaterial.shininess > 0.0f) {
        material.roughness = glm::clamp(1.0f - (sourceMaterial.shininess / 256.0f), 0.04f, 1.0f);
        material.shininess = sourceMaterial.shininess;
    }

    material.translation = glm::vec3(0.0f);
    material.rotationEuler = glm::vec3(0.0f);
    material.scale = glm::vec3(1.0f);
    material.aoIntensity = material.ao;
    material.normalStrength = material.normalScale;
    material.emissiveIntensity = 1.0f;

    material.refreshTextureUsageFlags();
    return material;
}

}

MeshInstance::MeshInstance(const std::filesystem::path& path, bool normalize)
    : m_name(path.filename().string())
    , m_sourcePath(path)
{
    std::vector<Mesh> cpuMeshes = loadMesh(path, { .normalizeVertexPositions = normalize });
    initializeFromMeshes(std::move(cpuMeshes));
}

MeshInstance::MeshInstance(const std::filesystem::path& path, std::vector<Mesh>&& meshes)
    : m_name(path.filename().string())
    , m_sourcePath(path)
{
    initializeFromMeshes(std::move(meshes));
}

MeshInstance::MeshInstance(const std::filesystem::path& path, std::vector<MeshDrawItem>&& items)
    : m_name(path.filename().string())
    , m_sourcePath(path)
{
    initializeFromDrawItems(std::move(items));
}

const std::string& MeshInstance::name() const
{
    return m_name;
}

void MeshInstance::setName(std::string name)
{
    m_name = std::move(name);
}

const std::filesystem::path& MeshInstance::sourcePath() const
{
    return m_sourcePath;
}

const std::vector<MeshDrawItem>& MeshInstance::drawItems() const
{
    return m_drawItems;
}

std::vector<MeshDrawItem>& MeshInstance::drawItems()
{
    return m_drawItems;
}

const glm::mat4& MeshInstance::transform() const
{
    return m_transform;
}

void MeshInstance::setTransform(const glm::mat4& transform)
{
    m_transform = transform;
}

const BoundingBox& MeshInstance::localBounds() const
{
    return m_localBounds;
}

void MeshInstance::initializeFromMeshes(std::vector<Mesh>&& meshes)
{
    std::vector<MeshDrawItem> items;
    items.reserve(meshes.size());

    BoundingBox aggregate;
    aggregate.min = glm::vec3(std::numeric_limits<float>::max());
    aggregate.max = glm::vec3(std::numeric_limits<float>::lowest());

    for (Mesh& mesh : meshes) {
        BoundingBox meshBounds = computeBounds(mesh);
        expandBounds(aggregate, meshBounds);
        GPUMesh gpuMesh(mesh);
        const bool hasUVs = gpuMesh.hasTextureCoords();
        const bool hasSecondaryUVs = gpuMesh.hasSecondaryTextureCoords();
        const bool hasTangents = gpuMesh.hasTangents();
        RenderMaterial material = makeRenderMaterialFrom(mesh.material);
        items.emplace_back(std::move(gpuMesh), std::move(material), glm::mat4(1.0f), meshBounds, hasUVs, hasSecondaryUVs, hasTangents);
    }

    if (aggregate.min.x == std::numeric_limits<float>::max()) {
        aggregate.min = glm::vec3(0.0f);
        aggregate.max = glm::vec3(0.0f);
    }

    m_localBounds = aggregate;
    initializeFromDrawItems(std::move(items));
}

void MeshInstance::initializeFromDrawItems(std::vector<MeshDrawItem>&& items)
{
    m_drawItems = std::move(items);
    if (m_drawItems.empty()) {
        m_transform = glm::mat4(1.0f);
        m_localBounds = { glm::vec3(0.0f), glm::vec3(0.0f) };
        return;
    }

    BoundingBox aggregate = m_drawItems.front().bounds;
    for (std::size_t i = 1; i < m_drawItems.size(); ++i)
        expandBounds(aggregate, m_drawItems[i].bounds);

    m_localBounds = aggregate;
}
