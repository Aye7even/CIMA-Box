// What changed in each version, from CHANGELOG.md (built into the program by CMake).
#pragma once
#include <string>
#include <vector>

struct ChangeEntry {
    std::string version;  // "2.6.0"
    std::string date;     // optional, from "## 2.6.0 - 2026-11-18"
    std::vector<std::string> items;
};

// The built-in changelog, newest first (empty if the program was built without one).
const std::vector<ChangeEntry>& changelog();

// "## <version> [- date]" starts a version; "- " or "* " lines are its changes (indented lines continue
// the one before). Everything else is ignored.
std::vector<ChangeEntry> parseChangelog(const std::string& markdown);
