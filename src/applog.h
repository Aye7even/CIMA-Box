// cimabox.log: what happened, for when something goes wrong on someone else's PC. Errors and a few
// key events, one line each with the time. API keys never get in: every registered key is blanked,
// and so is anything that looks like one. The file is trimmed when it gets big.
#pragma once
#include <filesystem>
#include <string>

void logOpen(const std::filesystem::path& path);        // appends from now on (trims an oversized file first)
void logLine(const std::string& text);        // safe from any thread
void logSecret(const std::string& secret);    // never write this text (an API key)
std::filesystem::path logPath();
std::string logRedact(const std::string& text);  // what logLine writes, for testing
// After the first logOpen: false if the last run's log didn't end with "Closed." (a crash, or
// the program was ended by force).
bool logLastRunClosed();
