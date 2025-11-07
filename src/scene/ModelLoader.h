#pragma once

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
DISABLE_WARNINGS_POP()

#include <filesystem>
#include <string>
#include <vector>

#include "rendering/Material.h"

struct aiNode;
struct aiMesh;
struct aiScene;

struct MeshData {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec4> tangents;
    std::vector<glm::vec2> texCoords0;
    std::vector<glm::vec2> texCoords1;
    std::vector<unsigned int> indices;
    glm::mat4 nodeTransform { 1.0f };
    bool hasUVs { false };
    bool hasSecondaryUVs { false };
    bool hasTangents { false };
    RenderMaterial material;
    MaterialTextures textures;
};

class ModelLoader {
public:
    bool loadModel(const std::string& path);
    [[nodiscard]] const std::vector<MeshData>& getMeshes() const;
    [[nodiscard]] const std::string& getLastError() const;

private:
    void processNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform);
    MeshData processMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& nodeTransform);
    void fillMaterial(const aiScene* scene, const aiMesh* mesh, MeshData& data);

private:
    std::vector<MeshData> m_meshes;
    std::filesystem::path m_directory;
    std::string m_lastError;
};
