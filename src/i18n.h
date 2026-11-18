// The app's own language. Every text in the interface is written in English in the code and looked
// up here; a missing translation simply shows the English. Built-in languages live in i18n.cpp;
// more can be added without rebuilding by putting a file in the "lang" folder next to the program
// (see the Read Me).
#pragma once
#include <string>
#include <vector>

struct UiLanguage {
    std::string code;  // "es", "pt-BR", ...
    std::string name;  // in its own language: "Español"
};

// English first, then the built-in ones, then any found in the "lang" folder.
const std::vector<UiLanguage>& uiLanguages();
void setUiLanguage(const std::string& code);  // unknown codes fall back to English
const std::string& uiLanguage();

// The text in the current language. "Label##id" keeps its "##id" (ImGui ids stay the same).
// The result stays valid until the language changes.
const char* T(const char* english);
inline const char* T(const std::string& english) { return T(english.c_str()); }

// For printf formats: the translation is only used if it has the same % conversions, in the same
// order, as the English (a broken translation must never crash the program).
const char* TF(const char* englishFormat);

// Every English text with its built-in translations, for checking completeness (tests).
size_t builtInStringCount();
size_t builtInMissing(const std::string& code);
