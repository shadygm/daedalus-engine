// SPDX-License-Identifier: MIT

#include "mesh/MeshManager.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <imgui/imgui.h>
#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>
DISABLE_WARNINGS_POP()

#include "scene/ModelLoader.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <string>
#include <array>
#include <limits>
#include <system_error>
#include <iostream>

namespace {
std::shared_ptr<Texture> loadTexture(const MaterialTextureReference& reference, bool srgb, int forceChannels = 0)
{
    if (!reference.isValid())
        return nullptr;

    try {
        std::shared_ptr<Texture> tex;
        if (reference.path) {
            tex = std::make_shared<Texture>(*reference.path, srgb, reference.sampler, forceChannels);
            std::cout << "[Texture Loaded] " << reference.path->string()
                      << " ID=" << tex->id() << " sRGB=" << (srgb ? "yes" : "no");
            if (forceChannels > 0) {
                std::cout << " forceChannels=" << forceChannels;
            }
            std::cout << "\n";
        } else if (reference.embedded) {
            tex = std::make_shared<Texture>(*reference.embedded, srgb, reference.sampler);
            std::cout << "[Texture Embedded] ID=" << tex->id() << " sRGB=" << (srgb ? "yes" : "no") << "\n";
        }
        
        if (tex && tex->id() == 0) {
            std::cerr << "[ERROR] Texture created but has invalid ID=0\n";
            return nullptr;
        }
        
        return tex;
    } catch (const std::exception& ex) {
        std::cerr << "[ERROR] Texture load failed: " << ex.what() << "\n";
        if (reference.path) {
            std::cerr << "         Path: " << reference.path->string() << "\n";
        }
        return nullptr;
    }

    return nullptr;
}

void applyTextureMaps(RenderMaterial& material, const MaterialTextures& textures)
{
    material.albedoMap = loadTexture(textures.baseColor, true);
    material.metallicRoughnessMap = loadTexture(textures.metallicRoughness, false);
    // Force normal maps to load with 4 channels (RGBA) so alpha can be used for height in parallax mapping
    material.normalMap = loadTexture(textures.normal, false, 4);
    material.aoMap = loadTexture(textures.occlusion, false);
    material.emissiveMap = loadTexture(textures.emissive, true);
    // Optional dedicated height map (linear)
    material.heightMap = loadTexture(textures.height, false);

    // If the source material doesn't reference a height map, try a specific known path first,
    // then auto-discover a sibling file with common displacement keywords.
    if (!material.heightMap) {
        // 1) Forced specific displacement map (exact path requested)
        try {
            // Try relative to working directory
            std::filesystem::path forcedDisp = std::filesystem::path("resources/brick_wall/textures/Ground002_4K-JPG_Displacement.jpg");
            // Also try an absolute path known for this workspace
            if (!std::filesystem::exists(forcedDisp))
                forcedDisp = std::filesystem::path("/home/migster232/uni/3DCGA/assignment_2/computer-graphics-course/assignment_2/resources/brick_wall/textures/Ground002_4K-JPG_Displacement.jpg");
            if (std::filesystem::exists(forcedDisp)) {
                TextureSamplerSettings sampler; // default
                auto tex = std::make_shared<Texture>(forcedDisp, false, sampler);
                if (tex && tex->id() != 0) {
                    std::cout << "[Height] Using forced displacement: " << forcedDisp.string() << " ID=" << tex->id() << "\n";
                    material.heightMap = std::move(tex);
                    // Align height UVs/transform with the normal map by default
                    material.heightUV = textures.normal.texCoord;
                    material.heightUVTransform.offset = textures.normal.uvOffset;
                    material.heightUVTransform.scale = textures.normal.uvScale;
                    material.heightUVTransform.rotation = textures.normal.uvRotation;
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "[Height] Forced displacement load failed: " << ex.what() << "\n";
        }

        // 2) Auto-discover sibling displacement if still not set
        auto toLower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        };

        auto findSiblingDisplacement = [&](const MaterialTextureReference& ref) -> std::optional<std::filesystem::path> {
            if (!ref.path)
                return std::nullopt;
            const std::filesystem::path& p = *ref.path;
            const std::filesystem::path dir = p.parent_path();
            if (!std::filesystem::exists(dir))
                return std::nullopt;

            // Try to infer a prefix from the reference filename (up to the last underscore),
            // so we prefer files from the same texture set.
            const std::string baseName = p.filename().string();
            std::string prefix = baseName;
            const std::size_t us = baseName.find_last_of('_');
            if (us != std::string::npos)
                prefix = baseName.substr(0, us + 1); // include trailing underscore
            const std::string lowerPrefix = toLower(prefix);

            std::filesystem::path best;
            int bestScore = -1; // 2 = exact prefix + displacement; 1 = displacement token; 0 = any height-ish token

            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (!entry.is_regular_file())
                    continue;
                const std::string name = entry.path().filename().string();
                const std::string lower = toLower(name);
                const bool tokenDisp = lower.find("displacement") != std::string::npos || lower.find("disp") != std::string::npos;
                const bool tokenHeight = lower.find("height") != std::string::npos;
                if (!tokenDisp && !tokenHeight)
                    continue;

                int score = tokenDisp ? 1 : 0;
                if (!lowerPrefix.empty() && lower.rfind(lowerPrefix, 0) == 0)
                    score += 1; // prefer same-set

                if (score > bestScore) {
                    bestScore = score;
                    best = entry.path();
                }
            }

            if (bestScore >= 0)
                return best;
            return std::nullopt;
        };

        // Prefer searching relative to the normal map, then albedo.
        std::optional<std::filesystem::path> candidate = findSiblingDisplacement(textures.normal);
        if (!candidate)
            candidate = findSiblingDisplacement(textures.baseColor);

        if (!material.heightMap && candidate) {
            try {
                TextureSamplerSettings sampler; // default
                // Load linear (non-sRGB)
                auto tex = std::make_shared<Texture>(*candidate, false, sampler);
                if (tex && tex->id() != 0) {
                    std::cout << "[Auto-Height] Using sibling displacement: " << candidate->string() << " ID=" << tex->id() << "\n";
                    material.heightMap = std::move(tex);
                    // Align height UVs/transform with the normal map by default
                    material.heightUV = textures.normal.texCoord;
                    material.heightUVTransform.offset = textures.normal.uvOffset;
                    material.heightUVTransform.scale = textures.normal.uvScale;
                    material.heightUVTransform.rotation = textures.normal.uvRotation;
                }
            } catch (const std::exception& ex) {
                std::cerr << "[Auto-Height] Failed to load sibling displacement: " << ex.what() << "\n";
            }
        }
    }

    material.normalScale = textures.normal.scale;
    material.normalStrength = material.normalScale;
    material.ao = textures.occlusion.scale;
    material.aoIntensity = material.ao;
    material.occlusionFromMetallicRoughness = !textures.occlusion.isValid() && textures.metallicRoughness.isValid();
    material.refreshTextureUsageFlags();
}

std::optional<float> rayIntersectsAabb(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const BoundingBox& bounds)
{
    const glm::vec3 invDir = 1.0f / rayDirection;
    const glm::vec3 t0 = (bounds.min - rayOrigin) * invDir;
    const glm::vec3 t1 = (bounds.max - rayOrigin) * invDir;

    const glm::vec3 tMinVec = glm::min(t0, t1);
    const glm::vec3 tMaxVec = glm::max(t0, t1);

    const float tMin = glm::max(glm::max(tMinVec.x, tMinVec.y), tMinVec.z);
    const float tMax = glm::min(glm::min(tMaxVec.x, tMaxVec.y), tMaxVec.z);

    if (tMax < 0.0f || tMin > tMax)
        return std::nullopt;

    return tMin >= 0.0f ? tMin : tMax;
}

BoundingBox transformBounds(const BoundingBox& localBounds, const glm::mat4& transform)
{
    const glm::vec3 min = localBounds.min;
    const glm::vec3 max = localBounds.max;

    const glm::vec3 corners[8] = {
        { min.x, min.y, min.z },
        { max.x, min.y, min.z },
        { min.x, max.y, min.z },
        { max.x, max.y, min.z },
        { min.x, min.y, max.z },
        { max.x, min.y, max.z },
        { min.x, max.y, max.z },
        { max.x, max.y, max.z }
    };

    glm::vec3 worldMin(std::numeric_limits<float>::max());
    glm::vec3 worldMax(std::numeric_limits<float>::lowest());

    for (const glm::vec3& corner : corners) {
        const glm::vec4 world = transform * glm::vec4(corner, 1.0f);
        worldMin = glm::min(worldMin, glm::vec3(world));
        worldMax = glm::max(worldMax, glm::vec3(world));
    }

    return { worldMin, worldMax };
}

Mesh meshFromData(const MeshData& data)
{
    Mesh mesh;
    mesh.vertices.reserve(data.positions.size());

    const bool hasNormals = data.normals.size() == data.positions.size();
    const bool hasTangents = data.tangents.size() == data.positions.size();
    const bool hasTexCoords = data.texCoords0.size() == data.positions.size();
    const bool hasTexCoords1 = data.texCoords1.size() == data.positions.size();

    for (std::size_t i = 0; i < data.positions.size(); ++i) {
        Vertex vertex {};
        vertex.position = data.positions[i];
        vertex.normal = hasNormals ? data.normals[i] : glm::vec3(0.0f, 1.0f, 0.0f);
    vertex.texCoord = hasTexCoords ? data.texCoords0[i] : glm::vec2(0.0f);
    vertex.texCoord1 = hasTexCoords1 ? data.texCoords1[i] : glm::vec2(0.0f);
    vertex.tangent = hasTangents ? data.tangents[i] : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        mesh.vertices.push_back(vertex);
    }

    const std::size_t triangleCount = data.indices.size() / 3;
    mesh.triangles.reserve(triangleCount);
    for (std::size_t i = 0; i < triangleCount; ++i) {
        const unsigned int i0 = data.indices[i * 3 + 0];
        const unsigned int i1 = data.indices[i * 3 + 1];
        const unsigned int i2 = data.indices[i * 3 + 2];
        mesh.triangles.emplace_back(i0, i1, i2);
    }

    mesh.material.kd = data.material.diffuseColor;
    mesh.material.ks = data.material.specularColor;
    mesh.material.shininess = glm::max(data.material.shininess, 1.0f);

    return mesh;
}

BoundingBox boundsFromData(const MeshData& data)
{
    BoundingBox bounds;
    bounds.min = glm::vec3(std::numeric_limits<float>::max());
    bounds.max = glm::vec3(std::numeric_limits<float>::lowest());

    for (const glm::vec3& pos : data.positions) {
        bounds.min = glm::min(bounds.min, pos);
        bounds.max = glm::max(bounds.max, pos);
    }

    if (bounds.min.x == std::numeric_limits<float>::max()) {
        bounds.min = glm::vec3(0.0f);
        bounds.max = glm::vec3(0.0f);
    }
    return bounds;
}

void configurePrimitiveMaterial(RenderMaterial& material, const glm::vec3& color, float roughness, float metallic, bool doubleSided)
{
    material.usePBR = true;
    material.unlit = false;
    material.doubleSided = doubleSided;
    material.baseColor = color;
    material.diffuseColor = color;
    material.specularColor = glm::vec3(0.04f);
    material.roughness = glm::clamp(roughness, 0.0f, 1.0f);
    material.metallic = glm::clamp(metallic, 0.0f, 1.0f);
    material.emissive = glm::vec3(0.0f);
    material.emissiveIntensity = 1.0f;
    material.opacity = 1.0f;
    material.alphaMode = AlphaMode::Opaque;
    material.refreshTextureUsageFlags();
}

MeshData createSphereMeshData(float radius,
    int latitudeSegments,
    int longitudeSegments,
    const glm::vec3& color,
    float roughness,
    float metallic)
{
    MeshData data;
    const int latSeg = std::max(latitudeSegments, 3);
    const int lonSeg = std::max(longitudeSegments, 3);
    const int columnCount = lonSeg + 1;

    data.positions.reserve(static_cast<std::size_t>((latSeg + 1) * columnCount));
    data.normals.reserve(data.positions.capacity());
    data.texCoords0.reserve(data.positions.capacity());

    for (int lat = 0; lat <= latSeg; ++lat) {
        const float v = static_cast<float>(lat) / static_cast<float>(latSeg);
        const float theta = v * glm::pi<float>();
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (int lon = 0; lon <= lonSeg; ++lon) {
            const float u = static_cast<float>(lon) / static_cast<float>(lonSeg);
            const float phi = u * glm::two_pi<float>();
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            glm::vec3 normal(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
            if (glm::length2(normal) > 0.0f)
                normal = glm::normalize(normal);
            glm::vec3 position = normal * radius;

            data.positions.push_back(position);
            data.normals.push_back(normal);
            data.texCoords0.emplace_back(u, 1.0f - v);
        }
    }

    for (int lat = 0; lat < latSeg; ++lat) {
        for (int lon = 0; lon < lonSeg; ++lon) {
            const int first = lat * columnCount + lon;
            const int second = first + columnCount;

            data.indices.push_back(static_cast<unsigned int>(first));
            data.indices.push_back(static_cast<unsigned int>(second));
            data.indices.push_back(static_cast<unsigned int>(first + 1));

            data.indices.push_back(static_cast<unsigned int>(second));
            data.indices.push_back(static_cast<unsigned int>(second + 1));
            data.indices.push_back(static_cast<unsigned int>(first + 1));
        }
    }

    data.hasUVs = true;
    data.hasTangents = false;
    data.nodeTransform = glm::mat4(1.0f);

    configurePrimitiveMaterial(data.material, color, roughness, metallic, false);
    return data;
}

MeshData createQuadMeshData(float width,
    float height,
    const glm::vec3& color,
    float roughness,
    float metallic,
    bool doubleSided)
{
    MeshData data;
    const float hw = width * 0.5f;
    const float hh = height * 0.5f;

    const std::array<glm::vec3, 4> positions {
        glm::vec3(-hw, -hh, 0.0f),
        glm::vec3(hw, -hh, 0.0f),
        glm::vec3(hw, hh, 0.0f),
        glm::vec3(-hw, hh, 0.0f)
    };
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    const std::array<glm::vec2, 4> uvs {
        glm::vec2(0.0f, 0.0f),
        glm::vec2(1.0f, 0.0f),
        glm::vec2(1.0f, 1.0f),
        glm::vec2(0.0f, 1.0f)
    };

    for (std::size_t i = 0; i < positions.size(); ++i) {
        data.positions.push_back(positions[i]);
        data.normals.push_back(normal);
        data.texCoords0.push_back(uvs[i]);
    }

    data.indices = { 0, 1, 2, 0, 2, 3 };
    if (doubleSided) {
        data.indices.insert(data.indices.end(), { 0, 2, 1, 0, 3, 2 });
    }

    data.hasUVs = true;
    data.hasTangents = false;
    data.nodeTransform = glm::mat4(1.0f);
    configurePrimitiveMaterial(data.material, color, roughness, metallic, doubleSided);
    return data;
}

MeshData createBoxMeshData(const glm::vec3& extents,
    const glm::vec3& color,
    float roughness,
    float metallic,
    bool doubleSided)
{
    MeshData data;
    const glm::vec3 h = extents * 0.5f;

    struct Face {
        glm::vec3 normal;
        glm::vec3 corners[4];
    };

    const std::array<Face, 6> faces {
        Face { glm::vec3(0.0f, 0.0f, 1.0f), { glm::vec3(-h.x, -h.y, h.z), glm::vec3(h.x, -h.y, h.z), glm::vec3(h.x, h.y, h.z), glm::vec3(-h.x, h.y, h.z) } },
        Face { glm::vec3(0.0f, 0.0f, -1.0f), { glm::vec3(h.x, -h.y, -h.z), glm::vec3(-h.x, -h.y, -h.z), glm::vec3(-h.x, h.y, -h.z), glm::vec3(h.x, h.y, -h.z) } },
        Face { glm::vec3(0.0f, 1.0f, 0.0f), { glm::vec3(-h.x, h.y, h.z), glm::vec3(h.x, h.y, h.z), glm::vec3(h.x, h.y, -h.z), glm::vec3(-h.x, h.y, -h.z) } },
        Face { glm::vec3(0.0f, -1.0f, 0.0f), { glm::vec3(-h.x, -h.y, -h.z), glm::vec3(h.x, -h.y, -h.z), glm::vec3(h.x, -h.y, h.z), glm::vec3(-h.x, -h.y, h.z) } },
        Face { glm::vec3(1.0f, 0.0f, 0.0f), { glm::vec3(h.x, -h.y, h.z), glm::vec3(h.x, -h.y, -h.z), glm::vec3(h.x, h.y, -h.z), glm::vec3(h.x, h.y, h.z) } },
        Face { glm::vec3(-1.0f, 0.0f, 0.0f), { glm::vec3(-h.x, -h.y, -h.z), glm::vec3(-h.x, -h.y, h.z), glm::vec3(-h.x, h.y, h.z), glm::vec3(-h.x, h.y, -h.z) } }
    };

    const std::array<glm::vec2, 4> uvs {
        glm::vec2(0.0f, 0.0f),
        glm::vec2(1.0f, 0.0f),
        glm::vec2(1.0f, 1.0f),
        glm::vec2(0.0f, 1.0f)
    };

    for (const Face& face : faces) {
        const std::size_t base = data.positions.size();
        for (int i = 0; i < 4; ++i) {
            data.positions.push_back(face.corners[i]);
            data.normals.push_back(face.normal);
            data.texCoords0.push_back(uvs[i]);
        }
        data.indices.push_back(static_cast<unsigned int>(base + 0));
        data.indices.push_back(static_cast<unsigned int>(base + 1));
        data.indices.push_back(static_cast<unsigned int>(base + 2));
        data.indices.push_back(static_cast<unsigned int>(base + 0));
        data.indices.push_back(static_cast<unsigned int>(base + 2));
        data.indices.push_back(static_cast<unsigned int>(base + 3));
        if (doubleSided) {
            data.indices.push_back(static_cast<unsigned int>(base + 0));
            data.indices.push_back(static_cast<unsigned int>(base + 2));
            data.indices.push_back(static_cast<unsigned int>(base + 1));
            data.indices.push_back(static_cast<unsigned int>(base + 0));
            data.indices.push_back(static_cast<unsigned int>(base + 3));
            data.indices.push_back(static_cast<unsigned int>(base + 2));
        }
    }

    data.hasUVs = true;
    data.hasTangents = false;
    data.nodeTransform = glm::mat4(1.0f);
    configurePrimitiveMaterial(data.material, color, roughness, metallic, doubleSided);
    return data;
}
}

MeshManager::MeshManager(const std::filesystem::path& meshDirectory, bool normalizeOnLoad)
    : m_meshDirectory(meshDirectory)
    , m_normalizeOnLoad(normalizeOnLoad)
{
    refreshAvailableMeshes();
}

void MeshManager::refreshAvailableMeshes()
{
    m_availableMeshes.clear();
    if (!std::filesystem::exists(m_meshDirectory))
        return;

    for (const auto& entry : std::filesystem::directory_iterator(m_meshDirectory)) {
        if (!entry.is_regular_file())
            continue;
        const auto ext = entry.path().extension();
        if (ext == ".obj" || ext == ".OBJ")
            m_availableMeshes.push_back(entry.path());
    }

    std::sort(m_availableMeshes.begin(), m_availableMeshes.end());
}

const std::vector<std::filesystem::path>& MeshManager::availableMeshes() const
{
    return m_availableMeshes;
}

bool MeshManager::loadMeshByIndex(std::size_t index)
{
    if (index >= m_availableMeshes.size())
        return false;
    return loadMeshFromPath(m_availableMeshes[index]);
}

bool MeshManager::loadMeshFromPath(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path))
        return false;

    MeshInstance instance(path, m_normalizeOnLoad);

    // Append suffix to differentiate instances originating from the same file
    if (isLoaded(path)) {
        int duplicateCount = 1;
        const std::string baseName = instance.name();
        std::string candidateName;
        do {
            candidateName = baseName + " (" + std::to_string(++duplicateCount) + ")";
        } while (std::any_of(m_instances.begin(), m_instances.end(), [&](const MeshInstance& other) { return other.name() == candidateName; }));
        instance.setName(candidateName);
    }

    m_instances.push_back(std::move(instance));
    m_selectedInstance = static_cast<int>(m_instances.size() - 1);
    return true;
}

bool MeshManager::addMeshFromData(const std::filesystem::path& sourcePath, const std::vector<MeshData>& meshes)
{
    if (meshes.empty())
        return false;

    std::vector<MeshDrawItem> items;
    items.reserve(meshes.size());

    for (const MeshData& data : meshes) {
        Mesh cpuMesh = meshFromData(data);
        BoundingBox bounds = boundsFromData(data);
        GPUMesh gpuMesh(cpuMesh);
        RenderMaterial material = data.material;
        applyTextureMaps(material, data.textures);
        material.refreshTextureUsageFlags();
        const bool hasUVs = data.hasUVs;
        const bool hasSecondary = data.hasSecondaryUVs;
        const bool hasTangents = data.hasTangents;
        items.emplace_back(std::move(gpuMesh), std::move(material), data.nodeTransform, bounds, hasUVs, hasSecondary, hasTangents);
    }

    MeshInstance instance(sourcePath, std::move(items));

    if (isLoaded(sourcePath)) {
        int duplicateCount = 1;
        const std::string baseName = instance.name();
        std::string candidateName;
        do {
            candidateName = baseName + " (" + std::to_string(++duplicateCount) + ")";
        } while (std::any_of(m_instances.begin(), m_instances.end(), [&](const MeshInstance& other) { return other.name() == candidateName; }));
        instance.setName(candidateName);
    }

    m_instances.push_back(std::move(instance));
    m_selectedInstance = static_cast<int>(m_instances.size() - 1);
    return true;
}

void MeshManager::removeMesh(std::size_t instanceIndex)
{
    if (instanceIndex >= m_instances.size())
        return;

    m_instances.erase(m_instances.begin() + static_cast<std::vector<MeshInstance>::difference_type>(instanceIndex));
    if (m_instances.empty()) {
        m_selectedInstance = -1;
    } else if (m_selectedInstance >= static_cast<int>(m_instances.size())) {
        m_selectedInstance = static_cast<int>(m_instances.size()) - 1;
    }
}

void MeshManager::drawImGui()
{
    if (ImGui::Begin("Mesh Manager")) {
        drawImGuiPanel();
    }
    ImGui::End();
}

void MeshManager::drawImGuiPanel()
{

    ImGui::Separator();

    ImGui::TextUnformatted("Add Primitive");
    static int primitiveSelection = 0;
    static constexpr const char* primitiveLabels[] = { "Sphere", "Cube" };
    ImGui::Combo("Primitive Type", &primitiveSelection, primitiveLabels, IM_ARRAYSIZE(primitiveLabels));
    ImGui::SameLine();
    if (ImGui::Button("Add Primitive")) {
        std::optional<std::size_t> newIndex;
        if (primitiveSelection == 0)
            newIndex = createSpherePrimitive("Sphere", 1.0f, 16, 16, glm::vec3(0.75f), 0.5f, 0.0f);
        else
            newIndex = createBoxPrimitive("Cube", glm::vec3(1.0f), glm::vec3(0.65f), 0.5f, 0.0f, false);

        if (newIndex)
            setSelectedInstance(static_cast<int>(*newIndex));
    }

    ImGui::Separator();

    if (ImGui::BeginListBox("Instances")) {
        for (std::size_t i = 0; i < m_instances.size(); ++i) {
            const bool selected = m_selectedInstance == static_cast<int>(i);
            if (ImGui::Selectable(m_instances[i].name().c_str(), selected))
                setSelectedInstance(static_cast<int>(i));
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    if (MeshInstance* instance = selectedInstance()) {
        ImGui::Separator();
        ImGui::Text("Selected: %s", instance->name().c_str());
        if (ImGui::Button("Remove Instance"))
            removeMesh(static_cast<std::size_t>(m_selectedInstance));
    } else {
        ImGui::TextDisabled("No mesh instance selected.");
    }
}

std::vector<MeshInstance>& MeshManager::instances()
{
    return m_instances;
}

const std::vector<MeshInstance>& MeshManager::instances() const
{
    return m_instances;
}

void MeshManager::setSelectedInstance(int index)
{
    if (index < 0 || index >= static_cast<int>(m_instances.size())) {
        m_selectedInstance = -1;
    } else {
        m_selectedInstance = index;
    }
}

int MeshManager::selectedInstanceIndex() const
{
    return m_selectedInstance;
}

MeshInstance* MeshManager::selectedInstance()
{
    if (m_selectedInstance < 0 || m_selectedInstance >= static_cast<int>(m_instances.size()))
        return nullptr;
    return &m_instances[static_cast<std::size_t>(m_selectedInstance)];
}

const MeshInstance* MeshManager::selectedInstance() const
{
    if (m_selectedInstance < 0 || m_selectedInstance >= static_cast<int>(m_instances.size()))
        return nullptr;
    return &m_instances[static_cast<std::size_t>(m_selectedInstance)];
}

MeshInstance* MeshManager::findInstanceByName(const std::string& name)
{
    const auto it = std::find_if(m_instances.begin(), m_instances.end(), [&](const MeshInstance& instance) {
        return instance.name() == name;
    });
    if (it == m_instances.end())
        return nullptr;
    return &*it;
}

const MeshInstance* MeshManager::findInstanceByName(const std::string& name) const
{
    const auto it = std::find_if(m_instances.begin(), m_instances.end(), [&](const MeshInstance& instance) {
        return instance.name() == name;
    });
    if (it == m_instances.end())
        return nullptr;
    return &*it;
}

void MeshManager::addPrimitiveSphere(const std::string& name, float radius, int segments, int rings)
{
    const float safeRadius = std::max(radius, 0.01f);
    const int latSeg = std::max(rings, 3);
    const int lonSeg = std::max(segments, 3);
    const glm::vec3 baseColor(0.75f);

    if (!name.empty()) {
        for (std::size_t i = 0; i < m_instances.size(); ++i) {
            if (m_instances[i].name() == name) {
                removeMesh(i);
                break;
            }
        }
    }

    const std::string finalName = name.empty() ? "Sphere" : name;
    if (auto index = createSpherePrimitive(finalName, safeRadius, latSeg, lonSeg, baseColor, 0.5f, 0.0f)) {
        if (!name.empty() && *index < m_instances.size())
            m_instances[*index].setName(name);
    }
}

void MeshManager::addPrimitiveCube(const std::string& name, float size)
{
    const glm::vec3 extents = glm::max(glm::vec3(size), glm::vec3(0.01f));
    const glm::vec3 baseColor(0.65f);

    if (!name.empty()) {
        for (std::size_t i = 0; i < m_instances.size(); ++i) {
            if (m_instances[i].name() == name) {
                removeMesh(i);
                break;
            }
        }
    }

    const std::string finalName = name.empty() ? "Cube" : name;
    if (auto index = createBoxPrimitive(finalName, extents, baseColor, 0.5f, 0.0f, false)) {
        if (!name.empty() && *index < m_instances.size())
            m_instances[*index].setName(name);
    }
}

std::optional<std::size_t> MeshManager::pickInstance(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const
{
    float closestDistance = std::numeric_limits<float>::max();
    std::optional<std::size_t> closestIndex;

    for (std::size_t i = 0; i < m_instances.size(); ++i) {
        const MeshInstance& instance = m_instances[i];
        const BoundingBox worldBounds = computeWorldBounds(instance);
        if (auto distance = rayIntersectsAabb(rayOrigin, rayDirection, worldBounds)) {
            if (*distance < closestDistance) {
                closestDistance = *distance;
                closestIndex = i;
            }
        }
    }

    return closestIndex;
}

std::optional<std::size_t> MeshManager::createSpherePrimitive(const std::string& name,
    float radius,
    int latitudeSegments,
    int longitudeSegments,
    const glm::vec3& baseColor,
    float roughness,
    float metallic)
{
    const std::string finalName = name.empty() ? "Sphere" : name;
    MeshData data = createSphereMeshData(std::max(radius, 0.01f), latitudeSegments, longitudeSegments, baseColor, roughness, metallic);
    std::vector<MeshData> meshes;
    meshes.push_back(std::move(data));
    const std::filesystem::path source = std::filesystem::path("__primitive") / (finalName + ".sphere");
    if (!addMeshFromData(source, meshes))
        return std::nullopt;

    if (m_instances.empty())
        return std::nullopt;

    const auto makeUnique = [&](const std::string& desired) {
        if (std::none_of(m_instances.begin(), m_instances.end() - 1, [&](const MeshInstance& other) { return other.name() == desired; }))
            return desired;
        int duplicateCount = 1;
        std::string candidate;
        do {
            candidate = desired + " (" + std::to_string(++duplicateCount) + ")";
        } while (std::any_of(m_instances.begin(), m_instances.end() - 1, [&](const MeshInstance& other) { return other.name() == candidate; }));
        return candidate;
    };

    const std::size_t newIndex = m_instances.size() - 1;
    m_instances[newIndex].setName(makeUnique(finalName));
    return newIndex;
}

std::optional<std::size_t> MeshManager::createQuadPrimitive(const std::string& name,
    float width,
    float height,
    const glm::vec3& baseColor,
    float roughness,
    float metallic,
    bool doubleSided)
{
    const std::string finalName = name.empty() ? "Quad" : name;
    MeshData data = createQuadMeshData(std::max(width, 0.01f), std::max(height, 0.01f), baseColor, roughness, metallic, doubleSided);
    std::vector<MeshData> meshes;
    meshes.push_back(std::move(data));
    const std::filesystem::path source = std::filesystem::path("__primitive") / (finalName + ".quad");
    if (!addMeshFromData(source, meshes))
        return std::nullopt;

    if (m_instances.empty())
        return std::nullopt;

    const auto makeUnique = [&](const std::string& desired) {
        if (std::none_of(m_instances.begin(), m_instances.end() - 1, [&](const MeshInstance& other) { return other.name() == desired; }))
            return desired;
        int duplicateCount = 1;
        std::string candidate;
        do {
            candidate = desired + " (" + std::to_string(++duplicateCount) + ")";
        } while (std::any_of(m_instances.begin(), m_instances.end() - 1, [&](const MeshInstance& other) { return other.name() == candidate; }));
        return candidate;
    };

    const std::size_t newIndex = m_instances.size() - 1;
    m_instances[newIndex].setName(makeUnique(finalName));
    return newIndex;
}

std::optional<std::size_t> MeshManager::createBoxPrimitive(const std::string& name,
    const glm::vec3& extents,
    const glm::vec3& baseColor,
    float roughness,
    float metallic,
    bool doubleSided)
{
    const std::string finalName = name.empty() ? "Box" : name;
    glm::vec3 safeExtents = glm::max(extents, glm::vec3(0.01f));
    MeshData data = createBoxMeshData(safeExtents, baseColor, roughness, metallic, doubleSided);
    std::vector<MeshData> meshes;
    meshes.push_back(std::move(data));
    const std::filesystem::path source = std::filesystem::path("__primitive") / (finalName + ".box");
    if (!addMeshFromData(source, meshes))
        return std::nullopt;

    if (m_instances.empty())
        return std::nullopt;

    const auto makeUnique = [&](const std::string& desired) {
        if (std::none_of(m_instances.begin(), m_instances.end() - 1, [&](const MeshInstance& other) { return other.name() == desired; }))
            return desired;
        int duplicateCount = 1;
        std::string candidate;
        do {
            candidate = desired + " (" + std::to_string(++duplicateCount) + ")";
        } while (std::any_of(m_instances.begin(), m_instances.end() - 1, [&](const MeshInstance& other) { return other.name() == candidate; }));
        return candidate;
    };

    const std::size_t newIndex = m_instances.size() - 1;
    m_instances[newIndex].setName(makeUnique(finalName));
    return newIndex;
}

BoundingBox MeshManager::computeWorldBounds(const MeshInstance& instance) const
{
    return transformBounds(instance.localBounds(), instance.transform());
}

bool MeshManager::isLoaded(const std::filesystem::path& path) const
{
    return std::any_of(m_instances.begin(), m_instances.end(), [&](const MeshInstance& instance) {
        std::error_code ec;
        return std::filesystem::equivalent(instance.sourcePath(), path, ec) && !ec;
    });
}
