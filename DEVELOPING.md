# Developing CIMA Box

Notes for building, releasing and changing CIMA Box. The user-facing overview is in
[README.md](README.md); the full user guide is `CIMA Box - Read Me.txt`.

## Building

Needs CMake 3.20+, a C++17 compiler and git. Everything else (Dear ImGui, GLFW, miniaudio,
nlohmann/json, the OpenVR SDK on Windows, and curl if your system has none) is downloaded by CMake on the first configure.

**Windows** (Visual Studio 2022 with "Desktop development with C++"):

    cmake -B build
    cmake --build build --config Release
    build\Release\CIMABox.exe

**macOS** (Xcode command line tools):

    cmake -B build && cmake --build build
    ./build/CIMABox

**Linux** (Debian/Ubuntu):

    sudo apt install build-essential cmake git libcurl4-openssl-dev libgl1-mesa-dev xorg-dev
    cmake -B build && cmake --build build
    ./build/CIMABox

Settings (`cimabox.json`) save a moment after every change, by default in `%APPDATA%\CIMA Box`
(macOS/Linux: `~/.config/cimabox/`), so builds in any folder share them. Settings > About can move
them to `Documents\CIMA Box Config` or next to the program; that choice is kept in
`%APPDATA%\CIMA Box\settings-location.txt`. With no choice made, a `cimabox.json` next to the
program is used where it is.

## Publishing updates (GitHub)

Once this is set up, releasing a new version is three commands, and everyone's CIMA Box offers it:
an "Update to 2.6.0" button appears at the top, and Settings > Updates installs it ("Update and
restart"). Settings and keys are kept.

How it works: pushing a tag like `v2.6.0` runs `.github/workflows/release.yml` on GitHub's own
Windows machines. It builds CIMA Box, signs `CIMABox.exe` with your private key, and publishes a
GitHub release with `CIMABox.exe`, `CIMABox.exe.sig` and a zip for new users. The app checks the
latest release, and only installs a file whose signature matches the public key built into it, so
nobody else can push an update to your friends, even with access to the download.

### One-time setup

1. **A GitHub repository.** Create one on github.com. It should be **public**: the app reads the
   releases without logging in, and GitHub hides private repositories' releases. (Your API keys are
   never in the code: `cimabox.json` stays on each PC and `.gitignore` keeps it out.)
2. **Put the project in it.** With Git for Windows installed, in the project folder:

        git init
        git add .
        git commit -m "CIMA Box 2.5"
        git branch -M main
        git remote add origin https://github.com/YOUR-NAME/YOUR-REPO.git
        git push -u origin main

3. **Make your signing key.** In Git Bash, in the project folder:

        bash tools/updates/make-update-key.sh

   This writes `update_key.txt` (public, commit it) and `update_private_key.pem` (secret).
4. **Give GitHub the private key.** On the repository page: Settings > Secrets and variables >
   Actions > New repository secret. Name: `UPDATE_SIGNING_KEY`. Value: the whole content of
   `update_private_key.pem` (open it in Notepad, copy everything). Then **back the file up** somewhere
   safe, like a password manager, and keep it out of git (`.gitignore` already does). If you lose it,
   you make a new key, and everyone installs one version by hand.
5. **Commit the public key:**

        git add update_key.txt
        git commit -m "Update signing key"
        git push

### Each release

1. Set the version in `CMakeLists.txt`: `project(CIMABox VERSION 2.7.0 ...)`. Use three numbers.
2. Add what changed to the top of `CHANGELOG.md`, written for the people using the app:

        ## 2.7.0
        - What's different, one line per change.

   This becomes the release notes on GitHub, the "Update to 2.7.0" notes in the app, and the
   "What's new" window people see after updating. The release stops if this section is missing.
3. Commit, tag the same version with a `v`, and push both:

        git commit -am "CIMA Box 2.7.0"
        git tag v2.7.0
        git push origin main v2.7.0

   Push the one tag by its name, as above. `--tags` pushes every tag on this PC that GitHub
   doesn't have yet, and each one starts a release of its own: an old tag left over from an
   earlier version gets published again next to the new one.

3. Watch it under the repository's **Actions** tab (5 to 10 minutes). When it's green, the release is
   on the Releases page, and apps pick it up the next time they start (or with Check for updates).
   If the tag and the version in `CMakeLists.txt` differ, it stops and says so.

Your friends install CIMA Box from the release zip once, by hand (the Download link in README.md); after
that it updates itself.
Builds you make on your own PC have no updater unless you configure them with
`-DCIMA_UPDATE_REPO=YOUR-NAME/YOUR-REPO` (and `update_key.txt` present).

Notes:
- Only `CIMABox.exe` is replaced, and it's all the program needs: OpenVR (SteamVR) is built into
  it from Valve's source code since 2.7.2, so there's no `openvr_api.dll` any more.
- The program must be in a folder its user can write to (not `Program Files`), or the update can't
  replace it. It says so if that's the case.
- Windows SmartScreen may warn about the zip the first time ("unrecognized app"): More info > Run
  anyway. Updates installed from inside the app don't go through that.
- GitHub allows 60 update checks an hour per internet connection without logging in, far more than
  one check per start needs.

## Interface translations

The built-in tables are in `tools/i18n/` (one JSON per language). After changing the
interface, run `python tools/i18n/extract.py` then `python tools/i18n/build.py`; it reports missing
texts and broken `%` codes, and writes `src/i18n_data.h`. `%1`, `%2`... are the clickable words in Home's sentence;
a translation can put them in any order.

How people add their own language without rebuilding (a file in a `lang` folder) is explained in
`CIMA Box - Read Me.txt`.

## Key guide pictures

The pictures in the setup's "how do I get this key?" guides live in `assets/guide/*.jpg` (already
cropped, with personal details blurred and the clicks marked). They're built into the program
through `src/guide_data.h`; after changing a picture, run `python tools/guide/make_header.py`.
They're decoded with [stb_image](https://github.com/nothings/stb) (public domain), in
`src/third_party/`.

## The app icon

The picture in `assets/icon.png` is built into the program: the window and taskbar icon, the logo
next to the name, and on Windows the icon of the `.exe` file itself (Explorer, shortcuts). Nothing
needs to sit next to the program.

To change it later, replace `assets/icon.png` and `assets/cimabox.ico` and regenerate
`src/logo_data.h` (raw pixels of the picture at 128, 64, 48, 32 and 16 pixels), then rebuild.

## Technical notes

- **Delay.** ElevenLabs works on one phrase at a time, so a result arrives after the speaker pauses.
  The app keeps its connections open and reconnects the moment speech starts. For the shortest delay
  on My voice: Changer mode, or a fast model (Flash v2.5, v4 Turbo), and a short "pause that ends a
  phrase". On Listen: *Sooner*. The Captions panel shows how long the last caption took.
- **Cut-off endings.** Recording keeps the quiet tail of a phrase, follows word endings that drop
  below the threshold, and runs 0.3 s past releasing the push-to-talk key.
- **Cost.** Changer is billed per second of audio; Re-speak and Translate per transcribed second
  plus per character spoken. Listen is billed per second of speech it transcribes, for as long as it
  is switched on and people are talking.
- **Privacy.** Listen sends other people's speech to ElevenLabs and their words to the translation
  service. Captions are kept only in memory, never written to disk.
- **Sliders.** v4 models take stability and similarity only (plus clone strength for professional clones). v3 accepts stability 0, 0.5 or 1, so
  the slider is rounded for it.
- **Push to talk** (and the "Talking to" hotkey) is global on Windows, checked every 8 ms by its own
  thread, so it reacts instantly even while the window is in the background. On macOS/Linux it only
  works while this window is focused.
- **Text in some scripts** (Hindi and other Indian scripts, Burmese, Thai combining marks) can look
  wrong in the app's text lines: letters show, but aren't combined the way they should be (the text
  renderer doesn't shape them). Arabic, Persian, Urdu and Hebrew are handled by `src/bidi.*`. The
  voice and the chatbox are unaffected.
- **Settings** are saved to `%APPDATA%\CIMA Box\cimabox.json` a moment after every change (Settings >
  About > Open settings folder). That file holds your API keys in plain text - don't share it.

## Layout

    src/core.h        ring buffer, phrase segmenter (voice activation / push to talk), WAV writer
    src/http.*        libcurl wrapper that keeps connections open
    src/eleven.*      ElevenLabs REST client
    src/livestt.*     live transcription: ElevenLabs realtime speech-to-text over a WebSocket (curl is built
                      with ENABLE_WEBSOCKETS); any problem falls back to the regular upload per phrase
    src/translate.*   MyMemory, DeepL, Claude, Gemini and OpenAI translation; languages and dialects
    src/bidi.*        Arabic shaping and right-to-left reordering (ImGui hook: cmake/imgui_rtl.inl)
    src/i18n.*        the app's own language (src/i18n_data.h is generated by tools/i18n/build.py)
    src/updater.*     updates from GitHub Releases, with signature check
    src/changelog.*   reads CHANGELOG.md (built into the program by CMake)
    src/listen.*      Listen pipeline: sources, phrases, workers, live words, voice recognition, caption feed
    src/extras.*      Features page helpers: word list, swear words, quick-phrase counts, setup codes
    src/screen.*      screen translation: window pictures, crop, PNG, Windows OCR (C++/WinRT), line matching;
                      built as its own small library (cima_screen) in C++20, which C++/WinRT wants
    src/appaudio.*    one program's sound (Windows process loopback) and the program list
    src/osc.*         VRChat chatbox messages
    src/logo_data.h   the app picture as raw pixels
    src/audio.h       miniaudio include
    src/emotion.*     Carry my emotion: word and part delivery, audio tags, punctuation; voice gender guess
    src/micmatch.*    Match my mic: tone and level correction of the new voice
    src/vroverlay.*   SteamVR caption overlay and the CIMA Box tab in the SteamVR menu
    src/captionpanel.* the caption panel's drawing (CPU, also used for the preview)
    src/voiceout.*    ElevenLabs audio (PCM or MP3) -> 48 kHz playback; also compiles miniaudio
    src/hotkey.h      push-to-talk key
    src/app.h         settings and the data the interface shows
    src/ui.*          the interface: widgets, each page's settings, find, the setup
    src/ui3.inl       the 3.0 frame, included by ui.cpp: sidebar, page title bar, Home and its quick
                      toggles, My voice's layout, the mini window
    src/main.cpp      devices, threads, window, settings file
    assets/           icon.png, cimabox.ico, cimabox.rc.in (Windows icon and version details)
    lang/             _template.json.txt, copied next to the program (add app languages)
    tools/i18n/       interface translations and the scripts that check and build them
    src/cleansound.*  Listen: is there a voice in a phrase, and noise removal (a pack, like voiceid and soundid)
    src/usagecount.*  the Usage page's counts: every service's use by feature, per session and per day
                      (usage-history.json); list prices. src/usage.* reads what the accounts say
                      (credits left, ElevenLabs' last 7 days). The page itself is in src/ui3.inl.
                      For pictures of the page: CIMA_TEST_START_PAGE=7 and CIMA_TEST_USAGE="service,range"
    tools/quality/    translation quality test: lines run through the app's own translation code
                      against a model on this PC (see its README.txt); run it after changing the
                      AI instructions in src/translate.cpp
    tools/updates/    make-update-key.sh: the signing key for updates
    .github/workflows/release.yml   builds, signs and publishes a release for each version tag
