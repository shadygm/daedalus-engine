// SPDX-License-Identifier: MIT

#include "rendering/texture.h"

#include "rendering/TextureUnits.h"

#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <stb/stb_image.h>
DISABLE_WARNINGS_POP()
#include <framework/image.h>

#include <fmt/format.h>

#include <cassert>
#include <iostream>
#include <string_view>

#include <stdexcept>
#include <utility>

namespace {

struct LoadedImage {
    std::vector<uint8_t> pixels;
    int width { 0 };
    int height { 0 };
    int channels { 0 };
};

LoadedImage loadImageFromFile(const std::filesystem::path& path, int forceChannels = 0)
{
    Image cpuTexture { path, forceChannels };
    LoadedImage img;
    img.width = cpuTexture.width;
    img.height = cpuTexture.height;
    img.channels = cpuTexture.channels;
    img.pixels.assign(cpuTexture.get_data(), cpuTexture.get_data() + (img.width * img.height * img.channels));
    return img;
}

LoadedImage loadImageFromData(TextureData data)
{
    LoadedImage img;
    if (data.compressed) {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* decoded = stbi_load_from_memory(data.bytes.data(), static_cast<int>(data.bytes.size()), &width, &height, &channels, 0);
        if (!decoded)
            throw std::runtime_error("Failed to decode compressed embedded texture");
        img.width = width;
        img.height = height;
        img.channels = channels;
        img.pixels.assign(decoded, decoded + (width * height * channels));
        stbi_image_free(decoded);
    } else {
        img.width = data.width;
        img.height = data.height;
        img.channels = data.channels;
        img.pixels = std::move(data.bytes);
    }
    if (img.width <= 0 || img.height <= 0 || img.channels <= 0)
        throw std::runtime_error("Invalid embedded texture dimensions");
    return img;
}

GLenum pickExternalFormat(int channels)
{
    switch (channels) {
    case 1:
        return GL_RED;
    case 2:
        return GL_RG;
    case 3:
        return GL_RGB;
    case 4:
        return GL_RGBA;
    default:
        throw std::runtime_error("Unsupported channel count for texture upload");
    }
}

std::string_view formatToString(GLenum format)
{
    switch (format) {
    case GL_RED:
        return "GL_RED";
    case GL_RG:
        return "GL_RG";
    case GL_RGB:
        return "GL_RGB";
    case GL_RGBA:
        return "GL_RGBA";
    case GL_R8:
        return "GL_R8";
    case GL_RG8:
        return "GL_RG8";
    case GL_RGB8:
        return "GL_RGB8";
    case GL_RGBA8:
        return "GL_RGBA8";
    case GL_SRGB8:
        return "GL_SRGB8";
    case GL_SRGB8_ALPHA8:
        return "GL_SRGB8_ALPHA8";
    default:
        return "UNKNOWN";
    }
}

GLint pickInternalFormat(int channels, bool srgb)
{
    switch (channels) {
    case 3:
        return srgb ? GL_SRGB8 : GL_RGB8;
    case 4:
        return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    default:
        throw std::runtime_error(fmt::format("Unsupported channel count ({}) for srgb={}", channels, srgb));
    }
}

void uploadToGPU(int width, int height, int channels, bool srgb, GLuint texture, const uint8_t* pixels)
{
    glBindTexture(GL_TEXTURE_2D, texture);

    const GLenum externalFormat = pickExternalFormat(channels);
    const GLint internalFormat = pickInternalFormat(channels, srgb);

    std::cout << fmt::format("[Texture Upload] size={}x{} channels={} srgb={} -> internalFormat={} externalFormat={}", width, height, channels, srgb ? "true" : "false", formatToString(internalFormat), formatToString(externalFormat))
              << std::endl;

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, externalFormat, GL_UNSIGNED_BYTE, pixels);
    GLint checkFormat = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &checkFormat);
    if (checkFormat != internalFormat) {
        std::cerr << fmt::format("[Warning] Texture internal format mismatch! expected={} got={}", formatToString(internalFormat), formatToString(checkFormat)) << std::endl;
    }
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

}

bool Texture::s_forcePerDrawUpload = false;

Texture::Texture(std::filesystem::path filePath, bool srgb, TextureSamplerSettings sampler, int forceChannels)
    : m_isSrgb(srgb)
{
    LoadedImage img = loadImageFromFile(filePath, forceChannels);
    m_cpuWidth = img.width;
    m_cpuHeight = img.height;
    m_cpuChannels = img.channels;
    m_cpuPixels = std::move(img.pixels);
    glGenTextures(1, &m_texture);
    uploadFromCpuMemory();
    createSampler(sampler);
}

Texture::Texture(TextureData data, bool srgb, TextureSamplerSettings sampler)
    : m_isSrgb(srgb)
{
    LoadedImage img = loadImageFromData(std::move(data));
    m_cpuWidth = img.width;
    m_cpuHeight = img.height;
    m_cpuChannels = img.channels;
    m_cpuPixels = std::move(img.pixels);
    glGenTextures(1, &m_texture);
    uploadFromCpuMemory();
    createSampler(sampler);
}

Texture::Texture(Texture&& other) noexcept
    : m_texture(other.m_texture)
    , m_sampler(other.m_sampler)
    , m_target(other.m_target)
    , m_isSrgb(other.m_isSrgb)
    , m_cpuWidth(other.m_cpuWidth)
    , m_cpuHeight(other.m_cpuHeight)
    , m_cpuChannels(other.m_cpuChannels)
    , m_cpuPixels(std::move(other.m_cpuPixels))
{
    other.m_texture = INVALID;
    other.m_sampler = INVALID;
    other.m_target = GL_TEXTURE_2D;
    other.m_isSrgb = false;
    other.m_cpuWidth = 0;
    other.m_cpuHeight = 0;
    other.m_cpuChannels = 0;
}

void Texture::bind() const
{
    refreshGpuIfNeeded();
    glBindTexture(m_target, m_texture);
}

Texture::~Texture()
{
    if (m_sampler != INVALID)
        glDeleteSamplers(1, &m_sampler);
    if (m_texture != INVALID)
        glDeleteTextures(1, &m_texture);
}

void Texture::bind(GLenum textureSlot) const
{
#ifndef NDEBUG
    assert(textureSlot >= GL_TEXTURE0);
#endif
    if (textureSlot < GL_TEXTURE0)
        return;

    const GLuint unit = static_cast<GLuint>(textureSlot - GL_TEXTURE0);
    refreshGpuIfNeeded();
    TextureUnits::assertNotEnvUnit(unit);
    glBindTextureUnit(unit, m_texture);
}

Texture& Texture::operator=(Texture&& other) noexcept
{
    if (this == &other)
        return *this;

    if (m_sampler != INVALID)
        glDeleteSamplers(1, &m_sampler);
    if (m_texture != INVALID)
        glDeleteTextures(1, &m_texture);

    m_texture = other.m_texture;
    m_sampler = other.m_sampler;
    m_target = other.m_target;
    m_isSrgb = other.m_isSrgb;
    m_cpuWidth = other.m_cpuWidth;
    m_cpuHeight = other.m_cpuHeight;
    m_cpuChannels = other.m_cpuChannels;
    m_cpuPixels = std::move(other.m_cpuPixels);

    other.m_texture = INVALID;
    other.m_sampler = INVALID;
    other.m_target = GL_TEXTURE_2D;
    other.m_isSrgb = false;
    other.m_cpuWidth = 0;
    other.m_cpuHeight = 0;
    other.m_cpuChannels = 0;

    return *this;
}

void Texture::createSampler(const TextureSamplerSettings& sampler)
{
    if (m_sampler != INVALID)
        glDeleteSamplers(1, &m_sampler);

    glGenSamplers(1, &m_sampler);
    glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_S, sampler.wrapS);
    glSamplerParameteri(m_sampler, GL_TEXTURE_WRAP_T, sampler.wrapT);
    glSamplerParameteri(m_sampler, GL_TEXTURE_MIN_FILTER, sampler.minFilter);
    glSamplerParameteri(m_sampler, GL_TEXTURE_MAG_FILTER, sampler.magFilter);
}

void Texture::setForcePerDrawUpload(bool enabled)
{
    s_forcePerDrawUpload = enabled;
}

bool Texture::forcePerDrawUpload()
{
    return s_forcePerDrawUpload;
}

void Texture::refreshGpuIfNeeded() const
{
    if (!s_forcePerDrawUpload)
        return;
    if (m_texture == INVALID)
        return;
    if (m_cpuPixels.empty())
        return;

    uploadFromCpuMemory();
}

void Texture::uploadFromCpuMemory() const
{
    if (m_cpuPixels.empty() || m_cpuWidth <= 0 || m_cpuHeight <= 0 || m_cpuChannels <= 0)
        return;

    uploadToGPU(m_cpuWidth, m_cpuHeight, m_cpuChannels, m_isSrgb, m_texture, m_cpuPixels.data());
}
