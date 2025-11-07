#include "app/DebugUiManager.h"
#include "camera/CameraStage.h"
#include "camera/CameraPath.h"
#include "camera/CameraPathPlayer.h"
#include "rendering/ShadingStage.h"
#include "app/SelectionManager.h"
#include "rendering/LightManager.h"
#include "rendering/EnvironmentManager.h"
#include "rendering/CameraEffectsStage.h"
#include "rendering/SunPathController.h"
#include "rendering/PathRenderer.h"
#include "rendering/RenderStats.h"
#include "mesh/MeshManager.h"
#include "mesh/mesh.h"
#include "pendulum/PendulumManager.h"
#include "terrain/ProceduralFloor.h"
#include "player/PlayerController.h"
#include "scene/ModelLoader.h"
#include "particle/ParticleSystem.h"
#include "water/Water.h"
#include "util/BezierPath.h"
#include "ui/Minimap.h"

#include <framework/file_picker.h>
// Always include window first (because it includes glfw, which includes GL which needs to be included AFTER glew).
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glad/glad.h>
// Include glad before glfw3
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/mat4x4.hpp>
#include <framework/ray.h>
#include <imgui/imgui.h>
#include <optional>
DISABLE_WARNINGS_POP()
#include <framework/window.h>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <array>
#include <utility>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <cmath>
#include <cstdlib>
#include <cfloat>
#include <limits>

#ifndef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX 0x9048
#endif
#ifndef GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#endif

#ifndef NDEBUG
namespace {
void traceFramebuffer(const char* label)
{
    GLint draw = 0;
    GLint read = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    std::fprintf(stderr, "[Application] %s | draw=%d read=%d\n", label, draw, read);
}
}
#define TRACE_APP_FBO(label) traceFramebuffer(label)
#else
#define TRACE_APP_FBO(label) ((void)0)
#endif

namespace {

void APIENTRY glDebugOutput(GLenum source,
    GLenum type,
    GLuint id,
    GLenum severity,
    GLsizei length,
    const GLchar* message,
    const void* userParam)
{
    (void)source;
    (void)type;
    (void)severity;
    (void)length;
    (void)userParam;
    std::fprintf(stderr, "[GL %u] %s\n", id, message);
}

} // namespace

class Application {
public:
    explicit Application(std::optional<std::filesystem::path> initialScene = std::nullopt);
    ~Application();

    void update();

private:
    // Render passes and helpers
    void renderShadowPasses(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    void renderPass(const glm::mat4& viewMatrix,
                    const glm::mat4& projectionMatrix,
                    const glm::vec3& cameraPosition,
                    RenderStats& stats);

    void renderTransparentPass(const glm::mat4& viewMatrix,
                            const glm::mat4& projectionMatrix,
                            const glm::vec3& cameraPosition); // transparent pass (no stats)

    void renderSkybox(const glm::mat4& viewMatrix,
                    const glm::mat4& projectionMatrix,
                    RenderStats& stats);

    void renderDebugPrimitives(const glm::mat4& viewMatrix,
                            const glm::mat4& projectionMatrix,
                            RenderStats& stats);

    void renderPendulums(const glm::mat4& viewMatrix,
                        const glm::mat4& projectionMatrix,
                        const glm::vec3& cameraPosition,
                        RenderStats& stats);
    void debugInspectSceneFramebuffer(glm::ivec2 framebufferSize);
    
    void loadSceneFromPath(const std::filesystem::path& path);
    void setModelPathBuffer(const std::filesystem::path& path);
    void loadEnvironmentFromPath(const std::filesystem::path& path);
    void setEnvironmentPathBuffer(const std::filesystem::path& path);

    void registerDebugTabs();
    void drawScenePanel();
    void drawEnvironmentPanel();
    void drawPlayerPanel();
    void drawPathsPanel();
    void drawParticlesPanel(); // <<< ADDED
    void drawCameraPathPanel();
    void drawPendulumPanel();
    void drawSelectionPanel();
    void drawPerformancePanel();
    CameraKeyframe captureCurrentCameraKeyframe(float timeSeconds) const;
    void rebuildCameraPathBezier();
    void renderCameraPathDebug(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, RenderStats& stats);
    void drawSelectionOverlay(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    void drawCrosshairOverlay() const;
    void gatherSelectables();
    void applySelectionDelta(const glm::vec3& delta);
    void syncMeshSelectionWithCurrentSelection();
    void beginPendulumDrag(std::size_t pendulumIndex);
    void endPendulumDrag();
    [[nodiscard]] bool crosshairEnabled() const;
    void beginFrameStats(float deltaTime);
    void finalizeFrameStats();
    void updateGpuMemoryStats();

    // --- Light cube helpers ---
    GLuint m_lightCubeVAO {0}, m_lightCubeVBO {0}, m_lightCubeEBO {0};
    Shader m_lightCubeShader; // simple unlit color shader
    void initLightCube();
    void buildLightCubeShader();
    void renderLightCube(const glm::vec3& pos, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& color);

    DebugUiManager m_debugUi;
    DebugUiManager::TabHandle m_tabPerformance;
    DebugUiManager::TabHandle m_tabCamera;
    DebugUiManager::TabHandle m_tabMeshes;
    DebugUiManager::TabHandle m_tabShading;
    DebugUiManager::TabHandle m_tabLights;
    DebugUiManager::TabHandle m_tabShadows;
    DebugUiManager::TabHandle m_tabPaths;
    DebugUiManager::TabHandle m_tabFloor;
    DebugUiManager::TabHandle m_tabEnvironment;
    DebugUiManager::TabHandle m_tabCameraFx;
    DebugUiManager::TabHandle m_tabParticles; // <<< ADDED
    DebugUiManager::TabHandle m_tabPendulums;
    DebugUiManager::TabHandle m_tabSelection;
    DebugUiManager::TabHandle m_tabMinimap;

    Window m_window;
    CameraStage m_cameraStage;
    ShadingStage m_shadingStage;
    EnvironmentManager m_environmentManager;
    CameraEffectsStage m_cameraEffectsStage;
    CameraEffectsStage::Settings m_cameraEffectsSettings;
    LightManager m_lightManager;
    SunPathController m_sunPathController;
    PathRenderer m_pathRenderer;
    PathRenderer m_cameraPathRenderer;

    CameraPath m_cameraPath;
    CameraPathPlayer m_cameraPathPlayer;
    BezierPath m_cameraPathBezier;
    std::uint64_t m_cameraPathBezierVersion { std::numeric_limits<std::uint64_t>::max() };
    bool m_cameraPathVisible { true };
    bool m_cameraPathShowKeyframes { true };
    bool m_cameraPathShowTangents { false };
    bool m_cameraPathFollowCamera { false };
    float m_cameraPathPlaybackSpeed { 1.0f };
    float m_defaultCameraFov { 80.0f };
    float m_activeCameraFov { 80.0f };
    std::optional<CameraPath::Sample> m_cameraPathLastSample;
    std::optional<std::size_t> m_cameraPathSelectedIndex;

    MeshManager m_meshManager;
    ModelLoader m_modelLoader;
    PendulumManager m_pendulumManager;
    SelectionManager m_selectionManager;
    std::optional<SelectionManager::HitResult> m_hoveredSelectable;

    // Particles
    ParticleSystem m_particles;    // <<< ADDED
    FireworkParams m_fireworkParams; // <<< ADDED (if your system uses it)

    std::array<char, 512> m_modelPathBuffer { "" };
    std::string m_modelLoadMessage;
    bool m_lastModelLoadSuccess { true };
    std::filesystem::path m_lastModelPath;
    std::array<char, 512> m_environmentPathBuffer { "" };
    std::string m_environmentLoadMessage;
    bool m_environmentLoadSuccess { true };
    bool m_showGround { true };

    ProceduralFloor m_floor;
    PlayerController m_player;

    std::string m_pendulumNodePrimitiveName { "__pendulum_node__" };
    std::string m_pendulumBarPrimitiveName { "__pendulum_bar__" };
    int m_selectedPendulum { -1 };

    float m_simulationTime { 0.0f };
    bool m_runtimeLoadAutoTest { false };
    bool m_runtimeLoadTriggered { false };

    float m_sceneInspectCooldown { 0.0f };

    struct FrameStats {
        float frameTimeMs { 0.0f };
        float avgFrameTimeMs { 0.0f };
        float minFrameTimeMs { 0.0f };
        float maxFrameTimeMs { 0.0f };
        float instantFps { 0.0f };
        float avgFps { 0.0f };
        RenderStats render;

        struct GpuMemory {
            bool supported { false };
            float usedMB { 0.0f };
            float totalMB { 0.0f };
        } gpuMemory;
    };

    static constexpr std::size_t kFrameTimeHistorySize = 240;
    std::vector<float> m_frameTimeHistory;
    FrameStats m_frameStats;
    enum class GpuMemoryQueryMode { Uninitialized, NVX, Unsupported };
    GpuMemoryQueryMode m_gpuMemoryQueryMode { GpuMemoryQueryMode::Uninitialized };

    glm::mat4 m_projectionMatrix = glm::perspective(glm::radians(80.0f), 1.0f, 0.1f, 100.0f);

    bool m_showCrosshair { true };
    bool m_crosshairToggleHeld { false };
    bool m_leftMouseHeld { false };
    bool m_rightMouseHeld { false };
    enum class ActiveDragButton { None, Left, Right };
    ActiveDragButton m_activeDragButton { ActiveDragButton::None };
    bool m_isDraggingSelection { false };
    float m_maxPickDistance { 100.0f };

    struct PendulumDragState {
        std::size_t pendulumIndex { 0 };
        bool wasRunning { false };
        bool wasPaused { false };
    };
    std::optional<PendulumDragState> m_pendulumDragState;

    // Minimap
    Minimap m_minimap;
    bool m_showMinimap { true };
    float m_minimapCamHeight { 200.0f };
    float m_minimapAreaSize { 512.0f };

    // Water
    WaterRenderer m_water;
    DebugUiManager::TabHandle m_tabWater;
};

// ---------------- Implementation ----------------

Application::Application(std::optional<std::filesystem::path> initialScene)
    : m_window("Final Project", glm::ivec2(1920, 1080), OpenGLVersion::GL45)
    , m_cameraStage(m_window, [](const glm::vec3&) { return 0.0f; })
    , m_shadingStage(std::filesystem::path(RESOURCE_ROOT "/shaders"))
    , m_environmentManager(std::filesystem::path(RESOURCE_ROOT "/shaders"))
    , m_meshManager(std::filesystem::path(RESOURCE_ROOT "resources"))
    , m_pendulumManager()
    , m_minimap(512)
{

    if (std::getenv("APP_RUNTIME_LOAD_TEST") != nullptr)
        m_runtimeLoadAutoTest = true;

    m_meshManager.addPrimitiveSphere(m_pendulumNodePrimitiveName, 1.0f);
    m_meshManager.addPrimitiveCube(m_pendulumBarPrimitiveName, 1.0f);

    if (initialScene) {
        loadSceneFromPath(*initialScene);
        if (!m_lastModelLoadSuccess)
            m_meshManager.loadMeshFromPath(std::filesystem::path(RESOURCE_ROOT "resources/dragon.obj"));
    } else {
        m_meshManager.loadMeshFromPath(std::filesystem::path(RESOURCE_ROOT "resources/dragon.obj"));
    }

    const glm::ivec2 framebuffer = m_window.getFrameBufferSize();
    glViewport(0, 0, framebuffer.x, framebuffer.y);

    m_cameraEffectsStage.initialize(std::filesystem::path(RESOURCE_ROOT "/shaders"), framebuffer);
    m_window.registerWindowResizeCallback([this](const glm::ivec2&) {
        const glm::ivec2 fbSize = m_window.getFrameBufferSize();
        glViewport(0, 0, fbSize.x, fbSize.y);
        m_projectionMatrix = glm::perspective(glm::radians(m_activeCameraFov), m_window.getAspectRatio(), 0.1f, 100.0f);
        m_cameraEffectsStage.resize(fbSize);
    });

    initLightCube();
    buildLightCubeShader();

    m_environmentManager.initializeGL();
    m_cameraEffectsStage.resize(framebuffer);
    m_projectionMatrix = glm::perspective(glm::radians(m_activeCameraFov), m_window.getAspectRatio(), 0.1f, 100.0f);

    // Particles GL init
    m_particles.initGL(); // <<< ADDED

    // Water init
    m_water.initGL(std::filesystem::path(RESOURCE_ROOT "/shaders"));

    m_sunPathController.setLightManager(&m_lightManager);
    m_pathRenderer.initialize(std::filesystem::path(RESOURCE_ROOT "/shaders"));
    m_cameraPathRenderer.initialize(std::filesystem::path(RESOURCE_ROOT "/shaders"));
    m_cameraPathPlayer.setPath(&m_cameraPath);
    m_cameraPathPlayer.setSpeed(m_cameraPathPlaybackSpeed);
    m_activeCameraFov = m_defaultCameraFov;

    // Initialize player state.
    m_player.setPosition(glm::vec3(0, 10, 0));

    registerDebugTabs();
}

void Application::registerDebugTabs()
{
    m_tabEnvironment = m_debugUi.registerTab({
        .id = "environment",
        .label = "Environment",
        .draw = [this]() {
            ImGui::PushID("EnvironmentTab");
            drawEnvironmentPanel();
            ImGui::PopID();
        },
        .order = 0,
    });

    // Particles tab
    m_tabParticles = m_debugUi.registerTab({
        .id = "particles",
        .label = "Particles",
        .draw = [this]() {
            ImGui::PushID("ParticlesTab");
            drawParticlesPanel();
            ImGui::PopID();
        },
        .order = 1,
    });

    m_tabCamera = m_debugUi.registerTab({
        .id = "camera",
        .label = "Camera",
        .draw = [this]() {
            ImGui::PushID("CameraTab");
            m_cameraStage.drawImGuiPanel();
            ImGui::Separator();
            ImGui::TextUnformatted("Player");
            ImGui::Indent();
            drawPlayerPanel();
            ImGui::Unindent();
            ImGui::PopID();
        },
        .order = 2,
    });

    m_tabCameraFx = m_debugUi.registerTab({
        .id = "camera_effects",
        .label = "CameraEffects",
        .draw = [this]() {
            ImGui::PushID("CameraFxTab");
            m_cameraEffectsStage.drawImGuiPanel(m_cameraEffectsSettings);
            ImGui::PopID();
        },
        .order = 3,
    });

    m_tabMeshes = m_debugUi.registerTab({
        .id = "meshes",
        .label = "Meshes",
        .draw = [this]() {
            ImGui::PushID("MeshTab");
            drawScenePanel();
            ImGui::Separator();
            m_meshManager.drawImGuiPanel();
            ImGui::PopID();
        },
        .order = 4,
    });

    // Water tab
    m_tabWater = m_debugUi.registerTab({
        .id = "water",
        .label = "Water",
        .draw = [this]() {
            ImGui::PushID("WaterTab");
            m_water.drawImGuiPanel();
            ImGui::PopID();
        },
        .order = 5, // show near materials
    });

    m_tabShading = m_debugUi.registerTab({
        .id = "materials",
        .label = "Materials",
        .draw = [this]() {
            ImGui::PushID("ShadingTab");
            m_shadingStage.drawImGuiPanel(m_meshManager.instances(), m_meshManager.selectedInstanceIndex());
            ImGui::PopID();
        },
        .order = 5,
    });

    m_tabLights = m_debugUi.registerTab({
        .id = "lights",
        .label = "Lights",
        .draw = [this]() {
            ImGui::PushID("LightTab");
            m_lightManager.drawImGuiPanel();
            ImGui::PopID();
        },
        .order = 6,
    });

    m_tabShadows = m_debugUi.registerTab({
        .id = "shadows",
        .label = "Shadows",
        .draw = [this]() {
            ImGui::PushID("ShadowsTab");
            m_lightManager.drawShadowDebugPanel();
            ImGui::PopID();
        },
        .order = 7,
    });

    m_tabPaths = m_debugUi.registerTab({
        .id = "paths",
        .label = "Paths",
        .draw = [this]() {
            ImGui::PushID("PathsTab");
            drawPathsPanel();
            ImGui::PopID();
        },
        .order = 8,
    });

    m_tabFloor = m_debugUi.registerTab({
        .id = "floor",
        .label = "Floor",
        .draw = [this]() {
            ImGui::PushID("FloorTab");
            m_floor.drawImGuiPanel();
            ImGui::PopID();
        },
        .order = 9,
    });

    m_tabPendulums = m_debugUi.registerTab({
        .id = "pendulums",
        .label = "Pendulums",
        .draw = [this]() {
            ImGui::PushID("PendulumTab");
            drawPendulumPanel();
            ImGui::PopID();
        },
        .order = 9,
    });

    m_tabSelection = m_debugUi.registerTab({
        .id = "selection",
        .label = "Selection",
        .draw = [this]() {
            ImGui::PushID("SelectionTab");
            drawSelectionPanel();
            ImGui::PopID();
        },
        .order = 10,
    });

    m_tabMinimap = m_debugUi.registerTab({
        .id = "minimap",
        .label = "Minimap",
        .draw = [this]() {
            ImGui::PushID("MinimapTab");
            ImGui::Checkbox("Show Minimap", &m_showMinimap);
            if (ImGui::SliderFloat("Area Size (world units)", &m_minimapAreaSize, 8.0f, 8192.0f)) {
                // user changed coverage; used on next render
            }
            ImGui::PopID();
        },
        .order = 10,
    });

    m_tabPerformance = m_debugUi.registerTab({
        .id = "performance",
        .label = "Performance",
        .draw = [this]() {
            ImGui::PushID("PerformanceTab");
            drawPerformancePanel();
            ImGui::PopID();
        },
        .order = 11,
    });

}

void Application::beginFrameStats(float deltaTime)
{
    const float frameTimeMs = deltaTime * 1000.0f;
    m_frameStats.frameTimeMs = frameTimeMs;
    m_frameStats.instantFps = (deltaTime > 0.0f) ? (1.0f / deltaTime) : 0.0f;

    if (m_frameTimeHistory.size() >= kFrameTimeHistorySize)
        m_frameTimeHistory.erase(m_frameTimeHistory.begin());
    m_frameTimeHistory.push_back(frameTimeMs);

    float minTime = frameTimeMs;
    float maxTime = frameTimeMs;
    float totalTime = 0.0f;
    for (float sample : m_frameTimeHistory) {
        minTime = std::min(minTime, sample);
        maxTime = std::max(maxTime, sample);
        totalTime += sample;
    }

    const float avgTime = !m_frameTimeHistory.empty() ? (totalTime / static_cast<float>(m_frameTimeHistory.size())) : frameTimeMs;
    m_frameStats.avgFrameTimeMs = avgTime;
    m_frameStats.minFrameTimeMs = minTime;
    m_frameStats.maxFrameTimeMs = maxTime;
    m_frameStats.avgFps = (avgTime > 0.0f) ? (1000.0f / avgTime) : m_frameStats.instantFps;

    m_frameStats.render.reset();
}

void Application::finalizeFrameStats()
{
    updateGpuMemoryStats();
}

void Application::updateGpuMemoryStats()
{
    auto& gpu = m_frameStats.gpuMemory;

    if (m_gpuMemoryQueryMode == GpuMemoryQueryMode::Uninitialized) {
#ifdef GLAD_GL_NVX_gpu_memory_info
        if (GLAD_GL_NVX_gpu_memory_info) {
            m_gpuMemoryQueryMode = GpuMemoryQueryMode::NVX;
        } else {
            m_gpuMemoryQueryMode = GpuMemoryQueryMode::Unsupported;
        }
#else
        m_gpuMemoryQueryMode = GpuMemoryQueryMode::Unsupported;
#endif
    }

    if (m_gpuMemoryQueryMode == GpuMemoryQueryMode::NVX) {
        GLint totalKB = 0;
        GLint currentKB = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalKB);
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &currentKB);

        const GLint usedKB = std::max(0, totalKB - currentKB);
        gpu.supported = true;
        gpu.totalMB = static_cast<float>(totalKB) / 1024.0f;
        gpu.usedMB = static_cast<float>(usedKB) / 1024.0f;
    } else {
        gpu.supported = false;
        gpu.totalMB = 0.0f;
        gpu.usedMB = 0.0f;
    }
}

void Application::drawPerformancePanel()
{
    const FrameStats& stats = m_frameStats;

    ImGui::Text("Frame Time: %.2f ms (%.1f FPS)", stats.frameTimeMs, stats.instantFps);
    ImGui::Text("Average: %.2f ms (%.1f FPS)", stats.avgFrameTimeMs, stats.avgFps);
    ImGui::Text("Min / Max: %.2f / %.2f ms", stats.minFrameTimeMs, stats.maxFrameTimeMs);

    if (!m_frameTimeHistory.empty()) {
        const float maxSample = *std::max_element(m_frameTimeHistory.begin(), m_frameTimeHistory.end());
        const float upper = std::max({ maxSample, stats.avgFrameTimeMs, stats.frameTimeMs, 1.0f }) * 1.2f;
        ImGui::PlotLines("Frame Time History (ms)",
            m_frameTimeHistory.data(),
            static_cast<int>(m_frameTimeHistory.size()),
            0,
            nullptr,
            0.0f,
            upper,
            ImVec2(ImGui::GetContentRegionAvail().x, 120.0f));
    }
}

void Application::drawScenePanel()
{
    if (ImGui::Checkbox("Show Ground", &m_showGround)) {
        if (!m_showGround) {
            glm::vec3 feetPos = m_player.position();
            feetPos.y = 0.0f;
            m_player.setPosition(feetPos);
        }
    }

    bool activateLoad = false;
    if (ImGui::InputText("Model Path", m_modelPathBuffer.data(), m_modelPathBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
        activateLoad = true;

    ImGui::SameLine();
    if (ImGui::Button("Browse")) {
        if (auto path = pickOpenFile("gltf,glb")) {
            setModelPathBuffer(*path);
            activateLoad = true;
        }
    }

    if (ImGui::Button("Load glTF"))
        activateLoad = true;

    if (activateLoad) {
        const std::string bufferValue = m_modelPathBuffer.data();
        loadSceneFromPath(bufferValue);
    }

    if (!m_modelLoadMessage.empty()) {
        const ImVec4 color = m_lastModelLoadSuccess ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(color, "%s", m_modelLoadMessage.c_str());
    }

    if (!m_lastModelPath.empty())
        ImGui::Text("Last loaded: %s", m_lastModelPath.filename().string().c_str());
}

void Application::drawEnvironmentPanel()
{
    bool useIBL = m_environmentManager.useIBL();
    if (ImGui::Checkbox("Use IBL Lighting", &useIBL))
        m_environmentManager.setUseIBL(useIBL);

    bool showSkybox = m_environmentManager.skyboxVisible();
    if (ImGui::Checkbox("Show Skybox", &showSkybox))
        m_environmentManager.setSkyboxVisible(showSkybox);

    float intensity = m_environmentManager.environmentIntensity();
    if (ImGui::SliderFloat("Environment Intensity", &intensity, 0.0f, 10.0f))
        m_environmentManager.setEnvironmentIntensity(intensity);

    bool loadEnvironment = false;
    if (ImGui::InputText("HDR Path", m_environmentPathBuffer.data(), m_environmentPathBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
        loadEnvironment = true;

    ImGui::SameLine();
    if (ImGui::Button("Browse HDR")) {
        if (auto path = pickOpenFile("hdr")) {
            setEnvironmentPathBuffer(*path);
            loadEnvironment = true;
        }
    }

    if (ImGui::Button("Load Environment"))
        loadEnvironment = true;

    if (loadEnvironment) {
        const std::string bufferValue = m_environmentPathBuffer.data();
        loadEnvironmentFromPath(bufferValue);
    }

    if (!m_environmentLoadMessage.empty()) {
        const ImVec4 color = m_environmentLoadSuccess ? ImVec4(0.25f, 0.8f, 0.25f, 1.0f) : ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
        ImGui::TextColored(color, "%s", m_environmentLoadMessage.c_str());
    }

    if (ImGui::CollapsingHeader("Advanced Settings")) {
        auto settings = m_environmentManager.advancedSettings();
        bool dirty = false;
        dirty |= ImGui::InputInt("Environment Resolution", &settings.environmentResolution);
        dirty |= ImGui::InputInt("Irradiance Resolution", &settings.irradianceResolution);
        dirty |= ImGui::InputInt("Prefilter Resolution", &settings.prefilterBaseResolution);
        if (dirty) {
            settings.environmentResolution = std::clamp(settings.environmentResolution, 64, 4096);
            settings.irradianceResolution = std::clamp(settings.irradianceResolution, 16, 1024);
            settings.prefilterBaseResolution = std::clamp(settings.prefilterBaseResolution, 64, 1024);
            const int baseResolution = std::max(settings.prefilterBaseResolution, 1);
            const int maxPrefilterMipLevels = static_cast<int>(std::floor(std::log2(static_cast<double>(baseResolution)))) + 1;
            settings.prefilterMipLevels = std::clamp(settings.prefilterMipLevels, 1, maxPrefilterMipLevels);
            try {
                m_environmentManager.setAdvancedSettings(settings);
                if (m_environmentManager.hasEnvironment()) {
                    setEnvironmentPathBuffer(m_environmentManager.currentEnvironmentPath());
                    m_environmentLoadMessage = "Rebuilt environment with new settings.";
                    m_environmentLoadSuccess = true;
                }
            } catch (const std::exception& ex) {
                m_environmentLoadMessage = ex.what();
                m_environmentLoadSuccess = false;
            }
        }
    }

    if (m_environmentManager.hasEnvironment()) {
        ImGui::Text("Active Environment: %s", m_environmentManager.currentEnvironmentPath().filename().string().c_str());
    }

    // Visual effects: world curvature and fog (moved from Particles tab)
    if (ImGui::CollapsingHeader("Visual Effects")) {
        // World curvature toggle (applies a view-space curvature to all geometry)
        static bool worldCurvatureEnabled = false;
        static float worldCurvatureStrength = 0.001f;
        if (ImGui::Checkbox("World Curvature", &worldCurvatureEnabled)) {
            m_shadingStage.setWorldCurvatureEnabled(worldCurvatureEnabled);
            m_floor.setWorldCurvatureEnabled(worldCurvatureEnabled);
        }
        if (ImGui::SliderFloat("Curvature Strength", &worldCurvatureStrength, 0.0f, 0.01f, "%.5f")) {
            m_shadingStage.setWorldCurvatureStrength(worldCurvatureStrength);
            m_floor.setWorldCurvatureStrength(worldCurvatureStrength);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Fog");
        static bool fogEnabled = false;
        static glm::vec3 fogColor = glm::vec3(0.6f, 0.7f, 0.9f);
        static float fogDensity = 0.01f;
        static float fogGradient = 1.8f;
        if (ImGui::Checkbox("Enable Fog", &fogEnabled)) {
            m_shadingStage.setFogEnabled(fogEnabled);
            m_floor.setFogEnabled(fogEnabled);
        }
        if (ImGui::ColorEdit3("Fog Color", glm::value_ptr(fogColor))) {
            m_shadingStage.setFogColor(fogColor);
            m_floor.setFogColor(fogColor);
        }
        if (ImGui::SliderFloat("Fog Density", &fogDensity, 0.0f, 0.1f, "%.4f")) {
            m_shadingStage.setFogDensity(fogDensity);
            m_floor.setFogDensity(fogDensity);
        }
        if (ImGui::SliderFloat("Fog Gradient", &fogGradient, 0.5f, 4.0f, "%.2f")) {
            m_shadingStage.setFogGradient(fogGradient);
            m_floor.setFogGradient(fogGradient);
        }
    }
}

void Application::drawPlayerPanel()
{
    auto params = m_player.params();
    if (ImGui::SliderFloat("Gravity", &params.gravity, -50.0f, -1.0f))
        m_player.setParams(params);
    if (ImGui::SliderFloat("Jump Impulse", &params.jumpImpulse, 1.0f, 30.0f))
        m_player.setParams(params);
    ImGui::Text("Grounded: %s", m_player.grounded() ? "Yes" : "No");
    ImGui::Text("Position: (%.2f %.2f %.2f)", static_cast<double>(m_player.position().x), static_cast<double>(m_player.position().y), static_cast<double>(m_player.position().z));
}

void Application::drawPathsPanel()
{
    bool enabled = m_sunPathController.enabled();
    if (ImGui::Checkbox("Enable Sun Path", &enabled))
        m_sunPathController.setEnabled(enabled);

    ImGui::SameLine();
    bool paused = m_sunPathController.paused();
    if (ImGui::Checkbox("Paused", &paused))
        m_sunPathController.setPaused(paused);

    int playbackIndex = (m_sunPathController.playbackMode() == PathPlaybackMode::Loop) ? 0 : 1;
    if (ImGui::Combo("Playback Mode", &playbackIndex, "Loop\0Ping-Pong\0"))
        m_sunPathController.setPlaybackMode(playbackIndex == 0 ? PathPlaybackMode::Loop : PathPlaybackMode::PingPong);

    int lightStyle = (m_sunPathController.lightStyle() == SunPathController::LightStyle::Spot) ? 0 : 1;
    if (ImGui::Combo("Sun Light Type", &lightStyle, "Spot\0Point\0"))
        m_sunPathController.setLightStyle(lightStyle == 0 ? SunPathController::LightStyle::Spot : SunPathController::LightStyle::Point);

    float speed = m_sunPathController.speed();
    if (ImGui::SliderFloat("Speed (units/sec)", &speed, 0.0f, 50.0f))
        m_sunPathController.setSpeed(speed);

    float timeScale = m_sunPathController.timeScale();
    if (ImGui::SliderFloat("Time Scale", &timeScale, 0.0f, 5.0f))
        m_sunPathController.setTimeScale(timeScale);

    float size = m_sunPathController.size();
    if (ImGui::SliderFloat("Size", &size, 0.5f, 200.0f))
        m_sunPathController.setSize(size);

    float height = m_sunPathController.height();
    if (ImGui::SliderFloat("Height", &height, -50.0f, 50.0f))
        m_sunPathController.setHeight(height);

    float rotation = m_sunPathController.rotationDegrees();
    if (ImGui::SliderFloat("Rotation", &rotation, -180.0f, 180.0f))
        m_sunPathController.setRotationDegrees(rotation);

    int planeIndex = 0;
    switch (m_sunPathController.plane()) {
    case SunPathController::Plane::XZ: planeIndex = 0; break;
    case SunPathController::Plane::XY: planeIndex = 1; break;
    case SunPathController::Plane::YZ: planeIndex = 2; break;
    }
    if (ImGui::Combo("Plane", &planeIndex, "XZ (Ground)\0XY (Front)\0YZ (Side)\0")) {
        SunPathController::Plane plane = SunPathController::Plane::XZ;
        if (planeIndex == 1) plane = SunPathController::Plane::XY;
        else if (planeIndex == 2) plane = SunPathController::Plane::YZ;
        m_sunPathController.setPlane(plane);
    }

    bool renderCurve = m_sunPathController.renderCurve();
    if (ImGui::Checkbox("Render Curve", &renderCurve))
        m_sunPathController.setRenderCurve(renderCurve);
    bool showControl = m_sunPathController.showControlPoints();
    if (ImGui::Checkbox("Show Control Points", &showControl))
        m_sunPathController.setShowControlPoints(showControl);
    bool showTangents = m_sunPathController.showTangents();
    if (ImGui::Checkbox("Show Tangents", &showTangents))
        m_sunPathController.setShowTangents(showTangents);

    float progress = m_sunPathController.normalizedPosition();
    if (ImGui::SliderFloat("Progress", &progress, 0.0f, 1.0f))
        m_sunPathController.scrubTo(progress);
    ImGui::SameLine();
    if (ImGui::Button("Reset Progress"))
        m_sunPathController.scrubTo(0.0f);

    if (m_sunPathController.enabled() && m_sunPathController.hasPath()) {
        const auto& sample = m_sunPathController.lastSample();
        ImGui::Separator();
        ImGui::Text("Sun Position: (%.2f, %.2f, %.2f)", sample.position.x, sample.position.y, sample.position.z);
        ImGui::Text("Sun Tangent: (%.2f, %.2f, %.2f)", sample.tangent.x, sample.tangent.y, sample.tangent.z);
    }

    ImGui::Separator();
    drawCameraPathPanel();
}

CameraKeyframe Application::captureCurrentCameraKeyframe(float timeSeconds) const
{
    CameraKeyframe keyframe;
    keyframe.time = timeSeconds;
    keyframe.fov = m_activeCameraFov;

    const glm::mat4 viewMatrix = m_cameraStage.getViewMatrix();
    const glm::mat4 inverseView = glm::inverse(viewMatrix);
    keyframe.position = glm::vec3(inverseView[3]);
    keyframe.rotation = glm::normalize(glm::quat_cast(inverseView));
    if (!std::isfinite(keyframe.rotation.x) || !std::isfinite(keyframe.rotation.y) || !std::isfinite(keyframe.rotation.z) || !std::isfinite(keyframe.rotation.w))
        keyframe.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    return keyframe;
}

void Application::drawCameraPathPanel()
{

    bool showPath = m_cameraPathVisible;
    if (ImGui::Checkbox("Show Path", &showPath))
        m_cameraPathVisible = showPath;

    ImGui::SameLine();
    bool showKeys = m_cameraPathShowKeyframes;
    if (ImGui::Checkbox("Show Keyframes", &showKeys))
        m_cameraPathShowKeyframes = showKeys;

    ImGui::SameLine();
    bool showTangents = m_cameraPathShowTangents;
    if (ImGui::Checkbox("Show Tangents", &showTangents))
        m_cameraPathShowTangents = showTangents;

    bool loopEnabled = m_cameraPath.loopEnabled();
    if (ImGui::Checkbox("Loop", &loopEnabled))
        m_cameraPath.setLoopEnabled(loopEnabled);

    bool followCamera = m_cameraPathFollowCamera;
    if (ImGui::Checkbox("Follow Camera", &followCamera)) {
        m_cameraPathFollowCamera = followCamera;
        if (followCamera && m_cameraStage.getMode() != CameraStage::Mode::FreeCam)
            m_cameraStage.setMode(CameraStage::Mode::FreeCam);
    }

    float playbackSpeed = m_cameraPathPlaybackSpeed;
    if (ImGui::SliderFloat("Playback Speed", &playbackSpeed, 0.1f, 10.0f, "%.2fx")) {
        m_cameraPathPlaybackSpeed = playbackSpeed;
        m_cameraPathPlayer.setSpeed(m_cameraPathPlaybackSpeed);
    }

    if (ImGui::Button(m_cameraPathPlayer.playing() ? "Pause" : "Play")) {
        if (m_cameraPathPlayer.playing())
            m_cameraPathPlayer.pause();
        else if (m_cameraPath.keyCount() >= 2)
            m_cameraPathPlayer.play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        m_cameraPathPlayer.stop();
        if (m_cameraPathFollowCamera && m_cameraPath.keyCount() > 0)
            m_cameraPathPlayer.applyToCamera(m_cameraStage.getFpsCamera());
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        if (m_cameraPath.keyCount() > 0) {
            m_cameraPathPlayer.setPlayhead(m_cameraPath.startTime());
            if (m_cameraPathFollowCamera)
                m_cameraPathPlayer.applyToCamera(m_cameraStage.getFpsCamera());
        }
    }

    const std::size_t keyCount = m_cameraPath.keyCount();
    const float defaultNewTime = keyCount == 0 ? 0.0f : (m_cameraPath.key(keyCount - 1).time + 1.0f);
    if (ImGui::Button("Add Keyframe (Current Camera)")) {
        const std::size_t inserted = m_cameraPath.addKeyframe(captureCurrentCameraKeyframe(defaultNewTime));
        m_cameraPathSelectedIndex = inserted;
        m_cameraPathPlayer.setPlayhead(m_cameraPath.key(inserted).time);
    }
    ImGui::SameLine();
    if (ImGui::Button("Insert At Playhead")) {
        const float playheadTime = m_cameraPathPlayer.playhead();
        const std::size_t inserted = m_cameraPath.addKeyframe(captureCurrentCameraKeyframe(playheadTime));
        m_cameraPathSelectedIndex = inserted;
        m_cameraPathPlayer.setPlayhead(m_cameraPath.key(inserted).time);
    }

    if (keyCount >= 2) {
        const float start = m_cameraPath.startTime();
        const float end = m_cameraPath.endTime();
        const float range = end - start;
        const float denom = glm::max(range, 1e-4f);
        float normalized = glm::clamp((m_cameraPathPlayer.playhead() - start) / denom, 0.0f, 1.0f);
        if (ImGui::SliderFloat("Playhead", &normalized, 0.0f, 1.0f)) {
            const float newTime = start + normalized * denom;
            m_cameraPathPlayer.setPlayhead(newTime);
            if (!m_cameraPathPlayer.playing() && m_cameraPathFollowCamera)
                m_cameraPathPlayer.applyToCamera(m_cameraStage.getFpsCamera());
        }
        ImGui::Text("Keys: %zu | Time Range: %.2f -> %.2f (%.2fs)", keyCount, start, end, range);
    } else {
        ImGui::Text("Keys: %zu (need at least 2 to play)", keyCount);
    }

    bool requestRefresh = false;
    bool removedKey = false;

    if (keyCount > 0) {
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("CameraPathKeyframes", 5, tableFlags)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
            ImGui::TableSetupColumn("Time");
            ImGui::TableSetupColumn("FOV");
            ImGui::TableSetupColumn("Position");
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableHeadersRow();

            for (std::size_t i = 0; i < keyCount; ++i) {
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(i));
                bool exitLoop = false;

                const bool selected = m_cameraPathSelectedIndex && *m_cameraPathSelectedIndex == i;
                if (selected)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.25f, 0.45f, 0.9f, 0.2f)));

                const CameraKeyframe current = m_cameraPath.key(i);

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", i);
                if (ImGui::IsItemClicked())
                    m_cameraPathSelectedIndex = i;

                ImGui::TableSetColumnIndex(1);
                float timeValue = current.time;
                if (ImGui::InputFloat("##Time", &timeValue, 0.1f, 1.0f, "%.3f")) {
                    if (std::abs(timeValue - current.time) > 1e-4f) {
                        CameraKeyframe updated = current;
                        updated.time = timeValue;
                        m_cameraPath.updateKeyframe(i, updated);
                        requestRefresh = true;
                        exitLoop = true;
                    }
                }

                if (exitLoop) {
                    ImGui::PopID();
                    break;
                }

                ImGui::TableSetColumnIndex(2);
                float fovValue = current.fov;
                if (ImGui::SliderFloat("##Fov", &fovValue, 10.0f, 150.0f, "%.1f")) {
                    if (std::abs(fovValue - current.fov) > 1e-3f) {
                        CameraKeyframe updated = current;
                        updated.fov = fovValue;
                        m_cameraPath.updateKeyframe(i, updated);
                        requestRefresh = true;
                        exitLoop = true;
                    }
                }

                if (exitLoop) {
                    ImGui::PopID();
                    break;
                }

                ImGui::TableSetColumnIndex(3);
                glm::vec3 position = current.position;
                if (ImGui::InputFloat3("##Pos", &position.x, "%.3f")) {
                    if (glm::length2(position - current.position) > 1e-6f) {
                        CameraKeyframe updated = current;
                        updated.position = position;
                        m_cameraPath.updateKeyframe(i, updated);
                        requestRefresh = true;
                        exitLoop = true;
                    }
                }

                if (exitLoop) {
                    ImGui::PopID();
                    break;
                }

                ImGui::TableSetColumnIndex(4);
                if (ImGui::SmallButton("Capture##Capture")) {
                    CameraKeyframe updated = captureCurrentCameraKeyframe(current.time);
                    m_cameraPath.updateKeyframe(i, updated);
                    requestRefresh = true;
                    exitLoop = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Go##Go")) {
                    m_cameraPathPlayer.setPlayhead(current.time);
                    if (!m_cameraPathPlayer.playing() && m_cameraPathFollowCamera)
                        m_cameraPathPlayer.applyToCamera(m_cameraStage.getFpsCamera());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete##Delete")) {
                    m_cameraPath.removeKeyframe(i);
                    removedKey = true;
                    if (m_cameraPathSelectedIndex && *m_cameraPathSelectedIndex >= m_cameraPath.keyCount())
                        m_cameraPathSelectedIndex.reset();
                    exitLoop = true;
                }

                ImGui::PopID();
                if (exitLoop)
                    break;
            }

            ImGui::EndTable();
        }
    }

    if (removedKey || requestRefresh)
        m_cameraPathPlayer.setPlayhead(m_cameraPathPlayer.playhead());

    if (m_cameraPathLastSample) {
        const auto& sample = *m_cameraPathLastSample;
        ImGui::Separator();
        ImGui::Text("Current Sample");
        ImGui::Text("Position: (%.2f, %.2f, %.2f)", sample.position.x, sample.position.y, sample.position.z);
        const glm::vec3 forward = glm::rotate(sample.rotation, glm::vec3(0.0f, 0.0f, -1.0f));
        ImGui::Text("Forward: (%.2f, %.2f, %.2f)", forward.x, forward.y, forward.z);
        ImGui::Text("FOV: %.1f deg", sample.fov);
    }
}

void Application::rebuildCameraPathBezier()
{
    const std::uint64_t version = m_cameraPath.version();
    if (m_cameraPathBezierVersion == version)
        return;

    std::vector<CubicBezier> segments;
    const std::size_t keyCount = m_cameraPath.keyCount();

    if (keyCount >= 2) {
        const bool loop = m_cameraPath.loopEnabled();
        const std::size_t segmentCount = loop ? keyCount : (keyCount - 1);

        auto getKey = [&](std::ptrdiff_t index) -> const CameraKeyframe& {
            if (loop) {
                const std::ptrdiff_t wrapped = (index % static_cast<std::ptrdiff_t>(keyCount) + static_cast<std::ptrdiff_t>(keyCount)) % static_cast<std::ptrdiff_t>(keyCount);
                return m_cameraPath.key(static_cast<std::size_t>(wrapped));
            }
            if (index <= 0)
                return m_cameraPath.key(0);
            if (index >= static_cast<std::ptrdiff_t>(keyCount) - 1)
                return m_cameraPath.key(keyCount - 1);
            return m_cameraPath.key(static_cast<std::size_t>(index));
        };

        segments.reserve(segmentCount);
        for (std::size_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
            const std::ptrdiff_t base = static_cast<std::ptrdiff_t>(segmentIndex);
            const glm::vec3 p0 = getKey(base - 1).position;
            const glm::vec3 p1 = getKey(base).position;
            const glm::vec3 p2 = getKey(base + 1).position;
            const glm::vec3 p3 = getKey(base + 2).position;

            CubicBezier bezier;
            bezier.p0 = p1;
            bezier.p3 = p2;
            bezier.p1 = p1 + (p2 - p0) / 6.0f;
            bezier.p2 = p2 - (p3 - p1) / 6.0f;
            segments.push_back(bezier);
        }
    }

    m_cameraPathBezier.setSegments(std::move(segments));
    m_cameraPathBezierVersion = version;
}

void Application::renderCameraPathDebug(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, RenderStats& stats)
{
    if (!m_cameraPathVisible || m_cameraPath.keyCount() < 2)
        return;

    rebuildCameraPathBezier();
    if (m_cameraPathBezier.segmentCount() == 0 || m_cameraPathBezier.totalLength() <= 0.0f)
        return;

    m_cameraPathRenderer.updateGeometry(m_cameraPathBezier, m_cameraPathBezierVersion);

    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glDepthMask(GL_FALSE);

    m_cameraPathRenderer.drawCurve(viewMatrix, projectionMatrix, glm::vec3(0.95f, 0.45f, 0.15f));
    stats.addDraw(1, 0);

    if (m_cameraPathShowTangents) {
        m_cameraPathRenderer.drawTangents(viewMatrix, projectionMatrix, glm::vec3(0.25f, 0.8f, 1.0f));
        stats.addDraw(1, 0);
    }

    if (m_cameraPathShowKeyframes) {
        glEnable(GL_PROGRAM_POINT_SIZE);
        m_cameraPathRenderer.drawControlPoints(viewMatrix, projectionMatrix, glm::vec3(1.0f, 1.0f, 0.3f), 7.0f);
        glDisable(GL_PROGRAM_POINT_SIZE);
        stats.addDraw(1, 0);
    }

    glDepthMask(depthMask);

    if (!m_cameraPathShowKeyframes)
        return;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
        return;

    auto* drawList = ImGui::GetForegroundDrawList();
    const glm::mat4 viewProjection = projectionMatrix * viewMatrix;

    const auto projectPoint = [&](const glm::vec3& world) -> std::optional<ImVec2> {
        const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f)
            return std::nullopt;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < 0.0f || ndc.z > 1.0f)
            return std::nullopt;
        ImVec2 screen;
        screen.x = (ndc.x * 0.5f + 0.5f) * displaySize.x;
        screen.y = (-ndc.y * 0.5f + 0.5f) * displaySize.y;
        return screen;
    };

    for (std::size_t i = 0; i < m_cameraPath.keyCount(); ++i) {
        const glm::vec3 position = m_cameraPath.key(i).position;
        if (auto screen = projectPoint(position)) {
            const bool selected = m_cameraPathSelectedIndex && *m_cameraPathSelectedIndex == i;
            const float radius = selected ? 7.0f : 5.0f;
            const ImU32 colorFill = selected ? IM_COL32(70, 180, 255, 220) : IM_COL32(245, 210, 70, 220);
            const ImU32 colorOutline = IM_COL32(10, 10, 10, 220);
            drawList->AddCircleFilled(*screen, radius, colorFill, 16);
            drawList->AddCircle(*screen, radius, colorOutline, 16, 1.5f);
        }
    }

    if (m_cameraPathLastSample) {
        if (auto screen = projectPoint(m_cameraPathLastSample->position)) {
            const float radius = 6.5f;
            drawList->AddCircle(*screen, radius, IM_COL32(255, 120, 60, 230), 20, 2.0f);
        }
    }
}

void Application::drawPendulumPanel()
{
    ImGui::TextUnformatted("Pendulum Simulation");
    ImGui::Separator();

    static int createNodeCount = 6;
    createNodeCount = std::clamp(createNodeCount, 1, 64);
    if (ImGui::InputInt("New Pendulum Nodes", &createNodeCount))
        createNodeCount = std::clamp(createNodeCount, 1, 64);

    if (ImGui::Button("Create Pendulum")) {
        const std::size_t index = m_pendulumManager.createPendulum("", static_cast<std::size_t>(createNodeCount));
        m_pendulumManager.setRenderMeshes(index, m_pendulumNodePrimitiveName, m_pendulumBarPrimitiveName);
        m_selectedPendulum = static_cast<int>(index);
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Demo (6-node)")) {
        const std::size_t index = m_pendulumManager.createDemoPendulum();
        m_pendulumManager.setRenderMeshes(index, m_pendulumNodePrimitiveName, m_pendulumBarPrimitiveName);
        m_selectedPendulum = static_cast<int>(index);
    }

    auto summaries = m_pendulumManager.summaries();
    if (summaries.empty())
        m_selectedPendulum = -1;
    else if (m_selectedPendulum >= static_cast<int>(summaries.size()))
        m_selectedPendulum = static_cast<int>(summaries.size()) - 1;

    ImGui::Separator();
    ImGui::TextUnformatted("Pendulum List");
    if (ImGui::BeginListBox("##PendulumList", ImVec2(-FLT_MIN, 160.0f))) {
        for (const auto& summary : summaries) {
            const bool isSelected = m_selectedPendulum == static_cast<int>(summary.index);
            if (ImGui::Selectable(summary.name.c_str(), isSelected))
                m_selectedPendulum = static_cast<int>(summary.index);
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndListBox();
    }

    if (m_selectedPendulum < 0)
        return;

    const std::size_t selectedIndex = static_cast<std::size_t>(m_selectedPendulum);
    PendulumManager::PendulumData* pendulum = m_pendulumManager.getPendulum(selectedIndex);
    if (!pendulum)
        return;

    ImGui::Separator();
    ImGui::Text("Selected: %s", pendulum->name.c_str());
    ImGui::Text("Status: %s", pendulum->running ? "Running" : (pendulum->paused ? "Paused" : "Stopped"));
    ImGui::Text("Last step: %.3f ms", pendulum->stats.lastStepMilliseconds);
    ImGui::Text("Accumulator: %.3f ms", pendulum->stats.accumulator * 1000.0);

    if (ImGui::Button("Start"))
        m_pendulumManager.start(selectedIndex);
    ImGui::SameLine();
    if (ImGui::Button("Pause"))
        m_pendulumManager.pause(selectedIndex);
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
        m_pendulumManager.stop(selectedIndex);
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
        m_pendulumManager.resetPendulum(selectedIndex);
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
        m_pendulumManager.removePendulum(selectedIndex);
        m_selectedPendulum = -1;
        return;
    }

    glm::vec3 rootPosition = pendulum->rootPosition;
    if (ImGui::InputFloat3("Root Position", glm::value_ptr(rootPosition)))
        m_pendulumManager.setRootPosition(selectedIndex, rootPosition);

    const bool rootFrozen = pendulum->rootFrozen;
    const char* toggleLabel = rootFrozen ? "Unfreeze Root" : "Freeze Root";
    if (ImGui::Button(toggleLabel))
        m_pendulumManager.setRootFrozen(selectedIndex, !rootFrozen);
    ImGui::SameLine();
    ImGui::Text("Root: %s", rootFrozen ? "Frozen" : "Free");

    PendulumManager::Settings settings = m_pendulumManager.settings();
    bool dirtySettings = false;
    dirtySettings |= ImGui::SliderFloat("Gravity (m/s^2)", &settings.gravity, 0.0f, 25.0f);
    dirtySettings |= ImGui::SliderFloat("Damping", &settings.damping, 0.0f, 0.25f);
    dirtySettings |= ImGui::SliderFloat("Node Radius", &settings.nodeRadius, 0.05f, 0.5f);
    dirtySettings |= ImGui::SliderFloat("Bar Thickness", &settings.barThickness, 0.01f, 0.3f);
    dirtySettings |= ImGui::InputFloat("Fixed Timestep (s)", &settings.fixedTimeStep, 0.0001f, 0.01f, "%.5f");
    settings.fixedTimeStep = std::clamp(settings.fixedTimeStep, 1e-4f, 0.1f);
    int substeps = settings.substeps;
    if (ImGui::SliderInt("Substeps", &substeps, 1, 8)) {
        settings.substeps = std::clamp(substeps, 1, 16);
        dirtySettings = true;
    }

    int integratorIndex = settings.integrator == PendulumManager::Integrator::SemiImplicitEuler ? 0 : 1;
    if (ImGui::Combo("Integrator", &integratorIndex, "Semi-Implicit Euler\0Runge-Kutta 4\0")) {
        settings.integrator = integratorIndex == 0 ? PendulumManager::Integrator::SemiImplicitEuler : PendulumManager::Integrator::RungeKutta4;
        dirtySettings = true;
        m_pendulumManager.setIntegrator(settings.integrator);
    }

    if (dirtySettings)
        m_pendulumManager.setSettings(settings);

    int nodeCount = static_cast<int>(pendulum->nodes.size());
    if (ImGui::InputInt("Node Count", &nodeCount)) {
        nodeCount = std::clamp(nodeCount, 1, 64);
        m_pendulumManager.resizeNodes(selectedIndex, static_cast<std::size_t>(nodeCount));
        pendulum = m_pendulumManager.getPendulum(selectedIndex);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Node Parameters");
    if (ImGui::BeginTable("PendulumNodesTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Node");
        ImGui::TableSetupColumn("Mass (kg)");
        ImGui::TableSetupColumn("Length (m)");
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < pendulum->nodes.size(); ++i) {
            PendulumManager::NodeState& node = pendulum->nodes[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%zu", i);

            ImGui::TableSetColumnIndex(1);
            float mass = node.mass;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::DragFloat("##mass", &mass, 0.01f, 0.01f, 20.0f, "%.2f"))
                m_pendulumManager.setNodeMass(selectedIndex, i, mass);

            ImGui::TableSetColumnIndex(2);
            float length = node.length;
            if (ImGui::DragFloat("##length", &length, 0.01f, 0.05f, 5.0f, "%.2f"))
                m_pendulumManager.setNodeLength(selectedIndex, i, length);
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Render Resources");

    const std::string nodeLabel = pendulum->nodeMeshName.empty() ? std::string("<none>") : pendulum->nodeMeshName;
    if (ImGui::BeginCombo("Node Mesh", nodeLabel.c_str())) {
        for (MeshInstance& instance : m_meshManager.instances()) {
            const bool selected = instance.name() == pendulum->nodeMeshName;
            if (ImGui::Selectable(instance.name().c_str(), selected))
                m_pendulumManager.setRenderMeshes(selectedIndex, instance.name(), pendulum->barMeshName);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const std::string barLabel = pendulum->barMeshName.empty() ? std::string("<none>") : pendulum->barMeshName;
    if (ImGui::BeginCombo("Bar Mesh", barLabel.c_str())) {
        for (MeshInstance& instance : m_meshManager.instances()) {
            const bool selected = instance.name() == pendulum->barMeshName;
            if (ImGui::Selectable(instance.name().c_str(), selected))
                m_pendulumManager.setRenderMeshes(selectedIndex, pendulum->nodeMeshName, instance.name());
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void Application::drawSelectionPanel()
{
    ImGui::TextUnformatted("Selection Tools");
    ImGui::Separator();

    bool showCrosshair = m_showCrosshair;
    if (ImGui::Checkbox("Show Crosshair (C)", &showCrosshair))
        m_showCrosshair = showCrosshair;

    ImGui::Text("Mode: %s", crosshairEnabled() ? "Active" : "Inactive");
    ImGui::Text("State: %s", m_isDraggingSelection ? "Dragging" : "Idle");

    if (ImGui::SliderFloat("Max Pick Distance", &m_maxPickDistance, 1.0f, 250.0f, "%.1f"))
        m_maxPickDistance = std::clamp(m_maxPickDistance, 1.0f, 500.0f);

    auto describeType = [](SelectionManager::Type type) {
        switch (type) {
        case SelectionManager::Type::MeshInstance: return "Mesh";
        case SelectionManager::Type::PendulumNode: return "Pendulum Node";
        case SelectionManager::Type::Light: return "Light";
        }
        return "Unknown";
    };

    ImGui::Separator();
    ImGui::TextUnformatted("Hovered");
    if (m_hoveredSelectable) {
        const auto& hover = *m_hoveredSelectable;
        ImGui::Text("Type: %s", describeType(hover.id.type));
        ImGui::Text("Name: %s", hover.name.c_str());
        ImGui::Text("Distance: %.2f", hover.distance);
        ImGui::Text("Hit: (%.2f, %.2f, %.2f)", hover.hitPoint.x, hover.hitPoint.y, hover.hitPoint.z);
    } else {
        ImGui::TextUnformatted("Type: <none>");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Selection");
    const auto selection = m_selectionManager.selection();
    if (selection) {
        ImGui::Text("Type: %s", describeType(selection->id.type));
        ImGui::Text("Name: %s", selection->name.c_str());
        ImGui::Text("Distance: %.2f", selection->distance);
        ImGui::Text("Hit: (%.2f, %.2f, %.2f)", selection->hitPoint.x, selection->hitPoint.y, selection->hitPoint.z);

        switch (selection->id.type) {
        case SelectionManager::Type::MeshInstance:
            ImGui::Text("Instance Index: %zu", selection->id.primary);
            break;
        case SelectionManager::Type::PendulumNode:
            ImGui::Text("Pendulum: %zu", selection->id.primary);
            ImGui::Text("Node: %zu", selection->id.secondary);
            break;
        case SelectionManager::Type::Light:
            ImGui::Text("Light Index: %zu", selection->id.primary);
            break;
        }

        if (ImGui::Button("Clear Selection")) {
            m_selectionManager.clearSelection();
            m_isDraggingSelection = false;
            endPendulumDrag();
            syncMeshSelectionWithCurrentSelection();
        }
    } else {
        ImGui::TextUnformatted("Type: <none>");
    }

    if (m_pendulumDragState) {
        ImGui::Separator();
        ImGui::Text("Pendulum Dragging: %zu", m_pendulumDragState->pendulumIndex);
    }
}

void Application::drawParticlesPanel()
{
    ImGui::TextUnformatted("Particle System");
    
    // Global particle texture selector (affects fireworks, magic aura, etc.)
    if (ImGui::CollapsingHeader("Particle Texture (Fireworks/Magic)", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool useTexture = m_particles.isUsingParticleTexture();
        if (ImGui::Checkbox("Use Custom Texture##ParticleTexture", &useTexture)) {
            m_particles.setUseParticleTexture(useTexture);
        }
        
        std::string currentTexture = m_particles.getParticleTextureName();
        if (currentTexture.empty()) {
            currentTexture = "<none - using default>";
        }
        
        if (ImGui::BeginCombo("Effect Texture", currentTexture.c_str())) {
            // Add "None" option to disable texture
            bool isNone = m_particles.getParticleTextureName().empty();
            if (ImGui::Selectable("<none - using default>", isNone)) {
                m_particles.loadParticleTexture("");
                m_particles.setUseParticleTexture(false);
            }
            if (isNone) {
                ImGui::SetItemDefaultFocus();
            }
            
            ImGui::Separator();
            
            auto availableTextures = m_particles.getAvailableParticleTextures();
            for (const auto& texName : availableTextures) {
                bool isSelected = (texName == m_particles.getParticleTextureName());
                if (ImGui::Selectable(texName.c_str(), isSelected)) {
                    m_particles.loadParticleTexture(texName);
                    m_particles.setUseParticleTexture(true);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::TextWrapped("This texture will be used for fireworks, magic aura, and other effects when enabled. Disable to use default point sprites.");
    }
    
    ImGui::Separator();
    
    // Snow system controls
    if (ImGui::CollapsingHeader("Snow System", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool snowEnabled = m_particles.isSnowEnabled();
        if (ImGui::Checkbox("Enable Snow", &snowEnabled)) {
            m_particles.enableSnow(snowEnabled);
        }
        
        // Snow sprite/texture selection
        ImGui::Separator();
        ImGui::TextUnformatted("Snow Sprite");
        
        std::string currentTexture = m_particles.getSnowTextureName();
        if (currentTexture.empty()) {
            currentTexture = "<none>";
        }
        
        if (ImGui::BeginCombo("Snow Texture", currentTexture.c_str())) {
            auto availableTextures = m_particles.getAvailableParticleTextures();
            for (const auto& texName : availableTextures) {
                bool isSelected = (texName == currentTexture);
                if (ImGui::Selectable(texName.c_str(), isSelected)) {
                    m_particles.loadSnowTexture(texName);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        ImGui::Separator();
        
        if (snowEnabled) {
            float intensity = m_particles.getSnowIntensity();
            if (ImGui::SliderFloat("Snow Intensity", &intensity, 10.0f, 10000.0f, "%.0f particles/sec")) {
                m_particles.setSnowIntensity(intensity);
            }
            
            float area = m_particles.getSnowArea();
            if (ImGui::SliderFloat("Snow Area", &area, 10.0f, 100.0f, "%.1f units")) {
                m_particles.setSnowArea(area);
            }
            
            float dropSize = m_particles.getSnowFlakeSize();
            if (ImGui::SliderFloat("Flake Size", &dropSize, 1.0f, 100.0f, "%.1f")) {
                m_particles.setSnowFlakeSize(dropSize);
            }
            
            float speed = m_particles.getSnowSpeed();
            if (ImGui::SliderFloat("Fall Speed", &speed, 0.5f, 100.0f, "%.1f")) {
                m_particles.setSnowSpeed(speed);
            }
            
            ImGui::TextWrapped("Snow follows the camera and falls around you in a circular area.");
        }
    }
    
    ImGui::Separator();
    ImGui::TextUnformatted("Fireworks");

    // We always spawn at the player's eye position and shoot in the camera forward direction.
    const glm::vec3 baseOrigin = m_player.eyePosition();
    glm::vec3 launchDir(0.0f, 1.0f, 0.0f);
    // Prefer the FPS camera forward vector when available
    if (m_cameraStage.getMode() == CameraStage::Mode::FirstPerson) {
        launchDir = m_cameraStage.getFpsCamera().getForward();
    } else {
        // Fallback: compute forward from view matrix (negative Z)
        const glm::mat4 view = m_cameraStage.getViewMatrix();
        launchDir = glm::normalize(glm::vec3(-view[0][2], -view[1][2], -view[2][2]));
    }

    ImGui::Text("Spawn at player eye: (%.2f, %.2f, %.2f)", baseOrigin.x, baseOrigin.y, baseOrigin.z);
    ImGui::Text("Launch direction: (%.2f, %.2f, %.2f)", launchDir.x, launchDir.y, launchDir.z);

    // ---- Firework params ----
    ImGui::TextUnformatted("Firework Rocket");
    ImGui::SliderFloat("Speed", &m_fireworkParams.speed, 0.1f, 50.0f, "%.2f");
    ImGui::SliderFloat("Fuse (s)", &m_fireworkParams.fuse, 0.1f, 5.0f, "%.2f");
    ImGui::SliderInt("Explosion Particles", &m_fireworkParams.burstCount, 1, 8000);
    ImGui::SliderFloat("Min Size", &m_fireworkParams.minSize, 1.0f, 50.0f, "%.1f");
    ImGui::SliderFloat("Max Size", &m_fireworkParams.maxSize, m_fireworkParams.minSize, 120.0f, "%.1f");
    ImGui::ColorEdit3("Base Color", glm::value_ptr(m_fireworkParams.baseColor));
    ImGui::SliderFloat("Color Spread", &m_fireworkParams.colorSpread, 0.0f, 1.0f, "%.2f");

    if (ImGui::Button("Launch Firework")) {
        m_particles.spawnFirework(baseOrigin, launchDir, m_fireworkParams);
    }

    ImGui::Separator();
    ImGui::TextWrapped("Tip: point the camera where you want the firework to go, then press Launch Firework.");

    // ---- Magic effects ----
    ImGui::Separator();
    ImGui::TextUnformatted("Magic Effects");
    ImGui::TextWrapped("Cast a magical aura around the player that keeps them levitating while it lasts.");

    static int auraCount = 800;
    static float auraDuration = 10.0f;
    static int auraRings = 3;
    static float levitationAccel = 1.2f; // upward acceleration (m/s^2)

    ImGui::SliderInt("Aura Particles", &auraCount, 100, 2000);
    ImGui::SliderFloat("Aura Duration (s)", &auraDuration, 2.0f, 30.0f, "%.1f");
    ImGui::SliderInt("Aura Rings", &auraRings, 1, 6);
    ImGui::SliderFloat("Levitation Accel", &levitationAccel, 0.0f, 6.0f, "%.2f");

    ImGui::TextWrapped("Dense ring shape: uses multiple concentric rings to eliminate gaps.");
    // shape selector
    const char* shapeNames[] = { "Ring", "Helix", "Torus", "Spiral" };
    static int shapeIndex = 0;
    if (ImGui::Combo("Aura Shape", &shapeIndex, shapeNames, IM_ARRAYSIZE(shapeNames))) {}

    if (ImGui::Button("Cast Magic Aura")) {
        // spawn center slightly below eye so particles appear around torso/eye level
        const glm::vec3 spawnCenter = baseOrigin - glm::vec3(0.0f, 0.15f, 0.0f);
        ParticleSystem::MagicAuraShape shape = static_cast<ParticleSystem::MagicAuraShape>(shapeIndex);
        // spawn dense multi-ring aura centered at spawnCenter
        m_particles.spawnMagicAura(spawnCenter, auraCount, auraDuration, auraRings, shape, levitationAccel);
        // start sustained levitation for player for the same duration
        m_player.startLevitation(auraDuration, levitationAccel);
    }


}


Application::~Application()
{
    m_cameraEffectsStage.shutdown();
    if (m_lightCubeEBO) glDeleteBuffers(1, &m_lightCubeEBO);
    if (m_lightCubeVBO) glDeleteBuffers(1, &m_lightCubeVBO);
    if (m_lightCubeVAO) glDeleteVertexArrays(1, &m_lightCubeVAO);
    m_pathRenderer.shutdown();
    m_cameraPathRenderer.shutdown();
}

void Application::update()
{
    auto lastFrameTime = std::chrono::steady_clock::now();

    while (!m_window.shouldClose()) {
        const auto now = std::chrono::steady_clock::now();
        const float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;
        m_simulationTime += deltaTime;

    beginFrameStats(deltaTime);

        m_window.updateInput();
        m_cameraStage.update(deltaTime);

        m_cameraPathPlayer.update(deltaTime);
        auto cameraPathSample = m_cameraPathPlayer.currentSample();
        const bool followCameraPath = (m_cameraPathPlayer.playing() || m_cameraPathFollowCamera) && cameraPathSample.has_value();
        if (followCameraPath) {
            if (m_cameraStage.getMode() != CameraStage::Mode::FreeCam)
                m_cameraStage.setMode(CameraStage::Mode::FreeCam);
            m_cameraPathPlayer.applyToCamera(m_cameraStage.getFpsCamera());
        }
        m_cameraPathLastSample = cameraPathSample;

        // This is your game loop
        // Put your real-time logic and rendering in here

        // UI
        m_debugUi.draw();

        ImGuiIO& imguiIo = ImGui::GetIO();
        const bool togglePressed = m_window.isKeyPressed(GLFW_KEY_C);
        if (togglePressed && !m_crosshairToggleHeld && !imguiIo.WantCaptureKeyboard) {
            m_showCrosshair = !m_showCrosshair;
            m_crosshairToggleHeld = true;
        } else if (!togglePressed) {
            m_crosshairToggleHeld = false;
        }

    // Recompute projection in case the window was resized.
    const float targetFov = followCameraPath ? glm::clamp(cameraPathSample->fov, 10.0f, 150.0f) : m_defaultCameraFov;
    m_activeCameraFov = targetFov;
    m_projectionMatrix = glm::perspective(glm::radians(m_activeCameraFov), m_window.getAspectRatio(), 0.1f, 100.0f);
        const glm::mat4 viewMatrix = m_cameraStage.getViewMatrix();
        const glm::vec3 cameraPosition = m_cameraStage.getPosition();

        // Player movement & physics
        if (m_cameraStage.getMode() == CameraStage::Mode::FirstPerson) {
            glm::vec3 moveInput = m_cameraStage.consumeMoveInput();
            if (glm::length(moveInput) > 0.0f) {
                const FPSCamera& cam = m_cameraStage.getFpsCamera();
                glm::vec3 forward = cam.getForward();
                glm::vec3 right = cam.getRight();
                m_player.applyMoveInput(forward, right, moveInput, cam.getMovementSpeed(), deltaTime);
            }
        }
        if (m_showGround)
            m_floor.update(m_player.position());

        bool jumpReq = m_cameraStage.consumeJumpRequested();
        const ProceduralFloor* activeFloor = m_showGround ? &m_floor : nullptr;
        m_player.update(deltaTime, activeFloor, jumpReq);

        if (m_cameraStage.getMode() == CameraStage::Mode::FirstPerson) {
            glm::vec3 camPos = m_player.eyePosition();
            m_cameraStage.getFpsCamera().setPosition(camPos);
        }

        // Particles update
        m_particles.update(deltaTime); // <<< ADDED
        m_particles.updateSnow(deltaTime, cameraPosition); // <<< ADDED for snow system

        if (m_runtimeLoadAutoTest && !m_runtimeLoadTriggered && m_simulationTime > 0.5f) {
            const std::filesystem::path autoLoadPath = std::filesystem::path(RESOURCE_ROOT "resources/dragon.obj");
            loadSceneFromPath(autoLoadPath);
            m_runtimeLoadTriggered = true;
        }

    m_environmentManager.sanitizeGeneratedTextures();

    ShadingStage::EnvironmentState environmentState;
        environmentState.irradianceMap = m_environmentManager.irradianceCubemap();
        environmentState.prefilterMap = m_environmentManager.prefilterCubemap();
        environmentState.brdfLut = m_environmentManager.brdfLutTexture();
        environmentState.intensity = m_environmentManager.environmentIntensity();
        environmentState.prefilterMipLevels = static_cast<float>(m_environmentManager.prefilterMipLevelCount());
        environmentState.useIBL = m_environmentManager.useIBL() && m_environmentManager.hasEnvironment();
        m_shadingStage.setEnvironmentState(environmentState);

        m_sunPathController.update(static_cast<double>(deltaTime));
        m_pendulumManager.update(static_cast<double>(deltaTime));

        gatherSelectables();

        const bool crosshairActive = crosshairEnabled();
        const bool allowPointerInput = !imguiIo.WantCaptureMouse;

        if (!crosshairActive) {
            if (m_isDraggingSelection) {
                m_selectionManager.endDrag();
                endPendulumDrag();
                m_isDraggingSelection = false;
                m_activeDragButton = ActiveDragButton::None;
            }
            m_hoveredSelectable.reset();
            m_leftMouseHeld = false;
            m_rightMouseHeld = false;
        } else {
            const glm::mat4 viewProjection = m_projectionMatrix * viewMatrix;
            const glm::mat4 invViewProjection = glm::inverse(viewProjection);

            glm::vec4 nearPoint = invViewProjection * glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
            glm::vec4 farPoint = invViewProjection * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            if (nearPoint.w != 0.0f)
                nearPoint /= nearPoint.w;
            if (farPoint.w != 0.0f)
                farPoint /= farPoint.w;

            Ray pickRay;
            pickRay.origin = cameraPosition;
            pickRay.direction = glm::normalize(glm::vec3(farPoint - nearPoint));

            if (!allowPointerInput && m_isDraggingSelection) {
                m_selectionManager.endDrag();
                endPendulumDrag();
                m_isDraggingSelection = false;
                m_activeDragButton = ActiveDragButton::None;
            }

            if (allowPointerInput) {
                m_hoveredSelectable = m_selectionManager.pick(pickRay, m_maxPickDistance);
            } else {
                m_hoveredSelectable.reset();
            }

            const bool leftPressed = m_window.isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
            const bool rightPressed = m_window.isMouseButtonPressed(GLFW_MOUSE_BUTTON_RIGHT);
            const bool leftPressedNow = leftPressed && !m_leftMouseHeld;
            const bool rightPressedNow = rightPressed && !m_rightMouseHeld;
            const bool leftReleased = !leftPressed && m_leftMouseHeld;
            const bool rightReleased = !rightPressed && m_rightMouseHeld;

            if ((leftPressedNow || rightPressedNow) && allowPointerInput) {
                if (m_hoveredSelectable) {
                    m_selectionManager.setSelection(*m_hoveredSelectable);
                    syncMeshSelectionWithCurrentSelection();
                } else {
                    m_selectionManager.clearSelection();
                    syncMeshSelectionWithCurrentSelection();
                }
            }

            const auto& selection = m_selectionManager.selection();
            if (selection && allowPointerInput && m_hoveredSelectable && m_hoveredSelectable->id == selection->id && !m_isDraggingSelection) {
                if (leftPressedNow) {
                    if (m_selectionManager.beginDrag(pickRay, SelectionManager::DragMode::Ground)) {
                        m_isDraggingSelection = true;
                        m_activeDragButton = ActiveDragButton::Left;
                        if (selection->id.type == SelectionManager::Type::PendulumNode)
                            beginPendulumDrag(selection->id.primary);
                    }
                } else if (rightPressedNow) {
                    if (m_selectionManager.beginDrag(pickRay, SelectionManager::DragMode::Vertical)) {
                        m_isDraggingSelection = true;
                        m_activeDragButton = ActiveDragButton::Right;
                        if (selection->id.type == SelectionManager::Type::PendulumNode)
                            beginPendulumDrag(selection->id.primary);
                    }
                }
            }

            if (m_isDraggingSelection && allowPointerInput) {
                if (auto delta = m_selectionManager.updateDrag(pickRay))
                    applySelectionDelta(*delta);
            }

            if ((leftReleased && m_activeDragButton == ActiveDragButton::Left)
                || (rightReleased && m_activeDragButton == ActiveDragButton::Right)) {
                const bool wasDragging = m_isDraggingSelection;
                if (wasDragging)
                    m_selectionManager.endDrag();
                if (wasDragging)
                    endPendulumDrag();
                m_isDraggingSelection = false;
                m_activeDragButton = ActiveDragButton::None;
            }

            m_leftMouseHeld = leftPressed;
            m_rightMouseHeld = rightPressed;
        }

    RenderStats renderStats {};
    renderStats.reset();

        renderShadowPasses(viewMatrix, m_projectionMatrix);

        m_lightManager.updateGpuData();
        const LightManager::GpuBinding& lightBindingSrc = m_lightManager.gpuBinding();
        ShadingStage::LightBufferBinding lightBinding {};
        lightBinding.lightSSBO = lightBindingSrc.lightSSBO;
        lightBinding.shadowMatricesUBO = lightBindingSrc.shadowMatricesUBO;
        lightBinding.directionalShadowTexture = lightBindingSrc.directionalShadowTexture;
        lightBinding.directionalShadowFallback = lightBindingSrc.directionalShadowFallback;
        lightBinding.pointShadowTextures = lightBindingSrc.pointShadowTextures;
        lightBinding.pointShadowFallback = lightBindingSrc.pointShadowFallback;
        lightBinding.pointShadowCount = lightBindingSrc.pointShadowCount;
        lightBinding.lightCount = lightBindingSrc.lightCount;
        lightBinding.directionalLightCount = lightBindingSrc.directionalLightCount;
        m_shadingStage.setLightBinding(lightBinding);

        LightingSettings& legacyLighting = m_shadingStage.settings();
        glm::vec3 fallbackPos = legacyLighting.lightPos;
        glm::vec3 fallbackColor = legacyLighting.lightColor;
        const auto& sceneLights = m_lightManager.lights();
        for (const LightManager::Light& light : sceneLights) {
            if (!light.enabled)
                continue;

            fallbackColor = glm::max(light.color * light.intensity, glm::vec3(0.0f));
            fallbackPos = light.position;
            break;
        }
        legacyLighting.lightColor = fallbackColor;
        legacyLighting.lightPos = fallbackPos;


        const glm::ivec2 framebufferSize = m_window.getFrameBufferSize();
        m_cameraEffectsStage.updateUniforms(m_cameraEffectsSettings, framebufferSize, deltaTime, 0.1f, 100.0f);
        m_cameraEffectsStage.beginSceneCapture(framebufferSize, m_cameraEffectsSettings);
        TRACE_APP_FBO("after beginSceneCapture");

        glEnable(GL_DEPTH_TEST);

        renderSkybox(viewMatrix, m_projectionMatrix, renderStats);
        TRACE_APP_FBO("after renderSkybox");
        renderPass(viewMatrix, m_projectionMatrix, cameraPosition, renderStats);
        TRACE_APP_FBO("after renderPass");

        // Transparent pass (particles)
        renderTransparentPass(viewMatrix, m_projectionMatrix, cameraPosition); // <<< ADDED
        // Skybox + debug primitives
        renderSkybox(viewMatrix, m_projectionMatrix, renderStats);
        renderDebugPrimitives(viewMatrix, m_projectionMatrix, renderStats);

        TRACE_APP_FBO("after renderDebugPrimitives");

    m_frameStats.render = renderStats;

        m_cameraEffectsStage.endSceneCapture();
        TRACE_APP_FBO("after endSceneCapture");
#ifndef NDEBUG
            m_sceneInspectCooldown -= deltaTime;
            if (m_sceneInspectCooldown <= 0.0f) {
                m_sceneInspectCooldown = 1.0f;
                debugInspectSceneFramebuffer(framebufferSize);
            }
#endif
        
        m_cameraEffectsStage.drawPostProcess(framebufferSize);
        TRACE_APP_FBO("after drawPostProcess");

        // Apply outline pass if enabled
        if (m_cameraEffectsSettings.outline.enabled) {
            m_cameraEffectsStage.drawOutlinePass(m_cameraEffectsSettings, framebufferSize,
                                                 m_cameraEffectsStage.sceneColorTexture(),
                                                 m_cameraEffectsStage.sceneDepthTexture(),
                                                 0, // target default framebuffer
                                                 0.1f, 100.0f); // near, far planes
        }
        TRACE_APP_FBO("after outline pass");

        // Render minimap to its texture (center on player XZ) only if enabled
        if (m_showMinimap) {
            const glm::vec3 playerPos = m_player.position();
            const glm::vec3 centerXZ(playerPos.x, 0.0f, playerPos.z);
            const float camH = m_minimapCamHeight;
            const float area = m_minimapAreaSize;

            // ensure we call this every frame with up-to-date center
            m_minimap.renderToTexture(centerXZ, camH, area,
                [this, playerPos, camH](const glm::mat4& view, const glm::mat4& proj){
                    // Use minimap camera position as 'cameraPos' for any culling / shader calculations.
                    const glm::vec3 minimapCameraPos(playerPos.x, camH, playerPos.z);

                    if (m_showGround) {
                        m_floor.draw(view, proj,
                                     m_shadingStage.settings().lightPos,
                                     m_shadingStage.settings().lightColor,
                                     m_shadingStage.settings().ambientColor,
                                     m_shadingStage.settings().ambientStrength,
                                     minimapCameraPos);
                    }

                    // Draw mesh instances on minimap
                    for (MeshInstance& instance : m_meshManager.instances()) {
                        const glm::mat4& instanceTransform = instance.transform();
                        for (MeshDrawItem& item : instance.drawItems()) {
                            const glm::mat4 model = instanceTransform * item.nodeTransform;
                            m_shadingStage.apply(model,
                                                 view,
                                                 proj,
                                                 minimapCameraPos,
                                                 item.material,
                                                 item.hasUVs,
                                                 item.hasSecondaryUVs,
                                                 item.hasTangents);
                            item.geometry.draw(m_shadingStage.shader());
                        }
                    }
                });
        }

        drawSelectionOverlay(viewMatrix, m_projectionMatrix);
        drawCrosshairOverlay();

        // draw ImGui minimap overlay
        if (m_showMinimap) {
            const float mapPosX = 10.0f;
            const float mapPosY = 10.0f;
            // smaller circle in top-left
            const float mapW = 140.0f;
            const float mapH = 140.0f;

            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const ImVec2 p0(mapPosX, mapPosY);
            const ImVec2 p1(mapPosX + mapW, mapPosY + mapH);

            // texture id from Minimap (GLuint -> ImTextureID)
            ImTextureID texId = reinterpret_cast<ImTextureID>(static_cast<intptr_t>(m_minimap.textureId()));

            // radius for rounding (half width -> circle)
            const float rounding = std::min(mapW, mapH) * 0.5f;

            // Use ImGui's AddImageRounded to clip the image to a circle exactly.
            // ImDrawFlags_RoundCornersAll ensures all corners are rounded.
            dl->AddImageRounded(texId, p0, p1, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f),
                                IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersAll);

            // circular border
            ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);
            dl->AddCircle(ImVec2(mapPosX + mapW * 0.5f, mapPosY + mapH * 0.5f), rounding - 2.0f, borderCol, 64, 2.0f);

            // Draw player direction arrow at the center of the minimap (smaller)
            glm::vec3 forward(0.0f, 0.0f, 1.0f);
            // Prefer a player-facing vector if available; fallback inference:
            if (m_cameraStage.getMode() == CameraStage::Mode::FirstPerson) {
                // Prefer explicit FPS camera forward when in FP mode
                forward = m_cameraStage.getFpsCamera().getForward();
            } else {
                glm::mat4 view = m_cameraStage.getViewMatrix();
                glm::mat4 invView = glm::inverse(view);
                forward = glm::normalize(glm::vec3(invView[2]));
                if (glm::length(forward) < 1e-6f)
                    forward = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            glm::vec2 f2(forward.x, forward.z);
            if (glm::length(f2) < 1e-6f) f2 = glm::vec2(0.0f, 1.0f);
            f2 = glm::normalize(f2);
            // atan2(x, z) gives correct rotation so arrow tip points in forward direction
            float angle = std::atan2(f2.x, f2.y);

            const float cx = mapPosX + mapW * 0.5f;
            const float cy = mapPosY + mapH * 0.5f;
            // reduced arrow size
            const float arrowR = std::min(mapW, mapH) * 0.10f; // smaller than before

            ImVec2 pA(0.0f, -arrowR);                      // tip
            ImVec2 pB(arrowR * 0.45f, arrowR * 0.7f);      // bottom right
            ImVec2 pC(-arrowR * 0.45f, arrowR * 0.7f);     // bottom left

            auto rot = [&](const ImVec2& v)->ImVec2 {
                const float s = std::sin(angle);
                const float c = std::cos(angle);
                return ImVec2(c * v.x - s * v.y + cx, s * v.x + c * v.y + cy);
            };

            ImVec2 rA = rot(pA);
            ImVec2 rB = rot(pB);
            ImVec2 rC = rot(pC);

            ImU32 arrowFill = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImU32 arrowBorder = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
            dl->AddTriangleFilled(rA, rB, rC, arrowFill);
            dl->AddTriangle(rA, rB, rC, arrowBorder, 1.0f);
        }

        // Swap
        finalizeFrameStats();

        // Processes input and swaps the window buffer
        m_window.swapBuffers();
    }
}

// ---------------- Light cube helpers ----------------

void Application::initLightCube()
{
    if (m_lightCubeVAO != 0) return;
    const float vertices[] = {
        // positions
        -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  -0.5f, 0.5f,-0.5f,
        -0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  -0.5f, 0.5f, 0.5f
    };
    const unsigned indices[] = {
        0,1,2, 2,3,0, // back
        4,5,6, 6,7,4, // front
        0,4,7, 7,3,0, // left
        1,5,6, 6,2,1, // right
        3,2,6, 6,7,3, // top
        0,1,5, 5,4,0  // bottom
    };
    glGenVertexArrays(1,&m_lightCubeVAO);
    glGenBuffers(1,&m_lightCubeVBO);
    glGenBuffers(1,&m_lightCubeEBO);
    glBindVertexArray(m_lightCubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_lightCubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_lightCubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),(void*)0);
    glBindVertexArray(0);
}

void Application::buildLightCubeShader()
{
    ShaderBuilder sb; sb.addStage(GL_VERTEX_SHADER, RESOURCE_ROOT "shaders/light_cube.vert");
    sb.addStage(GL_FRAGMENT_SHADER, RESOURCE_ROOT "shaders/light_cube.frag");
    m_lightCubeShader = sb.build();
}

void Application::renderLightCube(const glm::vec3& pos, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& color)
{
    if (m_lightCubeVAO == 0) return;
    m_lightCubeShader.bind();
    glDisable(GL_CULL_FACE);
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) * glm::scale(glm::mat4(1.0f), glm::vec3(0.1f));
    auto loc = m_lightCubeShader.getUniformLocation("model"); if (loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,glm::value_ptr(model));
    loc = m_lightCubeShader.getUniformLocation("view"); if (loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,glm::value_ptr(view));
    loc = m_lightCubeShader.getUniformLocation("projection"); if (loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,glm::value_ptr(proj));
    loc = m_lightCubeShader.getUniformLocation("color"); if (loc>=0) glUniform3fv(loc,1,glm::value_ptr(color));
    glBindVertexArray(m_lightCubeVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

// ---------------- Render passes ----------------

void Application::renderShadowPasses(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
{
    ProceduralFloor* floorPtr = m_showGround ? &m_floor : nullptr;
    m_lightManager.renderShadowMaps(viewMatrix,
        projectionMatrix,
        m_cameraStage.getPosition(),
        m_meshManager,
        floorPtr);
}

void Application::renderPass(const glm::mat4& viewMatrix,
                             const glm::mat4& projectionMatrix,
                             const glm::vec3& cameraPosition,
                             RenderStats& stats)
{
    // Standard depth test
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);

    // Enable SRGB for correct PBR lighting output
    glEnable(GL_FRAMEBUFFER_SRGB);

    const bool skyboxAlreadyDrew =
        m_environmentManager.skyboxVisible() &&
        m_environmentManager.hasEnvironment();

    if (skyboxAlreadyDrew) {
        // skybox filled the background → only clear depth
        glClear(GL_DEPTH_BUFFER_BIT);
    } else {
        // no skybox → we own the clear
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    m_shadingStage.beginFrame(viewMatrix, projectionMatrix, cameraPosition);

    // Render floor if enabled
    if (m_showGround) {
        m_floor.draw(viewMatrix, projectionMatrix,
                     m_shadingStage.settings().lightPos,
                     m_shadingStage.settings().lightColor,
                     m_shadingStage.settings().ambientColor,
                     m_shadingStage.settings().ambientStrength,
                     cameraPosition,
                     &stats);
    }

    // ===== TRANSPARENCY: Split opaque and transparent objects =====
    struct DrawCommand {
        MeshInstance* instance;
        MeshDrawItem* item;
        glm::mat4 model;
        float distanceToCamera;
    };

    std::vector<DrawCommand> opaqueList;
    std::vector<DrawCommand> transparentList;

    // Collect all draw commands and classify them
    for (MeshInstance& instance : m_meshManager.instances()) {
        const glm::mat4& instanceTransform = instance.transform();
        for (MeshDrawItem& item : instance.drawItems()) {
            const glm::mat4 model = instanceTransform * item.nodeTransform;
            const glm::vec3 worldPos = glm::vec3(model[3]);
            const float distSq = glm::length2(worldPos - cameraPosition);

            DrawCommand cmd;
            cmd.instance = &instance;
            cmd.item = &item;
            cmd.model = model;
            cmd.distanceToCamera = distSq;

            if (item.material.isTransparent) {
                transparentList.push_back(cmd);
            } else {
                opaqueList.push_back(cmd);
            }
        }
    }

    // ===== OPAQUE PASS: depth test ON, depth write ON, blending OFF =====
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    for (const auto& cmd : opaqueList) {
        m_shadingStage.apply(cmd.model,
                             viewMatrix,
                             projectionMatrix,
                             cameraPosition,
                             cmd.item->material,
                             cmd.item->hasUVs,
                             cmd.item->hasSecondaryUVs,
                             cmd.item->hasTangents);
        cmd.item->geometry.draw(m_shadingStage.shader());

        const std::uint64_t triangleCount = static_cast<std::uint64_t>(cmd.item->geometry.indexCount()) / 3;
        stats.addDraw(1, triangleCount);
    }

    renderPendulums(viewMatrix, projectionMatrix, cameraPosition, stats);

    // ===== TRANSPARENT PASS: depth test ON, depth write OFF, blending ON =====
    if (!transparentList.empty()) {
        // Sort transparent objects back-to-front
        std::sort(transparentList.begin(), transparentList.end(),
                  [](const DrawCommand& a, const DrawCommand& b) {
                      return a.distanceToCamera > b.distanceToCamera;
                  });

        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (const auto& cmd : transparentList) {
            m_shadingStage.apply(cmd.model,
                                 viewMatrix,
                                 projectionMatrix,
                                 cameraPosition,
                                 cmd.item->material,
                                 cmd.item->hasUVs,
                                 cmd.item->hasSecondaryUVs,
                                 cmd.item->hasTangents);
            cmd.item->geometry.draw(m_shadingStage.shader());

            const std::uint64_t triangleCount = static_cast<std::uint64_t>(cmd.item->geometry.indexCount()) / 3;
            stats.addDraw(1, triangleCount);
        }

        // Restore state
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    m_shadingStage.endFrame();

    glDisable(GL_FRAMEBUFFER_SRGB);
}

void Application::renderTransparentPass(const glm::mat4& viewMatrix,
                                        const glm::mat4& projectionMatrix,
                                        const glm::vec3& cameraPosition)
{
    // Transparent objects (particles, etc.)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Avoid writing depth so we properly blend with background
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    glDepthMask(GL_FALSE);

    // Draw water surface first so particle effects can appear above if desired
    m_water.draw(viewMatrix,
                 projectionMatrix,
                 cameraPosition,
                 m_shadingStage.settings().lightPos,
                 m_shadingStage.settings().lightColor,
                 m_shadingStage.settings().ambientColor,
                 m_shadingStage.settings().ambientStrength,
                 m_simulationTime);

    // Draw particle system (transparent)
    m_particles.draw(viewMatrix, projectionMatrix);

    // Restore state
    glDepthMask(prevDepthMask);
    glDisable(GL_BLEND);
}

void Application::renderPendulums(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, const glm::vec3& cameraPosition, RenderStats& stats)

{
    if (m_pendulumManager.pendulumCount() == 0)
        return;

    m_pendulumManager.forEachPendulum([&](const PendulumManager::PendulumData& pendulum, std::size_t index) {
        const auto packet = m_pendulumManager.renderPacket(index);
        if (!packet.nodeTransforms || packet.nodeTransforms->empty())
            return;

        MeshInstance* nodeInstance = m_meshManager.findInstanceByName(pendulum.nodeMeshName);
        if (!nodeInstance || nodeInstance->drawItems().empty())
            return;
        MeshDrawItem& nodeItem = nodeInstance->drawItems().front();

        MeshDrawItem* barItemPtr = nullptr;
        if (packet.barTransforms && !packet.barTransforms->empty()) {
            MeshInstance* barInstance = m_meshManager.findInstanceByName(pendulum.barMeshName);
            if (barInstance && !barInstance->drawItems().empty())
                barItemPtr = &barInstance->drawItems().front();
        }

        const std::uint64_t nodeTriangles = static_cast<std::uint64_t>(nodeItem.geometry.indexCount()) / 3;

        for (const glm::mat4& model : *packet.nodeTransforms) {
            m_shadingStage.apply(model,
                                 viewMatrix,
                                 projectionMatrix,
                                 cameraPosition,
                                 nodeItem.material,
                                 nodeItem.hasUVs,
                                 nodeItem.hasSecondaryUVs,
                                 nodeItem.hasTangents);
            nodeItem.geometry.draw(m_shadingStage.shader());
            stats.addDraw(1, nodeTriangles);
        }

        if (barItemPtr && packet.barTransforms) {
            const std::uint64_t barTriangles = static_cast<std::uint64_t>(barItemPtr->geometry.indexCount()) / 3;
            for (const glm::mat4& model : *packet.barTransforms) {
                m_shadingStage.apply(model,
                                     viewMatrix,
                                     projectionMatrix,
                                     cameraPosition,
                                     barItemPtr->material,
                                     barItemPtr->hasUVs,
                                     barItemPtr->hasSecondaryUVs,
                                     barItemPtr->hasTangents);
                barItemPtr->geometry.draw(m_shadingStage.shader());
                stats.addDraw(1, barTriangles);
            }
        }
    });
}


void Application::renderSkybox(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, RenderStats& stats)
{
    const bool shouldDraw = m_environmentManager.hasEnvironment() && m_environmentManager.skyboxVisible();
    if (!shouldDraw)
        return;
    m_environmentManager.drawSkybox(viewMatrix, projectionMatrix);
    stats.addDraw(1, 12);
}

void Application::renderDebugPrimitives(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, RenderStats& stats)
{
    glEnable(GL_FRAMEBUFFER_SRGB);
    for (const LightManager::Light& light : m_lightManager.lights()) {
        if (!light.enabled)
            continue;

        glm::vec3 color = light.color;
        switch (light.type) {
        case LightManager::LightType::Point:
        case LightManager::LightType::Spot:
            renderLightCube(light.position, viewMatrix, projectionMatrix, color);
            stats.addDraw(1, 12);
            break;
        }
    }

    const bool drawCurve = m_sunPathController.renderCurve();
    const bool drawControlPoints = m_sunPathController.showControlPoints();
    const bool drawTangents = m_sunPathController.showTangents();
    if (m_sunPathController.hasPath() && (drawCurve || drawControlPoints || drawTangents)) {
        m_pathRenderer.updateGeometry(m_sunPathController.path(), m_sunPathController.pathVersion());

        GLboolean depthMask;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glDepthMask(GL_FALSE);

        if (drawCurve) {
            m_pathRenderer.drawCurve(viewMatrix, projectionMatrix, glm::vec3(1.0f, 0.85f, 0.1f));
            stats.addDraw(1, 0);
        }

        if (drawTangents) {
            m_pathRenderer.drawTangents(viewMatrix, projectionMatrix, glm::vec3(0.2f, 0.8f, 1.0f));
            stats.addDraw(1, 0);
        }

        if (drawControlPoints) {
            glEnable(GL_PROGRAM_POINT_SIZE);
            m_pathRenderer.drawControlPoints(viewMatrix, projectionMatrix, glm::vec3(1.0f, 0.2f, 0.2f));
            glDisable(GL_PROGRAM_POINT_SIZE);
            stats.addDraw(1, 0);
        }

        glDepthMask(depthMask);
    }

    renderCameraPathDebug(viewMatrix, projectionMatrix, stats);
    glDisable(GL_FRAMEBUFFER_SRGB);
}

void Application::drawSelectionOverlay(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix)
{
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
        return;

    const auto selection = m_selectionManager.selection();

    std::vector<std::pair<const SelectionManager::HitResult*, ImU32>> overlays;
    overlays.reserve(2);

    const ImU32 hoverColor = IM_COL32(90, 200, 255, 200);
    const ImU32 selectColor = m_isDraggingSelection ? IM_COL32(255, 210, 60, 240) : IM_COL32(255, 230, 90, 220);

    if (m_hoveredSelectable)
        overlays.emplace_back(&*m_hoveredSelectable, hoverColor);

    if (selection) {
        const bool matchesHover = m_hoveredSelectable && m_hoveredSelectable->id == selection->id;
        if (matchesHover) {
            overlays.back().second = selectColor;
        } else {
            overlays.emplace_back(&*selection, selectColor);
        }
    }

    if (overlays.empty())
        return;

    auto* drawList = ImGui::GetForegroundDrawList();
    const glm::mat4 viewProj = projectionMatrix * viewMatrix;

    static constexpr std::array<std::pair<int, int>, 12> edges { {
        { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
        { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    } };

    const auto projectPoint = [&](const glm::vec3& world) -> std::optional<ImVec2> {
        const glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f)
            return std::nullopt;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < 0.0f || ndc.z > 1.0f)
            return std::nullopt;
        ImVec2 screen;
        screen.x = (ndc.x * 0.5f + 0.5f) * displaySize.x;
        screen.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * displaySize.y;
        return screen;
    };

    for (const auto& [hit, color] : overlays) {
        if (!hit)
            continue;

        glm::vec3 boundsMin;
        glm::vec3 boundsMax;
        if (hit->shape == SelectionManager::Shape::Sphere) {
            boundsMin = hit->center - glm::vec3(hit->radius);
            boundsMax = hit->center + glm::vec3(hit->radius);
        } else {
            boundsMin = hit->bounds.min;
            boundsMax = hit->bounds.max;
        }

        const std::array<glm::vec3, 8> corners { {
            { boundsMin.x, boundsMin.y, boundsMin.z },
            { boundsMax.x, boundsMin.y, boundsMin.z },
            { boundsMin.x, boundsMax.y, boundsMin.z },
            { boundsMax.x, boundsMax.y, boundsMin.z },
            { boundsMin.x, boundsMin.y, boundsMax.z },
            { boundsMax.x, boundsMin.y, boundsMax.z },
            { boundsMin.x, boundsMax.y, boundsMax.z },
            { boundsMax.x, boundsMax.y, boundsMax.z }
        } };

        std::array<ImVec2, 8> projected {};
        bool valid = true;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const auto screen = projectPoint(corners[i]);
            if (!screen.has_value()) {
                valid = false;
                break;
            }
            projected[i] = *screen;
        }

        if (!valid)
            continue;

        for (const auto& edge : edges)
            drawList->AddLine(projected[edge.first], projected[edge.second], color, 1.5f);

        if (!hit->name.empty()) {
            const ImVec2 labelPos = projected[0];
            drawList->AddText(labelPos, color, hit->name.c_str());
        }
    }
}

void Application::drawCrosshairOverlay() const
{
    if (!crosshairEnabled())
        return;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
        return;

    const ImVec2 centre(displaySize.x * 0.5f, displaySize.y * 0.5f);
    const float armLength = 10.0f;
    const float gap = 3.0f;
    const float thickness = 2.0f;

    ImU32 color = IM_COL32(255, 255, 255, 180);
    if (m_hoveredSelectable)
        color = IM_COL32(90, 200, 255, 220);
    if (m_selectionManager.selection())
        color = IM_COL32(255, 230, 90, 220);

    auto* drawList = ImGui::GetForegroundDrawList();
    drawList->AddLine(ImVec2(centre.x - armLength, centre.y), ImVec2(centre.x - gap, centre.y), color, thickness);
    drawList->AddLine(ImVec2(centre.x + gap, centre.y), ImVec2(centre.x + armLength, centre.y), color, thickness);
    drawList->AddLine(ImVec2(centre.x, centre.y - armLength), ImVec2(centre.x, centre.y - gap), color, thickness);
    drawList->AddLine(ImVec2(centre.x, centre.y + gap), ImVec2(centre.x, centre.y + armLength), color, thickness);
}

void Application::gatherSelectables()
{
    m_selectionManager.beginFrame();

    const auto& instances = m_meshManager.instances();
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const MeshInstance& instance = instances[i];

        SelectionManager::SelectableEntry entry;
        entry.id = { SelectionManager::Type::MeshInstance, i, 0 };
        entry.name = instance.name();
        entry.shape = SelectionManager::Shape::Aabb;
        entry.bounds = m_meshManager.computeWorldBounds(instance);
        entry.center = (entry.bounds.min + entry.bounds.max) * 0.5f;
        entry.radius = glm::length(entry.bounds.max - entry.center);
        m_selectionManager.addSelectable(entry);
    }

    const auto& lights = m_lightManager.lights();
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const auto& light = lights[i];
        if (!light.enabled)
            continue;

        SelectionManager::SelectableEntry entry;
        entry.id = { SelectionManager::Type::Light, i, 0 };
        entry.name = light.name.empty() ? "Light " + std::to_string(i) : light.name;
        entry.shape = SelectionManager::Shape::Sphere;
        entry.center = light.position;
        entry.radius = 0.15f;
        entry.bounds.min = entry.center - glm::vec3(entry.radius);
        entry.bounds.max = entry.center + glm::vec3(entry.radius);
        m_selectionManager.addSelectable(entry);
    }

    const float nodeRadius = m_pendulumManager.settings().nodeRadius;
    m_pendulumManager.forEachPendulum([&](const PendulumManager::PendulumData& pendulum, std::size_t pendulumIndex) {
        for (std::size_t nodeIndex = 0; nodeIndex < pendulum.nodes.size(); ++nodeIndex) {
            const auto& node = pendulum.nodes[nodeIndex];

            SelectionManager::SelectableEntry entry;
            entry.id = { SelectionManager::Type::PendulumNode, pendulumIndex, nodeIndex };
            if (pendulum.name.empty())
                entry.name = "Pendulum " + std::to_string(pendulumIndex) + " Node " + std::to_string(nodeIndex);
            else
                entry.name = pendulum.name + " Node " + std::to_string(nodeIndex);
            entry.shape = SelectionManager::Shape::Sphere;
            entry.center = node.position;
            entry.radius = nodeRadius;
            entry.bounds.min = entry.center - glm::vec3(nodeRadius);
            entry.bounds.max = entry.center + glm::vec3(nodeRadius);
            m_selectionManager.addSelectable(entry);
        }
    });
}

void Application::applySelectionDelta(const glm::vec3& delta)
{
    const auto& selection = m_selectionManager.selection();
    if (!selection)
        return;

    if (glm::dot(delta, delta) < 1e-12f)
        return;

    switch (selection->id.type) {
    case SelectionManager::Type::MeshInstance: {
        std::vector<MeshInstance>& instances = m_meshManager.instances();
        if (selection->id.primary >= instances.size())
            break;
        MeshInstance& instance = instances[selection->id.primary];
        glm::mat4 transform = instance.transform();
        transform = glm::translate(glm::mat4(1.0f), delta) * transform;
        instance.setTransform(transform);
        break;
    }
    case SelectionManager::Type::PendulumNode: {
        m_pendulumManager.translateNode(selection->id.primary, selection->id.secondary, delta);
        m_pendulumManager.refreshTransforms(selection->id.primary);
        break;
    }
    case SelectionManager::Type::Light: {
        auto& lights = m_lightManager.lights();
        if (selection->id.primary >= lights.size())
            break;
        lights[selection->id.primary].position += delta;
        m_lightManager.markDirty();
        break;
    }
    }
}

void Application::syncMeshSelectionWithCurrentSelection()
{
    const auto selection = m_selectionManager.selection();
    if (selection && selection->id.type == SelectionManager::Type::MeshInstance) {
        const std::size_t index = selection->id.primary;
        const MeshManager& meshManager = m_meshManager;
        const auto& instances = meshManager.instances();
        if (index < instances.size() && index <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            m_meshManager.setSelectedInstance(static_cast<int>(index));
            return;
        }
    }

    m_meshManager.setSelectedInstance(-1);
}

void Application::beginPendulumDrag(std::size_t pendulumIndex)
{
    if (m_pendulumDragState && m_pendulumDragState->pendulumIndex == pendulumIndex)
        return;

    const PendulumManager::PendulumData* pendulum = m_pendulumManager.getPendulum(pendulumIndex);
    if (!pendulum)
        return;

    PendulumDragState state;
    state.pendulumIndex = pendulumIndex;
    state.wasRunning = pendulum->running;
    state.wasPaused = pendulum->paused;
    m_pendulumDragState = state;

    if (!pendulum->paused)
        m_pendulumManager.pause(pendulumIndex);
}

void Application::endPendulumDrag()
{
    if (!m_pendulumDragState)
        return;

    const PendulumDragState state = *m_pendulumDragState;
    PendulumManager::PendulumData* pendulum = m_pendulumManager.getPendulum(state.pendulumIndex);
    if (pendulum) {
        pendulum->running = state.wasRunning;
        pendulum->paused = state.wasPaused;
    }

    m_pendulumDragState.reset();
}

bool Application::crosshairEnabled() const
{
    return m_showCrosshair;
}

void Application::debugInspectSceneFramebuffer(glm::ivec2 framebufferSize)
{
#ifndef NDEBUG
    if (framebufferSize.x <= 0 || framebufferSize.y <= 0)
        return;

    const GLuint sceneFbo = m_cameraEffectsStage.sceneFramebuffer();
    if (sceneFbo == 0)
        return;

    GLint prevReadFbo = 0;
    GLint prevReadBuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    const int sampleX = std::clamp(framebufferSize.x / 2, 0, std::max(framebufferSize.x - 1, 0));
    const int sampleY = std::clamp(framebufferSize.y / 2, 0, std::max(framebufferSize.y - 1, 0));

    float pixel[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glReadPixels(sampleX, sampleY, 1, 1, GL_RGBA, GL_FLOAT, pixel);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
    glReadBuffer(prevReadBuffer);

    const float luminance = pixel[0] + pixel[1] + pixel[2];
    std::fprintf(stderr,
        "[SceneFBO] center rgba=(%.3f %.3f %.3f %.3f) luminance=%.3f\n",
        pixel[0], pixel[1], pixel[2], pixel[3], luminance);
#else
    (void)framebufferSize;
#endif
}

// ---------------- Scene / environment helpers ----------------

void Application::loadSceneFromPath(const std::filesystem::path& path)
{
    if (path.empty()) {
        m_modelLoadMessage = "Please provide a glTF/.glb file path.";
        m_lastModelLoadSuccess = false;
        return;
    }

    const std::filesystem::path absolutePath = std::filesystem::absolute(path);
    if (!std::filesystem::exists(absolutePath)) {
        m_modelLoadMessage = "File not found: " + absolutePath.string();
        m_lastModelLoadSuccess = false;
        return;
    }

    if (!m_modelLoader.loadModel(absolutePath.string())) {
        const std::string& error = m_modelLoader.getLastError();
        m_modelLoadMessage = error.empty() ? "Assimp failed to load the model." : error;
        m_lastModelLoadSuccess = false;
        return;
    }

    if (!m_meshManager.addMeshFromData(absolutePath, m_modelLoader.getMeshes())) {
        m_modelLoadMessage = "Unable to create GPU meshes for the loaded scene.";
        m_lastModelLoadSuccess = false;
        return;
    }

    m_lastModelPath = absolutePath;
    setModelPathBuffer(absolutePath);
    m_modelLoadMessage = "Loaded " + absolutePath.filename().string();
    m_lastModelLoadSuccess = true;
}

void Application::setModelPathBuffer(const std::filesystem::path& path)
{
    const std::string pathString = path.string();
    std::snprintf(m_modelPathBuffer.data(), m_modelPathBuffer.size(), "%s", pathString.c_str());
}

void Application::loadEnvironmentFromPath(const std::filesystem::path& path)
{
    if (path.empty()) {
        m_environmentLoadMessage = "Please provide an .hdr environment file.";
        m_environmentLoadSuccess = false;
        return;
    }

    const std::filesystem::path absolutePath = std::filesystem::absolute(path);
    if (!std::filesystem::exists(absolutePath)) {
        m_environmentLoadMessage = "File not found: " + absolutePath.string();
        m_environmentLoadSuccess = false;
        return;
    }

    try {
        if (m_environmentManager.loadEnvironment(absolutePath)) {
            setEnvironmentPathBuffer(absolutePath);
            m_environmentLoadMessage = "Loaded environment " + absolutePath.filename().string();
            m_environmentLoadSuccess = true;
        } else {
            m_environmentLoadMessage = "Failed to load environment.";
            m_environmentLoadSuccess = false;
        }
    } catch (const std::exception& ex) {
        m_environmentLoadMessage = ex.what();
        m_environmentLoadSuccess = false;
    }
}

void Application::setEnvironmentPathBuffer(const std::filesystem::path& path)
{
    const std::string pathString = path.string();
    std::snprintf(m_environmentPathBuffer.data(), m_environmentPathBuffer.size(), "%s", pathString.c_str());
}

int main(int argc, char** argv)
{
    std::optional<std::filesystem::path> initialScene;
    if (argc > 1)
        initialScene = std::filesystem::path(argv[1]);

    Application app(initialScene);
    app.update();

    return 0;
}