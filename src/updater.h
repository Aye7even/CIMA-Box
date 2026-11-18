// Updates from GitHub Releases. The release that a version tag (v2.5.0) publishes holds
// CIMABox.exe and CIMABox.exe.sig, its ECDSA P-256 / SHA-256 signature made with your private key.
// The app checks the latest release, and on "Update and restart" downloads both, refuses the file
// unless the signature matches the public key built into the program, swaps the exe and restarts.
// Settings (cimabox.json) are untouched. Windows only; built with CIMA_UPDATE_REPO and update_key.txt.
#pragma once
#include <atomic>
#include <memory>
#include <string>

enum UpdateStage {
    kUpdateIdle = 0,     // not checked yet
    kUpdateChecking,
    kUpdateCurrent,      // this is the newest version
    kUpdateAvailable,    // a newer version can be installed
    kUpdateDownloading,
    kUpdateReady,        // swapped: restart to finish
    kUpdateFailed,       // message says why
};

struct UpdateState {
    int stage = kUpdateIdle;
    std::string message;   // English; the interface translates the known ones
    std::string latest;    // newest version, "2.5.0"
    std::string notes;     // its release notes
};

struct ReleaseInfo {
    std::string version;   // from the tag, without the "v"
    std::string notes, page, exeUrl, sigUrl;
};

class Updater {
public:
    explicit Updater(const std::string& currentVersion);
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    static bool configured();          // this build knows where updates come from (and can install them)
    static std::string source();       // "owner/repo", or empty

    void check();                      // in the background
    void install();                    // in the background; only after check() found one
    UpdateState state() const;
    bool restartWanted() const;        // installed: close the app, then call relaunch()

    static void removeOldVersion();    // at startup: delete what the last update replaced
    static bool relaunch();            // start the (new) program again

    // For tests: where the GitHub API is, and the key and repo to use.
    static void overrideForTests(const std::string& apiBase, const std::string& repo, const std::string& keyHex);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Pieces, exposed for testing.
bool parseRelease(const std::string& json, ReleaseInfo& out, std::string& err);
int compareVersions(const std::string& a, const std::string& b);  // <0, 0, >0 like strcmp; "v2.5" == "2.5.0"
bool derSignatureToRaw(const std::string& der, std::string& raw64);  // openssl's DER -> r||s, 32 bytes each
// SHA-256 + ECDSA P-256 check against a public key given as 128 hex digits (X then Y).
// Windows: the system's crypto library. Elsewhere: always false (updates aren't offered there).
bool verifySignature(const std::string& data, const std::string& raw64, const std::string& keyHex);
