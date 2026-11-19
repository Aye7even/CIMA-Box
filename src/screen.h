// Screen translation: a picture of one program's window, and the text in it.
//
// The picture is taken the way screenshot and streaming tools do it (PrintWindow, or the screen
// itself where the window is), never by reading the program's memory. The text is read by Windows'
// own text recognizer (Windows.Media.Ocr), on this PC; AI reading (translate.h, readPicture) is the
// other choice.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Picture {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // top row first
};

// The window of a program (by its file name, e.g. "VRChat.exe"). Not when it's minimized.
bool captureWindow(const std::string& exe, Picture& out, std::string& err);
// A part of a picture: area = left, top, right, bottom as 0..1 of its size.
Picture cropPicture(const Picture& p, const float area[4]);
// No wider or taller than `maxSide` pixels (keeps the shape).
Picture scaledDown(const Picture& p, int maxSide);
// The picture as a PNG file (for AI reading), and as base64 text.
std::string encodePng(const Picture& p);
std::string base64(const std::string& bytes);

// Windows' text reader. `lang`: the language in the picture ("ja", "en"...), "" = the ones in
// Windows' settings. Call from a thread of its own (it sets up the Windows runtime there).
bool readText(const Picture& p, const std::string& lang, std::vector<std::string>& lines, std::string& err);
// The languages Windows can read on this PC ("en-US", "ja"...); empty when there's no text reader.
std::vector<std::string> readerLanguages();

// About how many tokens a picture costs an AI service (TranslateService), for the preview.
int pictureTokens(int service, int width, int height);

// Two lines of text that are the same apart from small reading differences (a letter or two).
bool sameLine(const std::string& a, const std::string& b);
