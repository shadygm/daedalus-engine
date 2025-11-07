// SPDX-License-Identifier: MIT
#pragma once

#include "rendering/texture.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

enum class AlphaMode {
    Opaque,
    Mask,
    Blend
};

struct MaterialTextureReference {
    std::optional<std::filesystem::path> path;
    std::shared_ptr<TextureData> embedded;
    std::string cacheKey;
    TextureSamplerSettings sampler;
    unsigned int texCoord { 0 };
    float scale { 1.0f }; // normal/occlusion scaling where applicable
    glm::vec2 uvOffset { 0.0f, 0.0f };
    glm::vec2 uvScale { 1.0f, 1.0f };
    float uvRotation { 0.0f };

    [[nodiscard]] bool isValid() const { return path.has_value() || embedded != nullptr; }
};

struct MaterialTextures {
    MaterialTextureReference baseColor;
    MaterialTextureReference metallicRoughness;
    MaterialTextureReference normal;
    MaterialTextureReference occlusion;
    MaterialTextureReference emissive;
    // Optional dedicated height/displacement map
    MaterialTextureReference height;
};

struct RenderMaterial {
    struct UVTransform {
        glm::vec2 offset { 0.0f, 0.0f };
        glm::vec2 scale { 1.0f, 1.0f };
        float rotation { 0.0f };
    };

    bool usePBR { true };
    bool unlit { false };
    bool doubleSided { false };
    bool isTransparent { false };

    bool hasAlbedoTexture { false };
    bool hasMetallicRoughnessTexture { false };
    bool hasNormalTexture { false };
    bool hasAOTexture { false };
    bool hasEmissiveTexture { false };
    bool hasHeightTexture { false };

    glm::vec3 baseColor { 1.0f, 1.0f, 1.0f };
    glm::vec3 diffuseColor { 1.0f, 1.0f, 1.0f };
    glm::vec3 emissive { 0.0f, 0.0f, 0.0f };
    glm::vec3 specularColor { 0.04f, 0.04f, 0.04f };
    float opacity { 1.0f };

    float metallic { 1.0f };
    float roughness { 1.0f };
    float ao { 1.0f };
    float normalScale { 1.0f };
    float emissiveIntensity { 1.0f };
    float aoIntensity { 1.0f };
    float shininess { 64.0f };
    float normalStrength { 1.0f };
    float alphaCutoff { 0.5f };

    bool occlusionFromMetallicRoughness { false };

    AlphaMode alphaMode { AlphaMode::Opaque };

    std::shared_ptr<Texture> albedoMap;
    std::shared_ptr<Texture> metallicRoughnessMap;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> aoMap;
    std::shared_ptr<Texture> emissiveMap;
    std::shared_ptr<Texture> heightMap;

    unsigned int albedoUV { 0 };
    unsigned int metallicRoughnessUV { 0 };
    unsigned int normalUV { 0 };
    unsigned int aoUV { 0 };
    unsigned int emissiveUV { 0 };
    unsigned int heightUV { 0 };

    UVTransform albedoUVTransform {};
    UVTransform metallicRoughnessUVTransform {};
    UVTransform normalUVTransform {};
    UVTransform aoUVTransform {};
    UVTransform emissiveUVTransform {};
    UVTransform heightUVTransform {};

    glm::vec3 translation { 0.0f, 0.0f, 0.0f };
    glm::vec3 rotationEuler { 0.0f, 0.0f, 0.0f };
    glm::vec3 scale { 1.0f, 1.0f, 1.0f };

    [[nodiscard]] bool hasAlbedoMap() const { return static_cast<bool>(albedoMap); }
    [[nodiscard]] bool hasMetallicRoughnessMap() const { return static_cast<bool>(metallicRoughnessMap); }
    [[nodiscard]] bool hasNormalMap() const { return static_cast<bool>(normalMap); }
    [[nodiscard]] bool hasAOMap() const { return static_cast<bool>(aoMap); }
    [[nodiscard]] bool hasEmissiveMap() const { return static_cast<bool>(emissiveMap); }
    [[nodiscard]] bool hasHeightMap() const { return static_cast<bool>(heightMap); }

    void refreshTextureUsageFlags()
    {
        hasAlbedoTexture = hasAlbedoMap();
        hasMetallicRoughnessTexture = hasMetallicRoughnessMap();
        hasNormalTexture = hasNormalMap();
        hasAOTexture = hasAOMap();
        hasEmissiveTexture = hasEmissiveMap();
        hasHeightTexture = hasHeightMap();
    }
};
