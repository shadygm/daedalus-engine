// SPDX-License-Identifier: MIT
#pragma once
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/vec3.hpp>
DISABLE_WARNINGS_POP()
#include <exception>
#include <filesystem>
#include <framework/opengl_includes.h>
#include <vector>

struct TextureData {
    std::vector<uint8_t> bytes;
    int width { 0 };
    int height { 0 };
    int channels { 0 };
    bool compressed { false };
};

struct TextureSamplerSettings {
    GLint wrapS { GL_REPEAT };
    GLint wrapT { GL_REPEAT };
    GLint minFilter { GL_LINEAR_MIPMAP_LINEAR };
    GLint magFilter { GL_LINEAR };
};

struct ImageLoadingException : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Texture {
public:
    explicit Texture(std::filesystem::path filePath, bool srgb = false, TextureSamplerSettings sampler = {}, int forceChannels = 0);
    Texture(TextureData data, bool srgb = false, TextureSamplerSettings sampler = {});
    Texture(const Texture&) = delete;
    Texture(Texture&&) noexcept;
    void bind() const;
    ~Texture();

    Texture& operator=(const Texture&) = delete;
    Texture& operator=(Texture&& other) noexcept;

    void bind(GLenum textureSlot) const;
    [[nodiscard]] GLuint id() const { return m_texture; }
    [[nodiscard]] GLuint samplerHandle() const { return m_sampler; }
    [[nodiscard]] GLenum target() const { return m_target; }

    static void setForcePerDrawUpload(bool enabled);
    static bool forcePerDrawUpload();

    void refreshGpuIfNeeded() const;
    [[nodiscard]] bool hasCpuPixels() const { return !m_cpuPixels.empty(); }

private:
    static constexpr GLuint INVALID = 0xFFFFFFFF;
    void createSampler(const TextureSamplerSettings& sampler);
    void uploadFromCpuMemory() const;

    static bool s_forcePerDrawUpload;

    GLuint m_texture { INVALID };
    GLuint m_sampler { INVALID };
    GLenum m_target { GL_TEXTURE_2D };
    bool m_isSrgb { false };
    int m_cpuWidth { 0 };
    int m_cpuHeight { 0 };
    int m_cpuChannels { 0 };
    std::vector<uint8_t> m_cpuPixels;
};
