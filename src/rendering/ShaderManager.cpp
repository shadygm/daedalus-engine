// SPDX-License-Identifier: MIT

#include "rendering/ShaderManager.h"

#include <framework/opengl_includes.h>

#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

void ShaderManager::load(const std::string& name, const std::filesystem::path& vertexPath, const std::filesystem::path& fragmentPath)
{
    ShaderBuilder builder;
    builder.addStage(GL_VERTEX_SHADER, vertexPath);
    builder.addStage(GL_FRAGMENT_SHADER, fragmentPath);
    m_shaders[name] = builder.build();
}

bool ShaderManager::bind(const std::string& name)
{
    auto it = m_shaders.find(name);
    if (it == m_shaders.end())
        return false;

    m_currentShader = &it->second;
    m_currentShader->bind();
    return true;
}

bool ShaderManager::has(const std::string& name) const
{
    return m_shaders.find(name) != m_shaders.end();
}

void ShaderManager::setVec2(const std::string& name, const glm::vec2& value) const
{
    Shader& shader = requireCurrent();
    const GLint location = shader.getUniformLocation(name);
    glUniform2fv(location, 1, glm::value_ptr(value));
}

void ShaderManager::setVec3(const std::string& name, const glm::vec3& value) const
{
    Shader& shader = requireCurrent();
    const GLint location = shader.getUniformLocation(name);
    glUniform3fv(location, 1, glm::value_ptr(value));
}

void ShaderManager::setFloat(const std::string& name, float value) const
{
    Shader& shader = requireCurrent();
    const GLint location = shader.getUniformLocation(name);
    glUniform1f(location, value);
}

void ShaderManager::setInt(const std::string& name, int value) const
{
    Shader& shader = requireCurrent();
    const GLint location = shader.getUniformLocation(name);
    glUniform1i(location, value);
}

void ShaderManager::setMat3(const std::string& name, const glm::mat3& value) const
{
    Shader& shader = requireCurrent();
    const GLint location = shader.getUniformLocation(name);
    glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
}

void ShaderManager::setMat4(const std::string& name, const glm::mat4& value) const
{
    Shader& shader = requireCurrent();
    const GLint location = shader.getUniformLocation(name);
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
}

Shader& ShaderManager::current()
{
    return requireCurrent();
}

const Shader& ShaderManager::current() const
{
    return requireCurrent();
}

Shader* ShaderManager::find(const std::string& name)
{
    auto it = m_shaders.find(name);
    return it == m_shaders.end() ? nullptr : &it->second;
}

const Shader* ShaderManager::find(const std::string& name) const
{
    auto it = m_shaders.find(name);
    return it == m_shaders.end() ? nullptr : &it->second;
}

Shader& ShaderManager::requireCurrent() const
{
    if (!m_currentShader)
        throw std::runtime_error("No shader is currently bound in ShaderManager.");
    return *m_currentShader;
}
