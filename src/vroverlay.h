// Captions inside the headset: a SteamVR overlay (Valve's own system that XSOverlay and OVR Toolkit
// use), so it shows over any SteamVR game, VRChat included. Off unless switched on.
//
// Kept light: a background thread that wakes 20 times a second to check for changes, redraws the
// panel only when the text or the look changes, and hides the overlay entirely when there's nothing
// to show. Needs openvr_api.dll next to the program (the build puts it there) and a running SteamVR;
// it never starts SteamVR by itself.
#pragma once
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <vector>

#include "captionpanel.h"

enum VrPlacement { kVrHead = 0, kVrLeftHand = 1, kVrRightHand = 2, kVrFixed = 3 };

struct VrSettings {
    bool on = false;
    int placement = kVrHead;
    // Head and Fixed: where the panel sits, in metres from your eyes.
    float distance = 1.3f, height = -0.32f, sideways = 0.f;
    float width = 0.8f;       // panel width in metres (Head / Fixed)
    float handWidth = 0.26f;  // panel width on a hand
    float fixedPose[12] = {}; // Fixed: where it was put (a 3x4 matrix), all zero = not placed yet
    // What and how
    bool showMe = true;       // also what you say (and what the voice says for you)
    bool showOriginal = false;
    float fadeSeconds = 8.f;  // hide after this long without anything new; 0 = never
    float opacity = 0.95f;    // whole overlay
    PanelStyle style;         // text size, lines, colours, panel, glow (style.showSub follows showOriginal)
    bool dashboard = true;    // a "CIMA Box" tab in the SteamVR menu: switch who you're talking to
    bool showTalkingTo = true;  // small "Talking to: ..." note on the caption panel
    // Push to talk with a controller button (SteamVR Input). Connects to SteamVR for it even when
    // the captions are off.
    bool pttButton = false;
    bool buzz = true;           // a short vibration when recording starts and stops
};

// What the SteamVR menu tab shows (texts come translated from the app).
// The CIMA Box tab in the SteamVR menu (rows of buttons; the app numbers them), and the note on the
// caption panel ("Talking to: a girl").
struct VrMenu {
    std::string title, footer, tag;
    std::vector<MenuRow> rows;
    bool operator==(const VrMenu& o) const { return title == o.title && footer == o.footer && tag == o.tag && rows == o.rows; }
};

struct VrState {
    std::string status;   // "Showing in SteamVR", "Waiting for SteamVR to start", ...
    bool connected = false;
    bool visible = false;
    // The push-to-talk button
    int recoveries = 0;   // times the caption panel was rebuilt or SteamVR reconnected because it stopped updating
    std::string button;   // "Not picked yet", "Ready", ... (empty when it isn't used)
    bool buttonBound = false;
    bool buttonDown = false;
};

class VrOverlay {
public:
    VrOverlay();
    ~VrOverlay();
    VrOverlay(const VrOverlay&) = delete;
    VrOverlay& operator=(const VrOverlay&) = delete;

    static bool supported();  // false: built without SteamVR support (not Windows)

    void start();
    void stop();
    void configure(const VrSettings& s);                             // cheap; call every frame
    void setLines(const std::vector<PanelLine>& lines, uint64_t version);  // only re-sent when `version` changes
    void placeHere();                                                // Fixed: put it in front of you now
    bool takePlacedPose(float pose[12]);                             // after placeHere(): the pose, to save
    void setMenu(const VrMenu& m);                             // cheap; call every frame
    bool takeMenuPress(int& id);                               // a button pressed in the SteamVR menu (its id), once
    void setThumbnail(const uint8_t* rgba, int size);                // the menu tab's icon (square RGBA)
    VrState state() const;
    // Push to talk with a controller button.
    void setActionManifest(const std::string& path);  // SteamVR Input's description of CIMA Box's actions
    bool buttonDown() const;                          // cheap, any thread
    void buzz();                                      // a short vibration on the hand that pressed it
    void openBindings();                              // SteamVR's screen for picking the button
    // Starts the headset side over: lets go of SteamVR, connects again and redraws everything.
    void reset();
    // Where problems with SteamVR are written (set before start; called from the VR thread).
    std::function<void(const std::string&)> onLog;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// Shared with the preview on the VR page, so both draw exactly the same thing.
CaptionPainter& sharedPainter();
