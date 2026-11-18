#include "./image_loader.hpp"

#include "./OverlayManager.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "./stb_image.h"

#include <cstdint>
#include <string>
#include <vector>

bool load_image(const std::string &filename, overlay::Image &image)
{
    int width, height, channels;

    // Force 4 channels (RGBA).
    stbi_uc *data = stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!data)
        return false;

    image.width = width;
    image.height = height;
    image.stride = width * 4;
    image.pixels.resize(image.stride * height);

    for (int i = 0; i < width * height; ++i)
    {
        uint8_t r = data[i * 4 + 0];
        uint8_t g = data[i * 4 + 1];
        uint8_t b = data[i * 4 + 2];
        uint8_t a = data[i * 4 + 3];

        // Premultiply alpha and convert RGBA -> BGRA.
        image.pixels[i * 4 + 0] = (b * a + 127) / 255;
        image.pixels[i * 4 + 1] = (g * a + 127) / 255;
        image.pixels[i * 4 + 2] = (r * a + 127) / 255;
        image.pixels[i * 4 + 3] = a;
    }

    stbi_image_free(data);
    return true;
}
