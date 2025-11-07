#pragma once
#include <glm/glm.hpp>
#include <functional>

class Minimap {
public:
    Minimap(int size = 256);
    ~Minimap();
    void renderToTexture(const glm::vec3& centerXZ, float cameraHeight, float areaSize,
                         const std::function<void(const glm::mat4& view, const glm::mat4& proj)>& drawCallback);
    void drawOverlay(float posX, float posY, float width, float height);
    unsigned int textureId() const;
private:
    void allocate(int size);
    void destroy();
    int m_size = 256;
    unsigned int m_fbo = 0;
    unsigned int m_colorTex = 0;
    unsigned int m_depthRbo = 0;
};
