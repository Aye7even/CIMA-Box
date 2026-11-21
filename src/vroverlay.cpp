#include "vroverlay.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

CaptionPainter& sharedPainter() {
    static CaptionPainter painter;
    return painter;
}

#if defined(_WIN32) && defined(CIMA_HAS_OPENVR)
// ---------------------------------------------------------------- Windows + SteamVR

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <openvr.h>

#include "appaudio.h"  // findAppPid: is SteamVR running?

namespace {

using Clock = std::chrono::steady_clock;
using Vec3 = std::array<float, 3>;

Vec3 sub(Vec3 a, Vec3 b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
Vec3 norm(Vec3 a) {
    const float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    return l > 1e-6f ? Vec3{a[0] / l, a[1] / l, a[2] / l} : Vec3{0, 0, 1};
}

// A pose with the panel at `pos`, its front (+Z) pointing along `facing`, kept upright.
vr::HmdMatrix34_t poseFacing(Vec3 pos, Vec3 facing, Vec3 up = {0, 1, 0}) {
    const Vec3 z = norm(facing);
    Vec3 x = cross(up, z);
    if (std::fabs(x[0]) + std::fabs(x[1]) + std::fabs(x[2]) < 1e-5f) x = {1, 0, 0};
    x = norm(x);
    const Vec3 y = cross(z, x);
    vr::HmdMatrix34_t m;
    for (int r = 0; r < 3; ++r) {
        m.m[r][0] = x[size_t(r)];
        m.m[r][1] = y[size_t(r)];
        m.m[r][2] = z[size_t(r)];
        m.m[r][3] = pos[size_t(r)];
    }
    return m;
}

}  // namespace

struct VrOverlay::Impl {
    mutable std::mutex mtx;
    VrSettings cfg;
    std::vector<PanelLine> lines;
    uint64_t version = 0;
    VrState st;
    bool placeRequest = false, placedReady = false;
    float placed[12] = {};
    VrMenu menuWanted;
    int pressed = -1;                  // id of a menu button pressed, for the app to pick up
    std::vector<uint8_t> thumb;
    int thumbSize = 0;

    std::thread thread;
    std::atomic<bool> running{false};

    // Push to talk with a controller button
    std::string manifest;                 // set before start
    std::atomic<bool> pttDown{false}, buzzWanted{false}, bindingsWanted{false};
    bool inputReady = false;              // thread-only from here
    vr::VRActionSetHandle_t actionSet = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t pttAction = vr::k_ulInvalidActionHandle, hapticAction = vr::k_ulInvalidActionHandle;
    vr::VRInputValueHandle_t lastHand = vr::k_ulInvalidInputValueHandle;
    Clock::time_point nextBindCheck = Clock::now();

    // Keeping the panel alive: SteamVR can refuse a picture or drop or hide the panel (a scene switch,
    // the compositor restarting). Every call is checked; the panel is rebuilt when that happens, and
    // SteamVR reconnected when rebuilding doesn't help.
    std::atomic<bool> resetWanted{false};
    std::function<void(const std::string&)> log;
    std::vector<Clock::time_point> rebuilt;  // recent rebuilds (thread-only)
    Clock::time_point lastReconnect = Clock::now() - std::chrono::minutes(5);
    int uploadFails = 0;                     // pictures refused in a row
    float shrink = 1.f;                      // < 1: the picture is drawn smaller (same size in the headset)
    Clock::time_point nextCheck = Clock::now(), lastHiddenNote = Clock::now() - std::chrono::minutes(5);

    // thread-only
    bool inited = false, waitForExit = false;
    vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t dash = vr::k_ulOverlayHandleInvalid, dashThumb = vr::k_ulOverlayHandleInvalid;
    VrMenu shownMenu;
    bool haveShownMenu = false;
    int menuH = 0;                     // the menu picture's height (the pointer is scaled to it)
    int hover = -1, shownHover = -1;
    MenuSpec menu;
    PanelImage menuImage;
    uint64_t shownVersion = ~0ull;
    VrSettings shownCfg;
    bool haveShownCfg = false;
    PanelImage image;
    Clock::time_point lastNew = Clock::now(), nextTry = Clock::now(), nextPlace = Clock::now();
    float fade = 0.f;  // 0 hidden .. 1 fully shown
    bool overlayShown = false;
    std::string placeProblem;  // why the panel can't be placed right now, empty when it can

    void note(const std::string& s) {
        if (log) log("VR captions: " + s);
    }
    static std::string errName(vr::EVROverlayError e) {
        const char* n = vr::VROverlay() ? vr::VROverlay()->GetOverlayErrorNameFromEnum(e) : nullptr;
        return n ? n : std::to_string(int(e));
    }
    bool createCaptionOverlay() {
        const vr::EVROverlayError e = vr::VROverlay()->CreateOverlay("cimabox.captions", "CIMA Box captions", &handle);
        if (e != vr::VROverlayError_None) {
            handle = vr::k_ulOverlayHandleInvalid;
            note("couldn't create the panel (" + errName(e) + ")");
            return false;
        }
        vr::VROverlay()->SetOverlaySortOrder(handle, 10);  // above other overlays' panels
        return true;
    }
    // Throws the panel away and makes it again; redraws and places it on the next pass. When that has
    // already happened a few times in the last minute, reconnects to SteamVR instead.
    void rebuild(const std::string& why) {
        const auto now = Clock::now();
        rebuilt.erase(std::remove_if(rebuilt.begin(), rebuilt.end(), [&](Clock::time_point t) { return now - t > std::chrono::seconds(60); }),
                      rebuilt.end());
        rebuilt.push_back(now);
        {
            std::lock_guard<std::mutex> lk(mtx);
            ++st.recoveries;
        }
        if (rebuilt.size() >= 3) {
            note(why + "; rebuilding didn't help, so reconnecting to SteamVR");
            rebuilt.clear();
            shutdownVr();
            // Straight back in, unless that already happened in the last minute (then a short pause).
            nextTry = now - lastReconnect < std::chrono::seconds(60) ? now + std::chrono::seconds(3) : now;
            lastReconnect = now;
            return;
        }
        note(why + "; rebuilding the panel");
        if (handle != vr::k_ulOverlayHandleInvalid) vr::VROverlay()->DestroyOverlay(handle);
        handle = vr::k_ulOverlayHandleInvalid;
        overlayShown = false;
        shownVersion = ~0ull;
        haveShownCfg = false;
        if (!createCaptionOverlay()) {
            shutdownVr();
            nextTry = now + std::chrono::seconds(1);
        }
    }

    void setStatus(const std::string& s, bool connected) {
        std::lock_guard<std::mutex> lk(mtx);
        st.status = s;
        st.connected = connected;
    }

    void shutdownVr() {
        inputReady = false;
        pttDown = false;
        if (inited) {
            if (handle != vr::k_ulOverlayHandleInvalid) vr::VROverlay()->DestroyOverlay(handle);
            destroyDashboard();
            vr::VR_Shutdown();
        }
        handle = vr::k_ulOverlayHandleInvalid;
        inited = false;
        overlayShown = false;
        shownVersion = ~0ull;
        haveShownCfg = false;
    }

    // The "CIMA Box" tab in the SteamVR menu: rows of buttons the app describes (setMenu).
    void createDashboard() {
        if (dash != vr::k_ulOverlayHandleInvalid) return;
        vr::IVROverlay* ov = vr::VROverlay();
        if (ov->CreateDashboardOverlay("cimabox.menu", "CIMA Box", &dash, &dashThumb) != vr::VROverlayError_None) {
            dash = dashThumb = vr::k_ulOverlayHandleInvalid;
            return;
        }
        ov->SetOverlayWidthInMeters(dash, 2.2f);
        ov->SetOverlayInputMethod(dash, vr::VROverlayInputMethod_Mouse);
        menuH = 0;  // the pointer's scale is set when it is drawn
        std::vector<uint8_t> icon;
        int size = 0;
        {
            std::lock_guard<std::mutex> lk(mtx);
            icon = thumb;
            size = thumbSize;
        }
        if (size > 0) ov->SetOverlayRaw(dashThumb, icon.data(), uint32_t(size), uint32_t(size), 4);
        haveShownMenu = false;  // draw it
    }
    void destroyDashboard() {
        if (dash != vr::k_ulOverlayHandleInvalid) vr::VROverlay()->DestroyOverlay(dash);
        if (dashThumb != vr::k_ulOverlayHandleInvalid) vr::VROverlay()->DestroyOverlay(dashThumb);
        dash = dashThumb = vr::k_ulOverlayHandleInvalid;
    }
    void serviceDashboard(const VrSettings& c) {
        if (!c.dashboard) { destroyDashboard(); return; }
        createDashboard();
        if (dash == vr::k_ulOverlayHandleInvalid) return;
        vr::VREvent_t e;
        while (vr::VROverlay()->PollNextOverlayEvent(dash, &e, sizeof(e))) {
            // The pointer's y runs up from the bottom of the picture.
            const float x = e.data.mouse.x, y = float(menuH) - e.data.mouse.y;
            if (e.eventType == vr::VREvent_MouseMove) {
                hover = menuButtonAt(menu, x, y);
            } else if (e.eventType == vr::VREvent_MouseButtonUp) {
                const int b = menuButtonAt(menu, x, y);
                if (b >= 0) {
                    std::lock_guard<std::mutex> lk(mtx);
                    pressed = b;
                }
            }
        }
        VrMenu now;
        {
            std::lock_guard<std::mutex> lk(mtx);
            now = menuWanted;
        }
        if (haveShownMenu && now == shownMenu && hover == shownHover) return;
        menu.title = now.title;
        menu.footer = now.footer;
        menu.rows = now.rows;
        menu.hover = hover;
        menu.accentColor = c.style.accentColor;
        sharedPainter().paintMenu(menu, menuImage);
        if (menuImage.height != menuH) {  // rows came or went: the pointer follows the new size
            menuH = menuImage.height;
            const vr::HmdVector2_t scale = {{float(menuImage.width), float(menuImage.height)}};
            vr::VROverlay()->SetOverlayMouseScale(dash, &scale);
        }
        vr::VROverlay()->SetOverlayRaw(dash, menuImage.rgba.data(), uint32_t(menuImage.width), uint32_t(menuImage.height), 4);
        shownMenu = now;
        shownHover = hover;
        haveShownMenu = true;
    }

    bool tryInit() {
        const bool steamVrRunning = findAppPid("vrserver.exe") != 0;
        if (waitForExit) {  // SteamVR asked us to quit: wait until it is really gone before trying again
            if (steamVrRunning) return false;
            waitForExit = false;
        }
        if (!steamVrRunning) { setStatus("Waiting for SteamVR to start", false); return false; }
        vr::EVRInitError err = vr::VRInitError_None;
        vr::VR_Init(&err, vr::VRApplication_Overlay);
        if (err != vr::VRInitError_None) {
            setStatus(std::string("SteamVR: ") + vr::VR_GetVRInitErrorAsEnglishDescription(err), false);
            return false;
        }
        inited = true;
        if (!createCaptionOverlay()) {
            setStatus("Couldn't create the caption overlay in SteamVR", false);
            shutdownVr();
            return false;
        }
        lastNew = Clock::now();  // whatever is there shows again after (re)connecting
        setStatus("Connected to SteamVR", true);
        return true;
    }

    // Puts the panel where the settings say. Hands are looked up again now and then (they come and go).
    void place(const VrSettings& c) {
        placeProblem.clear();
        vr::IVROverlay* ov = vr::VROverlay();
        const float w = c.placement == kVrLeftHand || c.placement == kVrRightHand ? c.handWidth : c.width;
        ov->SetOverlayWidthInMeters(handle, std::max(0.05f, w));
        if (c.placement == kVrHead) {
            // In front of your eyes, turned to face them.
            const Vec3 pos = {c.sideways, c.height, -c.distance};
            const vr::HmdMatrix34_t m = poseFacing(pos, sub({0, 0, 0}, pos));
            ov->SetOverlayTransformTrackedDeviceRelative(handle, vr::k_unTrackedDeviceIndex_Hmd, &m);
        } else if (c.placement == kVrLeftHand || c.placement == kVrRightHand) {
            const vr::TrackedDeviceIndex_t dev = vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(
                c.placement == kVrLeftHand ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand);
            if (dev == vr::k_unTrackedDeviceIndexInvalid) {
                placeProblem = c.placement == kVrLeftHand ? "Left controller not found" : "Right controller not found";
                return;
            }
            // Over the back of the hand, tipped up towards your face.
            const vr::HmdMatrix34_t m = poseFacing({0.f, 0.035f, 0.11f}, {0.f, 1.f, 0.7f}, {0.f, 0.f, -1.f});
            ov->SetOverlayTransformTrackedDeviceRelative(handle, dev, &m);
        } else {
            vr::HmdMatrix34_t m;
            bool placedYet = false;
            for (float v : c.fixedPose) placedYet = placedYet || v != 0.f;
            if (!placedYet) {
                placeProblem = "Press \"Put it in front of me\" to place the panel";
                return;
            }
            std::memcpy(&m.m[0][0], c.fixedPose, sizeof(c.fixedPose));
            ov->SetOverlayTransformAbsolute(handle, vr::TrackingUniverseStanding, &m);
        }
    }

    // Fixed: the panel in front of you as you are now, upright, facing you.
    void placeInFront(const VrSettings& c) {
        vr::TrackedDevicePose_t poses[1];
        vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.f, poses, 1);
        if (!poses[0].bPoseIsValid) return;
        const vr::HmdMatrix34_t& h = poses[0].mDeviceToAbsoluteTracking;
        const Vec3 eye = {h.m[0][3], h.m[1][3], h.m[2][3]};
        Vec3 fwd = {-h.m[0][2], 0.f, -h.m[2][2]};  // where you look, kept level
        fwd = norm(fwd);
        const Vec3 right = norm(cross(fwd, {0, 1, 0}));
        const Vec3 pos = {eye[0] + fwd[0] * c.distance + right[0] * c.sideways, eye[1] + c.height,
                          eye[2] + fwd[2] * c.distance + right[2] * c.sideways};
        const vr::HmdMatrix34_t m = poseFacing(pos, {-fwd[0], 0.f, -fwd[2]});
        std::lock_guard<std::mutex> lk(mtx);
        std::memcpy(placed, &m.m[0][0], sizeof(placed));
        std::memcpy(cfg.fixedPose, placed, sizeof(placed));
        placedReady = true;
    }

    // SteamVR Input: CIMA Box's "Push to talk" action, bound to whatever button you pick in SteamVR's
    // controller bindings. Read every pass through the loop (100 times a second while it's used).
    void serviceInput(const VrSettings& c) {
        if (!c.pttButton || manifest.empty()) {
            pttDown = false;
            std::lock_guard<std::mutex> lk(mtx);
            st.button.clear();
            st.buttonBound = st.buttonDown = false;
            return;
        }
        vr::IVRInput* in = vr::VRInput();
        if (!in) return;
        if (!inputReady) {
            const vr::EVRInputError e = in->SetActionManifestPath(manifest.c_str());
            if (e != vr::VRInputError_None) {
                std::lock_guard<std::mutex> lk(mtx);
                st.button = "SteamVR couldn't load CIMA Box's controls";
                return;
            }
            in->GetActionSetHandle("/actions/cimabox", &actionSet);
            in->GetActionHandle("/actions/cimabox/in/push_to_talk", &pttAction);
            in->GetActionHandle("/actions/cimabox/out/buzz", &hapticAction);
            inputReady = true;
            nextBindCheck = Clock::now();
        }
        vr::VRActiveActionSet_t active = {};
        active.ulActionSet = actionSet;
        in->UpdateActionState(&active, sizeof(active), 1);
        vr::InputDigitalActionData_t d = {};
        const bool ok = in->GetDigitalActionData(pttAction, &d, sizeof(d), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None;
        const bool down = ok && d.bActive && d.bState;
        if (down && d.activeOrigin != vr::k_ulInvalidInputValueHandle) {
            vr::InputOriginInfo_t info = {};
            if (in->GetOriginTrackedDeviceInfo(d.activeOrigin, &info, sizeof(info)) == vr::VRInputError_None) lastHand = info.devicePath;
        }
        pttDown = down;
        if (buzzWanted.exchange(false) && c.buzz && hapticAction != vr::k_ulInvalidActionHandle)
            in->TriggerHapticVibrationAction(hapticAction, 0.f, 0.045f, 170.f, 0.55f, lastHand);
        if (bindingsWanted.exchange(false)) in->OpenBindingUI(nullptr, actionSet, vr::k_ulInvalidInputValueHandle, true);
        // Is a button bound to it? Checked now and then (the answer changes when you pick one).
        const auto now = Clock::now();
        if (now >= nextBindCheck) {
            nextBindCheck = now + std::chrono::seconds(2);
            vr::VRInputValueHandle_t origins[vr::k_unMaxActionOriginCount] = {};
            bool bound = false;
            if (in->GetActionOrigins(actionSet, pttAction, origins, vr::k_unMaxActionOriginCount) == vr::VRInputError_None)
                for (vr::VRInputValueHandle_t o : origins) bound = bound || o != vr::k_ulInvalidInputValueHandle;
            std::lock_guard<std::mutex> lk(mtx);
            st.buttonBound = bound;
        }
        std::lock_guard<std::mutex> lk(mtx);
        st.buttonDown = down;
        st.button = down ? "Button pressed" : st.buttonBound ? "Button ready" : "No button picked yet";
    }

    static bool sameLook(const VrSettings& a, const VrSettings& b) {
        const PanelStyle &x = a.style, &y = b.style;
        return x.width == y.width && x.textPx == y.textPx && x.maxLines == y.maxLines && x.textColor == y.textColor &&
               x.accentColor == y.accentColor && x.meColor == y.meColor && x.panelOpacity == y.panelOpacity &&
               x.glow == y.glow && a.showOriginal == b.showOriginal && x.tag == y.tag;
    }
    static bool samePlace(const VrSettings& a, const VrSettings& b) {
        return a.placement == b.placement && a.distance == b.distance && a.height == b.height && a.sideways == b.sideways &&
               a.width == b.width && a.handWidth == b.handWidth && std::memcmp(a.fixedPose, b.fixedPose, sizeof(a.fixedPose)) == 0;
    }

    void loop() {
        while (running) {
            VrSettings c;
            std::vector<PanelLine> ls;
            uint64_t ver;
            bool wantPlace;
            {
                std::lock_guard<std::mutex> lk(mtx);
                c = cfg;
                ver = version;
                wantPlace = placeRequest;
                placeRequest = false;
                if (ver != shownVersion) ls = lines;
            }
            const auto now = Clock::now();
            if (!c.on && !c.pttButton) {
                if (inited) shutdownVr();
                setStatus("Off", false);
                serviceInput(c);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
            if (!inited) {
                if (now >= nextTry) {
                    nextTry = now + std::chrono::seconds(3);
                    tryInit();
                }
                if (!inited) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    continue;
                }
            }

            vr::VREvent_t ev;
            while (vr::VRSystem()->PollNextEvent(&ev, sizeof(ev))) {
                if (ev.eventType == vr::VREvent_Quit) {  // SteamVR is closing: let go of it right away
                    vr::VRSystem()->AcknowledgeQuit_Exiting();
                    shutdownVr();
                    waitForExit = true;
                    setStatus("SteamVR closed. Waiting for it to start again", false);
                    break;
                }
            }
            if (!inited) continue;
            if (resetWanted.exchange(false)) {  // "Reset VR captions": start the headset side over
                note("reset (asked for)");
                shutdownVr();
                nextTry = now;
                setStatus("Resetting", false);
                continue;
            }
            serviceInput(c);
            if (!c.on) {  // only the push-to-talk button: no panel, no menu tab
                if (overlayShown) { vr::VROverlay()->HideOverlay(handle); overlayShown = false; }
                fade = 0.f;
                destroyDashboard();
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    st.visible = false;
                    st.status = "Connected to SteamVR";
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            if (wantPlace) {
                placeInFront(c);
                std::lock_guard<std::mutex> lk(mtx);
                c = cfg;
            }
            std::string tag;
            {
                std::lock_guard<std::mutex> lk(mtx);
                tag = menuWanted.tag;
            }
            serviceDashboard(c);
            c.style.tag = c.showTalkingTo ? tag : std::string();

            // Redraw only when the words or the look changed.
            const bool lookChanged = !haveShownCfg || !sameLook(c, shownCfg);
            if (ver != shownVersion || lookChanged) {
                if (ver != shownVersion && !ls.empty()) lastNew = now;
                PanelStyle style = c.style;
                style.showSub = c.showOriginal;
                if (shrink < 1.f) {  // a smaller picture, in case SteamVR's limit on its size is what's refusing it
                    style.width = int(float(style.width) * shrink);
                    style.textPx *= shrink;
                }
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    if (ver == shownVersion) ls = lines;
                }
                sharedPainter().paint(ls, style, image);
                shownVersion = ver;
                if (!image.empty) {
                    const vr::EVROverlayError e =
                        vr::VROverlay()->SetOverlayRaw(handle, image.rgba.data(), uint32_t(image.width), uint32_t(image.height), 4);
                    if (e != vr::VROverlayError_None) {
                        if (++uploadFails >= 4 && shrink > 0.5f) {
                            shrink = 0.5f;
                            note("drawing the panel at half resolution from now on");
                        }
                        rebuild("SteamVR didn't take the new captions (" + errName(e) + ")");
                        if (!inited) continue;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        continue;  // drawn again on the next pass, on the new panel
                    }
                    uploadFails = 0;
                    lastNew = now;  // a new picture shows, even if the words were the same
                }
            }
            {  // SteamVR's own messages about the panel: a picture it couldn't load means a rebuild
                vr::VREvent_t pe;
                bool failed = false;
                while (vr::VROverlay()->PollNextOverlayEvent(handle, &pe, sizeof(pe)))
                    if (pe.eventType == vr::VREvent_ImageFailed) failed = true;
                if (failed) {
                    rebuild("SteamVR couldn't load the captions picture");
                    continue;
                }
            }
            if (now >= nextCheck) {  // every few seconds: is the panel still there, and really visible?
                nextCheck = now + std::chrono::seconds(3);
                vr::VROverlayHandle_t found = vr::k_ulOverlayHandleInvalid;
                if (vr::VROverlay()->FindOverlay("cimabox.captions", &found) != vr::VROverlayError_None || found != handle) {
                    rebuild("SteamVR had dropped the caption panel");
                    continue;
                }
                if (overlayShown && !vr::VROverlay()->IsOverlayVisible(handle)) {
                    if (now - lastHiddenNote > std::chrono::minutes(1)) {
                        note("SteamVR had hidden the panel; showing it again");
                        lastHiddenNote = now;
                    }
                    vr::VROverlay()->ShowOverlay(handle);
                }
            }
            if (!haveShownCfg || !samePlace(c, shownCfg) || now >= nextPlace) {
                place(c);
                nextPlace = now + std::chrono::seconds(2);
            }
            shownCfg = c;
            haveShownCfg = true;

            // Show while there is something new; fade out after a quiet while; hidden costs nothing.
            const float quiet = std::chrono::duration<float>(now - lastNew).count();
            const bool want = !image.empty && (c.fadeSeconds <= 0.f || quiet < c.fadeSeconds);
            const float target = want ? 1.f : 0.f;
            const bool animating = std::fabs(fade - target) > 0.001f;
            if (animating) fade = target > fade ? std::min(1.f, fade + 0.25f) : std::max(0.f, fade - 0.08f);
            if (fade > 0.f && !overlayShown) { vr::VROverlay()->ShowOverlay(handle); overlayShown = true; }
            if (fade > 0.f || animating) vr::VROverlay()->SetOverlayAlpha(handle, fade * c.opacity);
            if (fade <= 0.f && overlayShown) { vr::VROverlay()->HideOverlay(handle); overlayShown = false; }
            {
                std::lock_guard<std::mutex> lk(mtx);
                st.visible = overlayShown;
                st.status = !placeProblem.empty() ? placeProblem
                            : overlayShown        ? "Showing in SteamVR"
                                                  : "Connected to SteamVR, waiting for something to show";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(c.pttButton ? 10 : animating ? 25 : 50));
        }
        shutdownVr();
    }
};

bool VrOverlay::supported() { return true; }

#else
// ---------------------------------------------------------------- built without SteamVR

struct VrOverlay::Impl {
    mutable std::mutex mtx;
    VrState st;
};
bool VrOverlay::supported() { return false; }

#endif

VrOverlay::VrOverlay() : p_(new Impl) { p_->st.status = supported() ? "Off" : "VR captions need the Windows build"; }
VrOverlay::~VrOverlay() { stop(); }

#if defined(_WIN32) && defined(CIMA_HAS_OPENVR)
void VrOverlay::start() {
    if (p_->running.exchange(true)) return;
    p_->log = onLog;
    p_->thread = std::thread([this] { p_->loop(); });
}
void VrOverlay::stop() {
    if (!p_->running.exchange(false)) return;
    p_->thread.join();
}
void VrOverlay::configure(const VrSettings& s) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    const bool placedNew = p_->placedReady;
    float keep[12];
    std::memcpy(keep, p_->cfg.fixedPose, sizeof(keep));
    p_->cfg = s;
    if (placedNew) std::memcpy(p_->cfg.fixedPose, keep, sizeof(keep));  // a fresh placement wins until it is saved
}
void VrOverlay::setActionManifest(const std::string& path) { p_->manifest = path; }
bool VrOverlay::buttonDown() const { return p_->pttDown; }
void VrOverlay::buzz() { p_->buzzWanted = true; }
void VrOverlay::openBindings() { p_->bindingsWanted = true; }
void VrOverlay::reset() { p_->resetWanted = true; }
void VrOverlay::setLines(const std::vector<PanelLine>& lines, uint64_t version) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    if (version == p_->version) return;
    p_->lines = lines;
    p_->version = version;
}
void VrOverlay::placeHere() {
    std::lock_guard<std::mutex> lk(p_->mtx);
    p_->placeRequest = true;
}
bool VrOverlay::takePlacedPose(float pose[12]) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    if (!p_->placedReady) return false;
    std::memcpy(pose, p_->placed, sizeof(p_->placed));
    p_->placedReady = false;
    return true;
}
void VrOverlay::setMenu(const VrMenu& m) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    p_->menuWanted = m;
}
bool VrOverlay::takeMenuPress(int& id) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    if (p_->pressed < 0) return false;
    id = p_->pressed;
    p_->pressed = -1;
    return true;
}
void VrOverlay::setThumbnail(const uint8_t* rgba, int size) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    p_->thumb.assign(rgba, rgba + size_t(size) * size_t(size) * 4);
    p_->thumbSize = size;
}
#else
void VrOverlay::start() {}
void VrOverlay::stop() {}
void VrOverlay::configure(const VrSettings&) {}
void VrOverlay::setLines(const std::vector<PanelLine>&, uint64_t) {}
void VrOverlay::placeHere() {}
bool VrOverlay::takePlacedPose(float*) { return false; }
void VrOverlay::setMenu(const VrMenu&) {}
bool VrOverlay::takeMenuPress(int&) { return false; }
void VrOverlay::setThumbnail(const uint8_t*, int) {}
void VrOverlay::setActionManifest(const std::string&) {}
bool VrOverlay::buttonDown() const { return false; }
void VrOverlay::buzz() {}
void VrOverlay::openBindings() {}
void VrOverlay::reset() {}
#endif

VrState VrOverlay::state() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->st;
}
