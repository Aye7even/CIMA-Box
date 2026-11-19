#include "guidepics.h"

#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
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
