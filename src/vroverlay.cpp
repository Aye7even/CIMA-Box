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
    VrTalking talking;
    int pressed = -1;                  // menu button pressed, for the app to pick up
    std::vector<uint8_t> thumb;
    int thumbSize = 0;

    std::thread thread;
    std::atomic<bool> running{false};

    // thread-only
    bool dllOk = false, inited = false, waitForExit = false;
    vr::VROverlayHandle_t handle = vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t dash = vr::k_ulOverlayHandleInvalid, dashThumb = vr::k_ulOverlayHandleInvalid;
    VrTalking shownTalking;
    bool haveShownTalking = false;
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

    void setStatus(const std::string& s, bool connected) {
        std::lock_guard<std::mutex> lk(mtx);
        st.status = s;
        st.connected = connected;
    }

    void shutdownVr() {
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

    // The "CIMA Box" tab in the SteamVR menu: a row of buttons for who you're talking to.
    void createDashboard() {
        if (dash != vr::k_ulOverlayHandleInvalid) return;
        vr::IVROverlay* ov = vr::VROverlay();
        if (ov->CreateDashboardOverlay("cimabox.menu", "CIMA Box", &dash, &dashThumb) != vr::VROverlayError_None) {
            dash = dashThumb = vr::k_ulOverlayHandleInvalid;
            return;
        }
        ov->SetOverlayWidthInMeters(dash, 2.2f);
        ov->SetOverlayInputMethod(dash, vr::VROverlayInputMethod_Mouse);
        const vr::HmdVector2_t scale = {{float(menu.width), float(menu.height)}};
        ov->SetOverlayMouseScale(dash, &scale);
        std::vector<uint8_t> icon;
        int size = 0;
        {
            std::lock_guard<std::mutex> lk(mtx);
            icon = thumb;
            size = thumbSize;
        }
        if (size > 0) ov->SetOverlayRaw(dashThumb, icon.data(), uint32_t(size), uint32_t(size), 4);
        haveShownTalking = false;  // draw it
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
            // Pointer y runs up from the bottom of the image; the buttons are centred, so either way works.
            const float x = e.data.mouse.x, y = float(menu.height) - e.data.mouse.y;
            if (e.eventType == vr::VREvent_MouseMove) {
                hover = menuButtonAt(menu, x, y);
            } else if (e.eventType == vr::VREvent_MouseButtonUp) {
                const int b = menuButtonAt(menu, x, y);
                if (b >= 0) {
                    std::lock_guard<std::mutex> lk(mtx);
                    pressed = b;
                    talking.choice = b;  // shows right away, before the app confirms it
                }
            }
        }
        VrTalking now;
        {
            std::lock_guard<std::mutex> lk(mtx);
            now = talking;
        }
        if (haveShownTalking && now == shownTalking && hover == shownHover) return;
        menu.title = now.title;
        menu.footer = now.footer;
        menu.buttons = now.buttons;
        menu.selected = now.choice;
        menu.hover = hover;
        menu.accentColor = c.style.accentColor;
        sharedPainter().paintMenu(menu, menuImage);
        vr::VROverlay()->SetOverlayRaw(dash, menuImage.rgba.data(), uint32_t(menuImage.width), uint32_t(menuImage.height), 4);
        shownTalking = now;
        shownHover = hover;
        haveShownTalking = true;
    }

    bool tryInit() {
        if (!dllOk) {
            // Loaded on demand (the program starts fine without it).
            dllOk = LoadLibraryA("openvr_api.dll") != nullptr;
            if (!dllOk) { setStatus("openvr_api.dll is missing next to CIMABox.exe", false); return false; }
        }
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
        if (vr::VROverlay()->CreateOverlay("cimabox.captions", "CIMA Box captions", &handle) != vr::VROverlayError_None) {
            setStatus("Couldn't create the caption overlay in SteamVR", false);
            shutdownVr();
            return false;
        }
        vr::VROverlay()->SetOverlaySortOrder(handle, 10);  // above other overlays' panels
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
            if (!c.on) {
                if (inited) shutdownVr();
                setStatus("Off", false);
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

            if (wantPlace) {
                placeInFront(c);
                std::lock_guard<std::mutex> lk(mtx);
                c = cfg;
            }
            VrTalking t;
            {
                std::lock_guard<std::mutex> lk(mtx);
                t = talking;
            }
            serviceDashboard(c);
            c.style.tag = c.showTalkingTo ? t.tag : std::string();

            // Redraw only when the words or the look changed.
            const bool lookChanged = !haveShownCfg || !sameLook(c, shownCfg);
            if (ver != shownVersion || lookChanged) {
                if (ver != shownVersion && !ls.empty()) lastNew = now;
                PanelStyle style = c.style;
                style.showSub = c.showOriginal;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    if (ver == shownVersion) ls = lines;
                }
                sharedPainter().paint(ls, style, image);
                if (!image.empty)
                    vr::VROverlay()->SetOverlayRaw(handle, image.rgba.data(), uint32_t(image.width), uint32_t(image.height), 4);
                shownVersion = ver;
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
            std::this_thread::sleep_for(std::chrono::milliseconds(animating ? 25 : 50));
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
void VrOverlay::setTalking(const VrTalking& t) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    if (p_->pressed >= 0) return;  // a press in VR is on its way to the app: don't undo it
    p_->talking = t;
}
bool VrOverlay::takeTalkingTo(int& choice) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    if (p_->pressed < 0) return false;
    choice = p_->pressed;
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
void VrOverlay::setTalking(const VrTalking&) {}
bool VrOverlay::takeTalkingTo(int&) { return false; }
void VrOverlay::setThumbnail(const uint8_t*, int) {}
#endif

VrState VrOverlay::state() const {
    std::lock_guard<std::mutex> lk(p_->mtx);
    return p_->st;
}
