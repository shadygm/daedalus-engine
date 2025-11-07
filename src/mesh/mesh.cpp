#include "mesh/mesh.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <fmt/format.h>
DISABLE_WARNINGS_POP()
#include <glm/gtx/norm.hpp>
#include <iostream>
#include <vector>
#include <algorithm>

GPUMaterial::GPUMaterial(const Material& material) :
    kd(material.kd),
    ks(material.ks),
    shininess(material.shininess)
{}

GPUMesh::GPUMesh(const Mesh& cpuMesh)
{
    // Create uniform buffer to store mesh material (https://learnopengl.com/Advanced-OpenGL/Advanced-GLSL)
    GPUMaterial gpuMaterial(cpuMesh.material);
    glGenBuffers(1, &m_uboMaterial);
    glBindBuffer(GL_UNIFORM_BUFFER, m_uboMaterial);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(GPUMaterial), &gpuMaterial, GL_STATIC_READ);

    // Figure out if this mesh has texture coordinates and tangents
    m_hasTextureCoords = std::any_of(cpuMesh.vertices.begin(), cpuMesh.vertices.end(), [](const Vertex& v) {
        return glm::length2(glm::vec2(v.texCoord)) > 1e-6f;
    });
    m_hasSecondaryTextureCoords = std::any_of(cpuMesh.vertices.begin(), cpuMesh.vertices.end(), [](const Vertex& v) {
        return glm::length2(glm::vec2(v.texCoord1)) > 1e-6f;
    });
    m_hasTangents = std::any_of(cpuMesh.vertices.begin(), cpuMesh.vertices.end(), [](const Vertex& v) {
        return glm::length2(glm::vec3(v.tangent)) > 1e-6f;
    });

    // Create VAO and bind it so subsequent creations of VBO and IBO are bound to this VAO
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    // Create vertex buffer object (VBO)
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.vertices.size() * sizeof(decltype(cpuMesh.vertices)::value_type)), cpuMesh.vertices.data(), GL_STATIC_DRAW);

    // Create index buffer object (IBO)
    glGenBuffers(1, &m_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.triangles.size() * sizeof(decltype(cpuMesh.triangles)::value_type)), cpuMesh.triangles.data(), GL_STATIC_DRAW);

    // Tell OpenGL that we will be using vertex attributes 0, 1, 2 and 3.
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glEnableVertexAttribArray(4);
    // We tell OpenGL what each vertex looks like and how they are mapped to the shader (location = ...).
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord1));
    // Reuse all attributes for each instance
    glVertexAttribDivisor(0, 0);
    glVertexAttribDivisor(1, 0);
    glVertexAttribDivisor(2, 0);
    glVertexAttribDivisor(3, 0);
    glVertexAttribDivisor(4, 0);

    // Each triangle has 3 vertices.
    m_numIndices = static_cast<GLsizei>(3 * cpuMesh.triangles.size());
}

GPUMesh::GPUMesh(GPUMesh&& other)
{
    moveInto(std::move(other));
}

GPUMesh::~GPUMesh()
{
    freeGpuMemory();
}

GPUMesh& GPUMesh::operator=(GPUMesh&& other)
{
    moveInto(std::move(other));
    return *this;
}

bool GPUMesh::hasTextureCoords() const
{
    return m_hasTextureCoords;
}

void GPUMesh::draw(const Shader& drawingShader)
{
    
    // Draw the mesh's triangles
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_numIndices, GL_UNSIGNED_INT, nullptr);
}

void GPUMesh::drawInstanced(GLsizei instanceCount) const
{
    glBindVertexArray(m_vao);
    glDrawElementsInstanced(GL_TRIANGLES, m_numIndices, GL_UNSIGNED_INT, nullptr, instanceCount);
}

void GPUMesh::moveInto(GPUMesh&& other)
{
    freeGpuMemory();
    m_numIndices = other.m_numIndices;
    m_hasTextureCoords = other.m_hasTextureCoords;
    m_hasSecondaryTextureCoords = other.m_hasSecondaryTextureCoords;
    m_hasTangents = other.m_hasTangents;
    m_ibo = other.m_ibo;
    m_vbo = other.m_vbo;
    m_vao = other.m_vao;
    m_uboMaterial = other.m_uboMaterial;

    other.m_numIndices = 0;
    other.m_hasTextureCoords = false;
    other.m_hasSecondaryTextureCoords = false;
    other.m_hasTangents = false;
    other.m_ibo = INVALID;
    other.m_vbo = INVALID;
    other.m_vao = INVALID;
    other.m_uboMaterial = INVALID;
}

void GPUMesh::freeGpuMemory()
{
    if (m_vao != INVALID)
        glDeleteVertexArrays(1, &m_vao);
    if (m_vbo != INVALID)
        glDeleteBuffers(1, &m_vbo);
    if (m_ibo != INVALID)
        glDeleteBuffers(1, &m_ibo);
    if (m_uboMaterial != INVALID)
        glDeleteBuffers(1, &m_uboMaterial);
}
