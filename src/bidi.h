// Right-to-left text (Arabic, Persian, Urdu, Hebrew) for text drawing that only goes left to right.
//
// Two steps, the same ones a full text engine does:
//  - shaping: Arabic letters change shape with their neighbours (isolated, start, middle, end of a
//    word, and the lam-alef ligature). The joined forms have their own code points (U+FB50-FEFF),
//    which the system fonts have, so the right shape can be picked here.
//  - reordering: right-to-left words are drawn from right to left, while numbers and English words
//    inside them stay left to right (a simplified Unicode bidi algorithm, without explicit
//    embedding marks).
// ImGui's text drawing calls visualLine() for each line it draws (see cmake/imgui_rtl.inl), and the
// VR caption panel does the same.
#pragma once
#include <string>

namespace bidi {

// True when the text has right-to-left letters (fast; most text has none).
bool hasRtl(const char* begin, const char* end);
inline bool hasRtl(const std::string& s) { return hasRtl(s.data(), s.data() + s.size()); }

// Arabic letters replaced by their joined forms, still in reading order. Text without Arabic comes
// back unchanged. Vowel marks are left out (they can't be placed well without a full text engine).
std::string shape(const std::string& logical);

// One line (no line breaks) as it should be drawn from left to right: shaped and reordered.
// `rtl`: the line reads right to left (so it lines up on the right).
std::string visualLine(const char* begin, const char* end, bool* rtl = nullptr);
inline std::string visualLine(const std::string& s, bool* rtl = nullptr) { return visualLine(s.data(), s.data() + s.size(), rtl); }

// When the app's own language reads right to left: any line with right-to-left words reads right to
// left (otherwise it goes by which kind of words there are more of).
void setPreferRtl(bool prefer);

// Hooks ImGui's text drawing (call once, after ImGui is created).
void installImGuiHook();

}  // namespace bidi
