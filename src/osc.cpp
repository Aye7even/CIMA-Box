#include "osc.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

// OSC strings end with a zero and are padded with zeros to a multiple of four bytes.
void oscString(std::string& out, const std::string& s) {
    out += s;
    out.append(4 - s.size() % 4, '\0');
}

}  // namespace

std::string clipUtf8(const std::string& s, size_t maxChars) {
    size_t chars = 0, i = 0;
    while (i < s.size()) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && chars++ == maxChars) break;  // start of a character
        ++i;
    }
    return s.substr(0, i);
}

std::string oscChatboxPacket(const std::string& text) {
    std::string p;
    oscString(p, "/chatbox/input");
    oscString(p, ",sTF");  // string, true (send now, don't open the keyboard), false (no sound)
    oscString(p, text);
    return p;
}

OscOut::OscOut() {
#ifdef _WIN32
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
#endif
    sock_ = intptr_t(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
}

OscOut::~OscOut() {
#ifdef _WIN32
    if (sock_ != intptr_t(INVALID_SOCKET)) closesocket(SOCKET(sock_));
    WSACleanup();
#else
    if (sock_ >= 0) close(int(sock_));
#endif
}

bool OscOut::chatbox(const std::string& text, int port) {
#ifdef _WIN32
    if (sock_ == intptr_t(INVALID_SOCKET)) return false;
#else
    if (sock_ < 0) return false;
#endif
    const std::string packet = oscChatboxPacket(clipUtf8(text, kChatboxChars));
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(static_cast<unsigned short>(port));
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifdef _WIN32
    return sendto(SOCKET(sock_), packet.data(), int(packet.size()), 0, reinterpret_cast<const sockaddr*>(&to), int(sizeof(to))) >= 0;
#else
    return sendto(int(sock_), packet.data(), packet.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) >= 0;
#endif
}
