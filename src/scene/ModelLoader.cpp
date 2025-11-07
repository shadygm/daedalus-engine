// SPDX-License-Identifier: MIT

#include "scene/ModelLoader.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/pbrmaterial.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
DISABLE_WARNINGS_POP()

#include <algorithm>
#include <optional>
#include <string_view>
#include <iostream>

#include <fmt/format.h>

#include <glm/common.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/mat4x4.hpp>
namespace {

glm::mat4 aiToGlm(const aiMatrix4x4& matrix)
{
    glm::mat4 result { 1.0f };
    result[0][0] = matrix.a1;
    result[0][1] = matrix.b1;
    result[0][2] = matrix.c1;
    result[0][3] = matrix.d1;

    result[1][0] = matrix.a2;
    result[1][1] = matrix.b2;
    result[1][2] = matrix.c2;
    result[1][3] = matrix.d2;

    result[2][0] = matrix.a3;
    result[2][1] = matrix.b3;
    result[2][2] = matrix.c3;
    result[2][3] = matrix.d3;

    result[3][0] = matrix.a4;
    result[3][1] = matrix.b4;
    result[3][2] = matrix.c4;
    result[3][3] = matrix.d4;

    return result;
}

}

bool ModelLoader::loadModel(const std::string& path)
{
    Assimp::Importer importer;
    m_lastError.clear();
    const aiScene* scene = importer.ReadFile(path.c_str(),
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |      
        aiProcess_JoinIdenticalVertices |
        aiProcess_ImproveCacheLocality |
        aiProcess_SortByPType |
        aiProcess_FlipUVs);


    m_meshes.clear();
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        m_lastError = importer.GetErrorString();
        return false;
    }

    m_directory = std::filesystem::path(path).parent_path();
    processNode(scene->mRootNode, scene, glm::mat4(1.0f));
    if (m_meshes.empty()) {
        m_lastError = "Scene contains no mesh data.";
        return false;
    }
    return true;
}

const std::vector<MeshData>& ModelLoader::getMeshes() const
{
    return m_meshes;
}

const std::string& ModelLoader::getLastError() const
{
    return m_lastError;
}

void ModelLoader::processNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform)
{
    const glm::mat4 nodeTransform = parentTransform * aiToGlm(node->mTransformation);
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        m_meshes.push_back(processMesh(mesh, scene, nodeTransform));
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        processNode(node->mChildren[i], scene, nodeTransform);
}

namespace {

std::optional<std::filesystem::path> resolveTexturePath(const aiString& aiPath, const std::filesystem::path& directory)
{
    if (aiPath.length == 0)
        return std::nullopt;

    const std::string_view rawPath { aiPath.C_Str(), static_cast<std::size_t>(aiPath.length) };
    if (rawPath.empty())
        return std::nullopt;

    std::filesystem::path texPath = std::filesystem::path(rawPath);
    if (texPath.is_relative())
        texPath = directory / texPath;

    if (!std::filesystem::exists(texPath))
        return std::nullopt;
    return texPath;
}

GLint toGLWrap(aiTextureMapMode mode)
{
    switch (mode) {
    case aiTextureMapMode_Clamp:
        return GL_CLAMP_TO_EDGE;
    case aiTextureMapMode_Mirror:
        return GL_MIRRORED_REPEAT;
    case aiTextureMapMode_Decal:
        return GL_CLAMP_TO_BORDER;
    case aiTextureMapMode_Wrap:
    default:
        return GL_REPEAT;
    }
}

GLint toGLFilter(int filter, GLint fallback)
{
    if (filter == 0)
        return fallback;
    switch (filter) {
    case GL_NEAREST:
    case GL_LINEAR:
    case GL_NEAREST_MIPMAP_NEAREST:
    case GL_LINEAR_MIPMAP_NEAREST:
    case GL_NEAREST_MIPMAP_LINEAR:
    case GL_LINEAR_MIPMAP_LINEAR:
        return filter;
    default:
        return fallback;
    }
}

TextureData makeEmbeddedTextureData(const aiTexture* texture)
{
    TextureData data;
    if (!texture)
        return data;

    if (texture->mHeight == 0) {
        data.compressed = true;
        const auto* bytes = reinterpret_cast<const uint8_t*>(texture->pcData);
        data.bytes.assign(bytes, bytes + texture->mWidth);
    } else {
        data.compressed = false;
        data.width = static_cast<int>(texture->mWidth);
        data.height = static_cast<int>(texture->mHeight);
        data.channels = 4;
        data.bytes.resize(data.width * data.height * data.channels);
        for (int y = 0; y < data.height; ++y) {
            for (int x = 0; x < data.width; ++x) {
                const aiTexel& texel = texture->pcData[y * data.width + x];
                const int idx = (y * data.width + x) * data.channels;
                data.bytes[idx + 0] = texel.r;
                data.bytes[idx + 1] = texel.g;
                data.bytes[idx + 2] = texel.b;
                data.bytes[idx + 3] = texel.a;
            }
        }
    }
    return data;
}

MaterialTextureReference makeTextureReference(const aiScene* scene,
    const aiMaterial* material,
    aiTextureType type,
    unsigned index,
    const std::filesystem::path& directory)
{
    MaterialTextureReference reference {};
    aiString texPath;
    aiTextureMapMode mapModes[3] = { aiTextureMapMode_Wrap, aiTextureMapMode_Wrap, aiTextureMapMode_Wrap };
    unsigned int uvIndex = 0;

    // Assimp's GetTexture overload expects 8 args: aiString*, aiTextureMapping*, unsigned int*, ai_real*, aiTextureOp*, aiTextureMapMode*
    aiTextureMapping mapping;
    ai_real blend = 0.0f;
    aiTextureOp op = aiTextureOp_Multiply;
    if (material->GetTexture(type, index, &texPath, &mapping, &uvIndex, &blend, &op, mapModes) == AI_SUCCESS) {
        std::string textureIdentifier = texPath.C_Str();
        if (!textureIdentifier.empty() && textureIdentifier.front() == '*') {
            if (scene) {
                const aiTexture* embedded = scene->GetEmbeddedTexture(textureIdentifier.c_str());
                if (embedded) {
                    reference.embedded = std::make_shared<TextureData>(makeEmbeddedTextureData(embedded));
                    reference.cacheKey = textureIdentifier;
                }
            }
        } else {
            if (auto resolved = resolveTexturePath(texPath, directory)) {
                reference.path = std::move(resolved);
                const std::filesystem::path absolute = std::filesystem::absolute(*reference.path);
                reference.cacheKey = absolute.string();
            }
        }
        reference.texCoord = uvIndex;
        reference.sampler.wrapS = toGLWrap(mapModes[0]);
        reference.sampler.wrapT = toGLWrap(mapModes[1]);

        int minFilter = reference.sampler.minFilter;
        // Use Assimp glTF mapping filter matkeys
        if (material->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(type, index), minFilter) == AI_SUCCESS)
            reference.sampler.minFilter = toGLFilter(minFilter, reference.sampler.minFilter);

        int magFilter = reference.sampler.magFilter;
        if (material->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(type, index), magFilter) == AI_SUCCESS)
            reference.sampler.magFilter = toGLFilter(magFilter, reference.sampler.magFilter);

        float scale = reference.scale;
        if (material->Get(AI_MATKEY_GLTF_TEXTURE_SCALE(type, index), scale) == AI_SUCCESS)
            reference.scale = scale;

        aiUVTransform transform {};
        if (material->Get(AI_MATKEY_UVTRANSFORM(type, index), transform) == AI_SUCCESS) {
            reference.uvOffset = glm::vec2(transform.mTranslation.x, transform.mTranslation.y);
            reference.uvScale = glm::vec2(transform.mScaling.x, transform.mScaling.y);
            reference.uvRotation = transform.mRotation;
        }
    }

    return reference;
}

bool hasUnlitExtension(const aiMaterial* material)
{
    if (!material)
        return false;

    int unlit = 0;
    if (material->Get(AI_MATKEY_GLTF_UNLIT, unlit) == AI_SUCCESS)
        return unlit != 0;

    return false;
}

} // namespace

MeshData ModelLoader::processMesh(aiMesh* mesh, const aiScene* scene, const glm::mat4& nodeTransform)
{
    MeshData data;
    data.nodeTransform = nodeTransform;
    data.positions.reserve(mesh->mNumVertices);
    data.normals.reserve(mesh->mNumVertices);
    data.tangents.reserve(mesh->mNumVertices);
    data.texCoords0.reserve(mesh->mNumVertices);
    data.texCoords1.reserve(mesh->mNumVertices);

    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D& pos = mesh->mVertices[i];
        data.positions.emplace_back(pos.x, pos.y, pos.z);

        glm::vec3 normalVector { 0.0f, 1.0f, 0.0f };
        if (mesh->HasNormals()) {
            const aiVector3D& normal = mesh->mNormals[i];
            normalVector = glm::vec3(normal.x, normal.y, normal.z);
        }
        data.normals.push_back(normalVector);

        if (mesh->HasTangentsAndBitangents()) {
            const aiVector3D& tangent = mesh->mTangents[i];
            const aiVector3D& bitangent = mesh->mBitangents[i];
            const glm::vec3 tangentVec { tangent.x, tangent.y, tangent.z };
            const glm::vec3 bitangentVec { bitangent.x, bitangent.y, bitangent.z };
            float handedness = glm::sign(glm::dot(glm::cross(normalVector, tangentVec), bitangentVec));
            if (handedness == 0.0f)
                handedness = 1.0f;
            data.tangents.emplace_back(tangentVec, handedness);
        } else {
            data.tangents.emplace_back(0.0f, 0.0f, 0.0f, 1.0f);
        }

        if (mesh->HasTextureCoords(0)) {
            const aiVector3D& tex = mesh->mTextureCoords[0][i];
            data.texCoords0.emplace_back(tex.x, tex.y);
        } else {
            data.texCoords0.emplace_back(0.0f, 0.0f);
        }

        if (mesh->HasTextureCoords(1)) {
            const aiVector3D& tex1 = mesh->mTextureCoords[1][i];
            data.texCoords1.emplace_back(tex1.x, tex1.y);
        } else {
            data.texCoords1.emplace_back(0.0f, 0.0f);
        }
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; ++j)
            data.indices.push_back(face.mIndices[j]);
    }

    data.hasUVs = mesh->HasTextureCoords(0);
    data.hasSecondaryUVs = mesh->HasTextureCoords(1);
    data.hasTangents = mesh->HasTangentsAndBitangents();

    fillMaterial(scene, mesh, data);

    return data;
}

void ModelLoader::fillMaterial(const aiScene* scene, const aiMesh* mesh, MeshData& data)
{
    if (!scene || !mesh || mesh->mMaterialIndex < 0 || mesh->mMaterialIndex >= static_cast<int>(scene->mNumMaterials))
        return;

    const aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
    if (!material)
        return;

    data.material = RenderMaterial {};
    data.material.unlit = hasUnlitExtension(material);
    if (data.material.unlit)
        data.material.usePBR = false;

    aiColor4D baseColorFactor { 1.0f, 1.0f, 1.0f, 1.0f };
    bool hasBaseColor = false;
    if (material->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, baseColorFactor) == AI_SUCCESS)
        hasBaseColor = true;
    else if (material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColorFactor) == AI_SUCCESS)
        hasBaseColor = true;

    if (hasBaseColor) {
        data.material.baseColor = glm::vec3(baseColorFactor.r, baseColorFactor.g, baseColorFactor.b);
        data.material.diffuseColor = data.material.baseColor;
        data.material.opacity = baseColorFactor.a;
    }

    data.material.specularColor = glm::vec3(0.04f);

    float opacity = data.material.opacity;
    if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS)
        data.material.opacity = opacity;

    float metallicFactor = data.material.metallic;
    if (material->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, metallicFactor) != AI_SUCCESS)
        material->Get(AI_MATKEY_METALLIC_FACTOR, metallicFactor);
    data.material.metallic = glm::clamp(metallicFactor, 0.0f, 1.0f);

    float roughnessFactor = data.material.roughness;
    if (material->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, roughnessFactor) != AI_SUCCESS)
        material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughnessFactor);
    data.material.roughness = glm::clamp(roughnessFactor, 0.04f, 1.0f);
    data.material.shininess = glm::clamp(256.0f * (1.0f - data.material.roughness), 1.0f, 256.0f);

    // Legacy spec-gloss keys are ignored; we standardize on metallic-roughness only.

    aiColor3D emissiveColor { 0.0f, 0.0f, 0.0f };
    if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == AI_SUCCESS)
        data.material.emissive = glm::vec3(emissiveColor.r, emissiveColor.g, emissiveColor.b);

    float emissiveIntensity = data.material.emissiveIntensity;
    if (material->Get(AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity) == AI_SUCCESS)
        data.material.emissiveIntensity = emissiveIntensity;

    data.material.ao = 1.0f;
    data.material.aoIntensity = data.material.ao;
    data.material.normalScale = 1.0f;
    data.material.normalStrength = data.material.normalScale;

    data.textures = MaterialTextures {};
    data.textures.baseColor = makeTextureReference(scene, material, aiTextureType_BASE_COLOR, 0, m_directory);
    if (!data.textures.baseColor.isValid())
        data.textures.baseColor = makeTextureReference(scene, material, aiTextureType_DIFFUSE, 0, m_directory);

    data.textures.metallicRoughness = makeTextureReference(scene, material, aiTextureType_METALNESS, 0, m_directory);
    if (!data.textures.metallicRoughness.isValid())
        data.textures.metallicRoughness = makeTextureReference(scene, material, aiTextureType_DIFFUSE_ROUGHNESS, 0, m_directory);
    if (!data.textures.metallicRoughness.isValid())
        data.textures.metallicRoughness = makeTextureReference(scene, material, aiTextureType_UNKNOWN, 0, m_directory);

    data.textures.normal = makeTextureReference(scene, material, aiTextureType_NORMALS, 0, m_directory);
    if (!data.textures.normal.isValid())
        data.textures.normal = makeTextureReference(scene, material, aiTextureType_HEIGHT, 0, m_directory);
    data.material.normalScale = data.textures.normal.scale;
    data.material.normalStrength = data.material.normalScale;

    data.textures.occlusion = makeTextureReference(scene, material, aiTextureType_AMBIENT_OCCLUSION, 0, m_directory);
    if (!data.textures.occlusion.isValid())
        data.textures.occlusion = makeTextureReference(scene, material, aiTextureType_LIGHTMAP, 0, m_directory);

    float occlusionStrength = data.material.ao;
    if (material->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_AMBIENT_OCCLUSION, 0), occlusionStrength) == AI_SUCCESS)
        data.material.ao = glm::clamp(occlusionStrength, 0.0f, 1.0f);
    data.textures.occlusion.scale = data.material.ao;
    data.material.aoIntensity = data.material.ao;

    data.textures.emissive = makeTextureReference(scene, material, aiTextureType_EMISSIVE, 0, m_directory);

    // Height / displacement map (optional)
    // Try common slots used by Assimp: DISPLACEMENT first, then HEIGHT.
    data.textures.height = makeTextureReference(scene, material, aiTextureType_DISPLACEMENT, 0, m_directory);
    if (!data.textures.height.isValid())
        data.textures.height = makeTextureReference(scene, material, aiTextureType_HEIGHT, 0, m_directory);

    data.material.hasAlbedoTexture = data.textures.baseColor.isValid();
    data.material.hasMetallicRoughnessTexture = data.textures.metallicRoughness.isValid();
    data.material.hasNormalTexture = data.textures.normal.isValid();
    data.material.hasAOTexture = data.textures.occlusion.isValid();
    data.material.hasEmissiveTexture = data.textures.emissive.isValid();
    data.material.hasHeightTexture = data.textures.height.isValid();
    data.material.occlusionFromMetallicRoughness = !data.textures.occlusion.isValid() && data.textures.metallicRoughness.isValid();

    data.material.albedoUV = data.textures.baseColor.texCoord;
    data.material.metallicRoughnessUV = data.textures.metallicRoughness.texCoord;
    data.material.normalUV = data.textures.normal.texCoord;
    data.material.aoUV = data.textures.occlusion.texCoord;
    data.material.emissiveUV = data.textures.emissive.texCoord;
    data.material.heightUV = data.textures.height.texCoord;

    const auto copyUvTransform = [](RenderMaterial::UVTransform& dst, const MaterialTextureReference& src) {
        dst.offset = src.uvOffset;
        dst.scale = src.uvScale;
        dst.rotation = src.uvRotation;
    };

    copyUvTransform(data.material.albedoUVTransform, data.textures.baseColor);
    copyUvTransform(data.material.metallicRoughnessUVTransform, data.textures.metallicRoughness);
    copyUvTransform(data.material.normalUVTransform, data.textures.normal);
    copyUvTransform(data.material.aoUVTransform, data.textures.occlusion);
    copyUvTransform(data.material.emissiveUVTransform, data.textures.emissive);
    copyUvTransform(data.material.heightUVTransform, data.textures.height);

    const auto logTextureReference = [](std::string_view label, const MaterialTextureReference& reference, unsigned int uvSet) {
        if (!reference.isValid())
            return;

        std::cout << fmt::format("[TextureInfo] {} uses UV{} offset=({}, {}) scale=({}, {}) rotation={} rad\n",
            label,
            uvSet,
            reference.uvOffset.x,
            reference.uvOffset.y,
            reference.uvScale.x,
            reference.uvScale.y,
            reference.uvRotation);
    };

    logTextureReference("BaseColor", data.textures.baseColor, data.material.albedoUV);
    logTextureReference("MetallicRoughness", data.textures.metallicRoughness, data.material.metallicRoughnessUV);
    logTextureReference("Normal", data.textures.normal, data.material.normalUV);
    logTextureReference("Occlusion", data.textures.occlusion, data.material.aoUV);
    logTextureReference("Emissive", data.textures.emissive, data.material.emissiveUV);
    logTextureReference("Height", data.textures.height, data.material.heightUV);

    int doubleSided = 0;
    if (material->Get(AI_MATKEY_TWOSIDED, doubleSided) == AI_SUCCESS)
        data.material.doubleSided = doubleSided != 0;

    aiString alphaMode;
    if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {
        const std::string mode = alphaMode.C_Str();
        if (mode == "BLEND")
            data.material.alphaMode = AlphaMode::Blend;
        else if (mode == "MASK")
            data.material.alphaMode = AlphaMode::Mask;
        else
            data.material.alphaMode = AlphaMode::Opaque;
    }

    float cutoff = data.material.alphaCutoff;
    if (material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff) == AI_SUCCESS)
        data.material.alphaCutoff = cutoff;
}
