#pragma once
// Suppress warnings in third-party code.
#include <framework/disable_all_warnings.h>
DISABLE_WARNINGS_PUSH()
#include <glm/glm.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
DISABLE_WARNINGS_POP()
#include <filesystem>
#include <vector>


struct Image {
public:
    explicit Image(const std::filesystem::path& filePath, int forceChannels = 0);


    void writeBitmapToFile(const std::filesystem::path& filePath);

public:
    int width, height, channels;
    template<int image_channels = 3> glm::vec<image_channels, float>get_pixel(const int index) const {
        //Template argument should equal actual image channels
        assert(image_channels == channels);
        
        glm::vec<image_channels, float> pixel;
        for (int channel = 0; channel < image_channels; channel++) {
            pixel[channel] = pixels[index * image_channels + channel] / 255.0f;
        }

        return pixel;
    }

    template<int image_channels = 3> void set_pixel(const int index, glm::vec<image_channels, float> value) {
        //Template argument should equal actual image channels
        assert(image_channels == channels);
        
        for (int channel = 0; channel < image_channels; channel++) {
            pixels[index * image_channels + channel] = (uint8_t) (value[channel] * 255.0f);
        }
    }

    uint8_t* get_data() {
        return pixels.data();
    }

private:
    std::vector<uint8_t> pixels;
};
