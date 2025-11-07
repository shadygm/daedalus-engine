// SPDX-License-Identifier: MIT

#pragma once

#include <framework/shader.h>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>

class ShaderManager {
public:
    ShaderManager() = default;

    void load(const std::string& name, const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath);
    bool bind(const std::string& name);

    [[nodiscard]] bool has(const std::string& name) const;

    void setVec2(const std::string& name, const glm::vec2& value) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setFloat(const std::string& name, float value) const;
    void setInt(const std::string& name, int value) const;
    void setMat3(const std::string& name, const glm::mat3& value) const;
    void setMat4(const std::string& name, const glm::mat4& value) const;

    [[nodiscard]] Shader& current();
    [[nodiscard]] const Shader& current() const;
    [[nodiscard]] Shader* find(const std::string& name);
    [[nodiscard]] const Shader* find(const std::string& name) const;

private:
    [[nodiscard]] Shader& requireCurrent() const;

private:
    std::unordered_map<std::string, Shader> m_shaders;
    mutable Shader* m_currentShader { nullptr };
};
