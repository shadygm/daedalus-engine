#include "shader.h"
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <fmt/format.h>
DISABLE_WARNINGS_POP()
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cctype>
#include <utility>

static constexpr GLuint invalid = 0xFFFFFFFF;

static bool checkShaderErrors(GLuint shader);
static bool checkProgramErrors(GLuint program);
static std::string readFile(const std::filesystem::path& filePath);
static void ensureNoIncludeDirective(const std::filesystem::path& filePath, const std::string& source);
static std::string composeShaderSource(const std::filesystem::path& filePath, const std::string& source, const std::string& preamble);

Shader::Shader(GLuint program)
    : m_program(program)
{
}

Shader::Shader()
    : m_program(invalid)
{
}

Shader::Shader(Shader&& other)
{
    m_program = other.m_program;
    other.m_program = invalid;
}

Shader::~Shader()
{
    if (m_program != invalid)
        glDeleteProgram(m_program);
}

Shader& Shader::operator=(Shader&& other)
{
    if (m_program != invalid)
        glDeleteProgram(m_program);

    m_program = other.m_program;
    other.m_program = invalid;
    return *this;
}

void Shader::bind() const
{
    assert(m_program != invalid);
    glUseProgram(m_program);
}

GLuint Shader::getAttributeLocation(const std::string& name) const
{
    GLuint loc = glGetAttribLocation(m_program, name.c_str());
    if (loc == invalid) {
        std::cerr << "Warning : Could not find attribute " << name << std::endl;
    }
    return loc;
}

GLint Shader::getUniformLocation(const std::string& name) const
{
    GLint loc = glGetUniformLocation(m_program, name.c_str());
    if (loc == GL_INVALID_INDEX) {
        std::cerr << "Warning : Could not find uniform " << name << std::endl;
    }
    return loc;
}

ShaderBuilder::~ShaderBuilder()
{
    freeShaders();
}

ShaderBuilder& ShaderBuilder::addStage(GLuint shaderStage, std::filesystem::path shaderFile)
{
    if (!std::filesystem::exists(shaderFile)) {
        throw ShaderLoadingException(fmt::format("File {} does not exist", shaderFile.string().c_str()));
    }

    const std::string fileSource = readFile(shaderFile);
    ensureNoIncludeDirective(shaderFile, fileSource);
    const std::string shaderSource = composeShaderSource(shaderFile, fileSource, "");
    const GLuint shader = glCreateShader(shaderStage);
    const char* shaderSourcePtr = shaderSource.c_str();
    glShaderSource(shader, 1, &shaderSourcePtr, nullptr);
    glCompileShader(shader);
    if (!checkShaderErrors(shader)) {
        glDeleteShader(shader);
        throw ShaderLoadingException(fmt::format("Failed to compile shader {}", shaderFile.string().c_str()));
    }

    m_shaders.push_back(shader);
    return *this;
}

Shader ShaderBuilder::build()
{
    // Combine vertex and fragment shaders into a single shader program.
    GLuint program = glCreateProgram();
    for (GLuint shader : m_shaders)
        glAttachShader(program, shader);
    glLinkProgram(program);

    if (!checkProgramErrors(program)) {
        throw ShaderLoadingException("Shader program failed to link");
    }

    return Shader(program);
}

void ShaderBuilder::freeShaders()
{
    for (GLuint shader : m_shaders)
        glDeleteShader(shader);
}

static std::string readFile(const std::filesystem::path& filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file)
        throw ShaderLoadingException(fmt::format("Failed to open shader file {}", filePath.string()));

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

static void ensureNoIncludeDirective(const std::filesystem::path& filePath, const std::string& source)
{
    enum class State { Normal, LineComment, BlockComment };

    State state = State::Normal;
    std::size_t lineNumber = 1;

    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        const char next = (i + 1 < source.size()) ? source[i + 1] : '\0';

        switch (state) {
        case State::Normal:
            if (c == '/' && next == '/') {
                state = State::LineComment;
                ++i;
                continue;
            }
            if (c == '/' && next == '*') {
                state = State::BlockComment;
                ++i;
                continue;
            }
            if (c == '#') {
                bool directiveAtLineStart = true;
                std::size_t lookback = i;
                while (lookback > 0) {
                    const char prev = source[lookback - 1];
                    if (prev == '\n' || prev == '\r')
                        break;
                    if (prev != ' ' && prev != '\t') {
                        directiveAtLineStart = false;
                        break;
                    }
                    --lookback;
                }

                if (directiveAtLineStart) {
                    std::size_t j = i + 1;
                    while (j < source.size() && (source[j] == ' ' || source[j] == '\t'))
                        ++j;

                    if (j + 7 <= source.size() && source.compare(j, 7, "include") == 0) {
                        const char after = (j + 7 < source.size()) ? source[j + 7] : '\0';
                        if (after == '\0' || (!std::isalnum(static_cast<unsigned char>(after)) && after != '_')) {
                            throw ShaderLoadingException(fmt::format(
                                "Shader file {} contains forbidden #include directive on line {}.",
                                filePath.string(), lineNumber));
                        }
                    }
                }
            }
            break;
        case State::LineComment:
            if (c == '\n' || c == '\r')
                state = State::Normal;
            break;
        case State::BlockComment:
            if (c == '*' && next == '/') {
                state = State::Normal;
                ++i;
            }
            break;
        }

        if (c == '\n')
            ++lineNumber;
        else if (c == '\r' && next != '\n')
            ++lineNumber;
    }
}

static std::string composeShaderSource(const std::filesystem::path& filePath, const std::string& source, const std::string& preamble)
{
    std::istringstream input(source);
    std::ostringstream headerStream;
    std::ostringstream bodyStream;

    std::string line;
    std::size_t lineNumber = 0;
    bool seenVersion = false;
    bool inHeader = true;

    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        std::string trimmed = line;
        const auto firstNonSpace = trimmed.find_first_not_of(" \t");
        if (firstNonSpace != std::string::npos)
            trimmed.erase(0, firstNonSpace);
        else
            trimmed.clear();

        if (inHeader) {
            if (trimmed.empty()) {
                headerStream << line << '\n';
                continue;
            }

            if (!seenVersion) {
                if (trimmed.rfind("#version", 0) == 0) {
                    seenVersion = true;
                    headerStream << line << '\n';
                    continue;
                }

                throw ShaderLoadingException(fmt::format(
                    "Shader {} must begin with #version directive (encountered '{}' on line {}).",
                    filePath.string(), trimmed, lineNumber));
            }

            if (trimmed.rfind("#extension", 0) == 0) {
                headerStream << line << '\n';
                continue;
            }

            inHeader = false;
        }

        bodyStream << line << '\n';
    }

    if (!seenVersion) {
        throw ShaderLoadingException(fmt::format(
            "Shader {} is missing a #version directive.", filePath.string()));
    }

    std::string result = headerStream.str();
    if (!preamble.empty()) {
        if (!result.empty() && result.back() != '\n')
            result.push_back('\n');
        result += preamble;
        if (!preamble.empty() && preamble.back() != '\n')
            result.push_back('\n');
    }

    result += bodyStream.str();
    return result;
}

static bool checkShaderErrors(GLuint shader)
{
    // Check if the shader compiled successfully.
    GLint compileSuccessful;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileSuccessful);

    // If it didn't, then read and print the compile log.
    if (!compileSuccessful) {
        GLint logLength;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

        std::string logBuffer;
        logBuffer.resize(static_cast<size_t>(logLength));
        glGetShaderInfoLog(shader, logLength, nullptr, logBuffer.data());

        std::cerr << logBuffer << std::endl;
        return false;
    } else {
        return true;
    }
}

static bool checkProgramErrors(GLuint program)
{
    // Check if the program linked successfully
    GLint linkSuccessful;
    glGetProgramiv(program, GL_LINK_STATUS, &linkSuccessful);

    // If it didn't, then read and print the link log
    if (!linkSuccessful) {
        GLint logLength;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);

        std::string logBuffer;
        logBuffer.resize(static_cast<size_t>(logLength));
        glGetProgramInfoLog(program, logLength, nullptr, logBuffer.data());

        std::cerr << logBuffer << std::endl;
        return false;
    } else {
        return true;
    }
}
