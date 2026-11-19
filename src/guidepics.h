// The pictures for "how do I get this key?", built into the program (see tools/guide).
#pragma once
#include <cstdint>
#include <vector>

int guidePicCount();
// The picture as RGBA pixels. False if it couldn't be read.
bool decodeGuidePic(int index, std::vector<uint8_t>& rgba, int& width, int& height);
// A JPEG or PNG file's bytes as RGBA (big pictures are scaled down). False if it isn't one.
bool decodePicture(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& rgba, int& width, int& height);
