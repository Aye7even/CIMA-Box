// Sends text to VRChat's chatbox over OSC (UDP on this computer).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// The raw "/chatbox/input" message: text, show immediately, no notification sound.
std::string oscChatboxPacket(const std::string& text);

// At most `maxChars` characters, never cutting a UTF-8 character in half.
std::string clipUtf8(const std::string& s, size_t maxChars);

// The chatbox text for "both": the line people should read on top, the other one underneath in
// brackets, like a subtitle with its original:
//     こんにちは、元気？
//     (Hey, how are you?)
// VRChat shows 144 characters. When both don't fit, the line underneath is shortened ("...") or
// left out; the top line is only shortened when it alone is too long.
std::string chatboxPair(const std::string& top, const std::string& under, size_t maxChars = 144);

class OscOut {
public:
    static constexpr int kVrchatPort = 9000;    // VRChat's default OSC input port
    static constexpr size_t kChatboxChars = 144;  // VRChat's chatbox limit

    OscOut();
    ~OscOut();
    OscOut(const OscOut&) = delete;
    OscOut& operator=(const OscOut&) = delete;

    bool chatbox(const std::string& text, int port = kVrchatPort);

private:
    intptr_t sock_ = -1;
};
