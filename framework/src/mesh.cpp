#include "mesh.h"
// Suppress warnings in third-party code.
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/gtx/norm.hpp>
#include <tinyobjloader/tiny_obj_loader.h>
DISABLE_WARNINGS_POP()
#include <algorithm>
#include <cassert>
#include <exception>
#include <iostream>
#include <numeric>
#include <span>
#include <stack>
#include <string>
#include <tuple>
#include <map>
#include <cmath>

static void centerAndScaleToUnitMesh(std::span<Mesh> meshes);

static glm::vec3 construct_vec3(const float* pFloats)
{
    return glm::vec3(pFloats[0], pFloats[1], pFloats[2]);
}

// https://stackoverflow.com/questions/2590677/how-do-i-combine-hash-values-in-c0x
template <class T>
static void hash_combine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

struct VertexHash {
    size_t operator()(const Vertex& v) const
    {
        size_t seed = 0;
        hash_combine(seed, v.position.x);
        hash_combine(seed, v.position.y);
        hash_combine(seed, v.position.z);
        hash_combine(seed, v.normal.x);
        hash_combine(seed, v.normal.y);
        hash_combine(seed, v.normal.z);
        hash_combine(seed, v.texCoord.s);
        hash_combine(seed, v.texCoord.t);
    hash_combine(seed, v.texCoord1.s);
    hash_combine(seed, v.texCoord1.t);
        hash_combine(seed, v.tangent.x);
        hash_combine(seed, v.tangent.y);
        hash_combine(seed, v.tangent.z);
        hash_combine(seed, v.tangent.w);
        return seed;
    }
};

static void computeTangents(Mesh& mesh)
{
    if (mesh.vertices.empty())
        return;

    std::vector<glm::vec3> accumulatedTangent(mesh.vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> accumulatedBitangent(mesh.vertices.size(), glm::vec3(0.0f));

    for (const glm::uvec3& tri : mesh.triangles) {
        const Vertex& v0 = mesh.vertices[tri.x];
        const Vertex& v1 = mesh.vertices[tri.y];
        const Vertex& v2 = mesh.vertices[tri.z];

        const glm::vec3 edge1 = v1.position - v0.position;
        const glm::vec3 edge2 = v2.position - v0.position;
        const glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
        const glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

        const float det = deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x;
        if (std::abs(det) < 1e-6f)
            continue;

        const float invDet = 1.0f / det;
        const glm::vec3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * invDet;
        const glm::vec3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * invDet;

        accumulatedTangent[tri.x] += tangent;
        accumulatedTangent[tri.y] += tangent;
        accumulatedTangent[tri.z] += tangent;

        accumulatedBitangent[tri.x] += bitangent;
        accumulatedBitangent[tri.y] += bitangent;
        accumulatedBitangent[tri.z] += bitangent;
    }

    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        Vertex& vertex = mesh.vertices[i];
        glm::vec3 T = accumulatedTangent[i];
        glm::vec3 B = accumulatedBitangent[i];
        T = T - vertex.normal * glm::dot(vertex.normal, T);
        if (glm::length2(T) < 1e-8f)
            T = glm::vec3(1.0f, 0.0f, 0.0f);
        else
            T = glm::normalize(T);
        float handedness = 1.0f;
        if (glm::length2(B) > 1e-8f)
            handedness = glm::sign(glm::dot(glm::cross(vertex.normal, T), B));
        vertex.tangent = glm::vec4(T, handedness);
    }
}

std::vector<Mesh> loadMesh(const std::filesystem::path& file, const LoadMeshSettings& settings)
{
    if (!std::filesystem::exists(file)) {
        std::cerr << "File " << file << " does not exist." << std::endl;
        throw std::exception();
    }

    const auto baseDir = file.parent_path();

    tinyobj::attrib_t inAttrib;
    std::vector<tinyobj::shape_t> inShapes;
    std::vector<tinyobj::material_t> inMaterials;

    std::string warn, error;
    bool ret = tinyobj::LoadObj(&inAttrib, &inShapes, &inMaterials, &warn, &error, file.string().c_str(), baseDir.string().c_str());
    if (!ret) {
        std::cerr << "Failed to load mesh " << file << std::endl;
        throw std::exception();
    }

    std::vector<Mesh> out;
    for (const auto& shape : inShapes) {
        assert(shape.mesh.indices.size() % 3 == 0);

        size_t startTriangle = 0;
        auto prevMaterialID = shape.mesh.material_ids[0];
        for (size_t endTriangle = 0; endTriangle < shape.mesh.indices.size() / 3; ++endTriangle) {
            // tinyobjloader does not automatically split the mesh into smaller sub meshes according to material so we have to do it ourselves.
            if (endTriangle == shape.mesh.indices.size() / 3 - 1)
                ++endTriangle; // End of the tinyobj.shape; write remaining mesh.
            else if (shape.mesh.material_ids[endTriangle] == prevMaterialID)
                continue;
            else
                prevMaterialID = shape.mesh.material_ids[endTriangle];

            Mesh mesh;
            using CacheKey = std::tuple<uint32_t, uint32_t, uint32_t>;
            std::map<CacheKey, uint32_t> vertexCache; // Map the index of a vertex as loaded by tinyobjloader to its index in the generated mesh
            for (size_t i = startTriangle * 3; i != endTriangle * 3; i += 3) {
                const glm::vec3 v0 = construct_vec3(&inAttrib.vertices[3 * shape.mesh.indices[i + 0].vertex_index]);
                const glm::vec3 v1 = construct_vec3(&inAttrib.vertices[3 * shape.mesh.indices[i + 1].vertex_index]);
                const glm::vec3 v2 = construct_vec3(&inAttrib.vertices[3 * shape.mesh.indices[i + 2].vertex_index]);
                const auto geometricNormal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

                // Load the triangle indices and lazily create the vertices.
                glm::uvec3 triangle;
                for (unsigned j = 0; j < 3; j++) {
                    const auto& tinyObjIndex = shape.mesh.indices[i + j];
                    Vertex vertex {
                        .position = construct_vec3(&inAttrib.vertices[3 * tinyObjIndex.vertex_index]),
                        .normal = glm::vec3(0),
                        .texCoord = glm::vec2(0),
                        .tangent = glm::vec4(0, 0, 0, 1)
                    };
                    if (tinyObjIndex.normal_index != -1 && !inAttrib.normals.empty())
                        vertex.normal = glm::vec3(inAttrib.normals[3 * tinyObjIndex.normal_index + 0], inAttrib.normals[3 * tinyObjIndex.normal_index + 1], inAttrib.normals[3 * tinyObjIndex.normal_index + 2]);
                    else
                        vertex.normal = geometricNormal;
                    if (tinyObjIndex.texcoord_index != -1 && !inAttrib.texcoords.empty())
                        vertex.texCoord = glm::vec2(inAttrib.texcoords[2 * tinyObjIndex.texcoord_index + 0], inAttrib.texcoords[2 * tinyObjIndex.texcoord_index + 1]);

                    const CacheKey cacheKey { tinyObjIndex.vertex_index, tinyObjIndex.normal_index, tinyObjIndex.texcoord_index };
                    if (auto iter = vertexCache.find(cacheKey); settings.cacheVertices && iter != std::end(vertexCache)) {
                        // Already visited this vertex? Reuse it!
                        triangle[j] = iter->second;
                    } else {
                        // New vertex? Create it and store it in the vertex cache.
                        vertexCache[cacheKey] = triangle[j] = (unsigned)mesh.vertices.size();
                        mesh.vertices.push_back(vertex);
                    }
                }
                mesh.triangles.push_back(triangle);
            }

            const auto materialID = shape.mesh.material_ids[startTriangle];
            if (materialID == -1) {
                mesh.material.kd = glm::vec3(1.0f);
                mesh.material.ks = glm::vec3(0.0f);
                mesh.material.shininess = 1.0f;
            } else {
                const auto& objMaterial = inMaterials[materialID];
                mesh.material.kd = construct_vec3(objMaterial.diffuse);
                if (!objMaterial.diffuse_texname.empty()) {
                    mesh.material.kdTexture = std::make_shared<Image>(baseDir / objMaterial.diffuse_texname);
                }
                mesh.material.ks = construct_vec3(objMaterial.specular);
                mesh.material.shininess = objMaterial.shininess;
                mesh.material.transparency = objMaterial.dissolve;
            }

            out.push_back(std::move(mesh));

            computeTangents(out.back());

            startTriangle = endTriangle;
        }
    }

    if (settings.normalizeVertexPositions)
        centerAndScaleToUnitMesh(out);

    return out;
}

static void centerAndScaleToUnitMesh(std::span<Mesh> meshes)
{
    std::vector<glm::vec3> positions;
    for (const auto& mesh : meshes)
        std::transform(std::begin(mesh.vertices), std::end(mesh.vertices),
            std::back_inserter(positions),
            [](const Vertex& v) { return v.position; });
    const glm::vec3 center = std::accumulate(std::begin(positions), std::end(positions), glm::vec3(0.0f)) / static_cast<float>(positions.size());
    float maxD = 0.0f;
    for (const glm::vec3& p : positions)
        maxD = std::max(glm::length(p - center), maxD);
    /*// REQUIRES A MODERN COMPILER
      const float maxD = std::transform_reduce(
              std::begin(vertices), std::end(vertices),
              0.0f,
              [](float lhs, float rhs) { return std::max(lhs, rhs); },
              [=](const Vertex& v) { return glm::length(v.pos - center); });*/

    for (auto& mesh : meshes) {
        std::transform(std::begin(mesh.vertices), std::end(mesh.vertices),
            std::begin(mesh.vertices), [=](Vertex v) {
                v.position = (v.position - center) / maxD;
                return v;
            });
    }
}

Mesh mergeMeshes(std::span<const Mesh> meshes)
{
    Mesh out;
    out.material = meshes[0].material;
    for (const auto& mesh : meshes) {
        const auto vertexOffset = out.vertices.size();
        out.vertices.resize(out.vertices.size() + mesh.vertices.size());
        std::copy(std::begin(mesh.vertices), std::end(mesh.vertices), std::begin(out.vertices) + vertexOffset);

        for (const auto& tri : mesh.triangles) {
            out.triangles.push_back(tri + (unsigned)vertexOffset);
        }
    }
    computeTangents(out);
    return out;
}

void meshFlipX(Mesh& mesh)
{
    for (auto& v : mesh.vertices) {
        v.position.x = -v.position.x;
        v.normal.x = -v.normal.x;
        v.tangent.x = -v.tangent.x;
    }
}

void meshFlipY(Mesh& mesh)
{
    for (auto& v : mesh.vertices) {
        v.position.y = -v.position.y;
        v.normal.y = -v.normal.y;
        v.tangent.y = -v.tangent.y;
    }
}

void meshFlipZ(Mesh& mesh)
{
    for (auto& v : mesh.vertices) {
        v.position.z = -v.position.z;
        v.normal.z = -v.normal.z;
        v.tangent.z = -v.tangent.z;
    }
}