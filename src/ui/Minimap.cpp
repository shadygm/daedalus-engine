#include <glad/glad.h>

#include "ui/Minimap.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/matrix_transform.hpp>
#include <imgui/imgui.h>
#include <GLFW/glfw3.h>
DISABLE_WARNINGS_POP()

Minimap::Minimap(int size)
{
    allocate(size);
}

Minimap::~Minimap()
{
    destroy();
}

void Minimap::allocate(int size)
{
    destroy();
    m_size = size;

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTex);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_size, m_size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenRenderbuffers(1, &m_depthRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_size, m_size);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        destroy();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

void Minimap::destroy()
{
    if (m_depthRbo) { glDeleteRenderbuffers(1, &m_depthRbo); m_depthRbo = 0; }
    if (m_colorTex) { glDeleteTextures(1, &m_colorTex); m_colorTex = 0; }
    if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
}

inline bool hasCurrentContext()
{
    return glfwGetCurrentContext() != nullptr;
}

void Minimap::renderToTexture(const glm::vec3& centerXZ, float cameraHeight, float areaSize,
    const std::function<void(const glm::mat4& view, const glm::mat4& proj)>& drawCallback)
{
    if (!m_fbo || !hasCurrentContext())
        return;

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_size, m_size);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float half = areaSize * 0.5f;
    const glm::mat4 proj = glm::ortho(-half, half, -half, half, 0.1f, cameraHeight + 1000.0f);

    const glm::vec3 eye(centerXZ.x, cameraHeight, centerXZ.z);
    const glm::vec3 center(centerXZ.x, 0.0f, centerXZ.z);
    const glm::vec3 up(0.0f, 0.0f, -1.0f);
    const glm::mat4 view = glm::lookAt(eye, center, up);

    if (drawCallback)
        drawCallback(view, proj);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
}

void Minimap::drawOverlay(float posX, float posY, float width, float height)
{
    if (!m_colorTex)
        return;

    ImGui::SetNextWindowBgAlpha(0.4f);
    ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    if (ImGui::Begin("Minimap", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings)) {
        ImTextureID texId = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(m_colorTex));
        ImGui::Image(texId, ImVec2(width, height));
    }
    ImGui::End();
}

unsigned int Minimap::textureId() const
{
    return m_colorTex;
}
