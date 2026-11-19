#include "guidepics.h"

#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "third_party/stb_image.h"

#include "guide_data.h"

int guidePicCount() { return int(sizeof(kGuidePics) / sizeof(kGuidePics[0])); }

bool decodeGuidePic(int index, std::vector<uint8_t>& rgba, int& width, int& height) {
    if (index < 0 || index >= guidePicCount()) return false;
    int channels = 0;
    unsigned char* px = stbi_load_from_memory(kGuidePics[index].jpg, int(kGuidePics[index].size), &width, &height, &channels, 4);
    if (!px) return false;
    rgba.assign(px, px + size_t(width) * size_t(height) * 4);
    stbi_image_free(px);
    return true;
}

bool decodePicture(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& rgba, int& width, int& height) {
    int channels = 0;
    if (bytes.empty() || bytes.size() > size_t(64) << 20) return false;
    unsigned char* px = stbi_load_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels, 4);
    if (!px) return false;
    // Big photos: every other pixel until it's at most 2560 wide, plenty for a window background.
    int step = 1;
    while (width / step > 2560 || height / step > 2560) step *= 2;
    const int w = width / step, h = height / step;
    rgba.resize(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            std::memcpy(&rgba[(size_t(y) * size_t(w) + size_t(x)) * 4],
                        px + ((size_t(y) * size_t(step)) * size_t(width) + size_t(x) * size_t(step)) * 4, 4);
    stbi_image_free(px);
    width = w;
    height = h;
    return true;
}

