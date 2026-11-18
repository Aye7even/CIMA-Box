# CIMA Box 2.5 (C++)

Three tools in one window:

- **My voice** - microphone -> ElevenLabs -> any output device. Point the output at a virtual cable
  and Discord / OBS / games hear the new voice.
- **Listen** - captions for what other people say, translated into your language. It listens to one
  program (for example VRChat), to everything on a playback device, or to an input device.
- **VR** - those captions (and optionally your own words) on a floating panel inside your headset,
  in any SteamVR game.

The gear at the top right opens **Settings**: the app's own language, your API keys, and hotkeys.

## Build

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

Coming from an earlier version: build this in its own folder, then copy your old `cimabox.json` (or
`voicechanger.json` from the Voice Changer) next to the new program to keep your key and settings.

## The app icon

The picture in `assets/icon.png` is built into the program: the window and taskbar icon, the logo
next to the name, and on Windows the icon of the `.exe` file itself (Explorer, shortcuts). Nothing
needs to sit next to the program.

To change it later, replace `assets/icon.png` and `assets/cimabox.ico` and regenerate
`src/logo_data.h` (raw pixels of the picture at 128, 64, 48, 32 and 16 pixels), then rebuild.

If you send the program to someone, send `CIMABox.exe` only. Don't send `cimabox.json`: it holds
your API keys.

## My voice

The page follows the sound from left to right: **Input**, **Voice**, **Output**.

1. Open **Settings** (the gear, top right), paste your ElevenLabs API key and load your voices. Until
   then, the strip at the bottom shows these first steps.
2. Under **Voice**, pick a mode:
   - **Changer** - speech-to-speech. Keeps your timing, emotion and intonation. Quickest.
   - **Re-speak** - writes down what you said (Scribe v2), then a text-to-speech model such as
     v3, v4 or Flash reads it. The delivery is the model's, not yours.
   - **Translate** - like Re-speak, but the text is translated first. Pick the language you
     speak and the language to translate to.
3. Under **Input**, pick the microphone and how you start talking: **Voice activation** (move the
   marker just right of where the meter sits when you are silent) or **Push to talk**.
4. Under **Output**, pick where the new voice goes. To feed another app, install a virtual cable
   (VB-Cable on Windows, BlackHole on macOS), choose it here and as the microphone in the other
   app. Turn on **Hear it yourself too** to listen on your headphones. Each has its own **Volume**
   (0 to 200 %); it changes only the new voice, not your passed-through mic.
5. Stability, similarity, style, speed and the rest are under **Fine-tune the voice** (folded away;
   the defaults suit most voices).

### Clone strength (professional voice clones on Eleven v4)

With a Professional Voice Clone selected and an Eleven v4 model, **Set clone strength** shows a
slider: higher follows your training recordings more closely, lower smooths the voice and gives it
more room to act. ElevenLabs suggests 0 for most voices and 0.5 for strong accents or unusual voices.
Switched off, the value saved with the voice on ElevenLabs is used. Other models don't take it, so
it is only sent with v4.

### Audio quality (under Output)

| | What ElevenLabs sends | Plan | Changer gets your voice at |
|---|---|---|---|
| **Standard** | 24 kHz PCM | any | 16 kHz |
| **High** (default) | 44.1 kHz MP3, 192 kbps (what the website plays) | Creator and up | 48 kHz |
| **Best** | 44.1 kHz uncompressed | Pro and up | 48 kHz |

Standard cuts everything above 12 kHz, which is why it sounds a little duller than the website. In
Changer mode the input matters too: at 16 kHz the voice changer never hears anything of your voice
above 8 kHz, so High and Best upload your voice at full quality. That is three times the upload (about
96 kB per second of speech instead of 32 kB), usually a few tenths of a second more on a slow
connection, nothing on a fast one. The MP3 is decoded as it streams in (it starts after the first
~26 ms frame), and everything is converted to 48 kHz with a windowed-sinc resampler that keeps the
full top end.

If your plan refuses a quality, CIMA Box steps down by itself (Best -> High -> High at 128 kbps ->
Standard), says so under the setting, and tries again before anything plays. Picking a quality again
retries it.

Match my mic can undo some of this: if your microphone itself sounds dull, matching gives the voice
the same dullness. Lower its Amount or switch it off when you want the cleanest sound.

### Your own mic, and matching it

- **Pass my mic through when off** (under Output): when you switch the voice changer off with the
  switch at the top right, your real voice goes to the output device instead of silence. Discord or
  VRChat can then stay on the virtual cable all the time, and you switch between your voice and the
  new one in CIMA Box. The header shows "Your mic" while it is passing through.
- **Match my mic** (under Output): gives the new voice the tone and loudness of the microphone you're
  actually using, so it sounds like it came through the same mic as your real voice: when you switch
  between passthrough and the new voice, or between your VR mic and your desk mic. It listens to your
  mic while you talk and to the new voice as it plays, about 4 seconds of each, then corrects the new
  voice by the difference (7 bands from 150 Hz to 8 kHz, at most 8 dB each, plus loudness). It only
  reshapes what's there: no echo, no hiss. It relearns by itself when you pick another mic, or with
  **Relearn my mic**. **Amount** sets how far it goes. No added delay.

### VRChat chatbox

**Show in VRChat chatbox** (under Output) puts your words above your avatar through OSC (turn OSC on in
VRChat first). Choose **What I said**, **Spoken as** (the translation, in Translate mode) or **Both**.
Tags like [excited] are left out. In Changer mode nothing is written down, so with the chatbox on each
phrase is also transcribed, at the same time as the voice conversion (no added delay, but it is billed
as speech-to-text). VRChat mutes the chatbox when it is written to too often, so at most one message is
sent every 1.6 seconds. If the Listen page also sends captions to the chatbox, the two take turns.

### Carry my emotion (Re-speak and Translate)

Re-speak and Translate only send words, so the voice reads them in its own tone. With **Carry my
emotion** on, CIMA Box measures how you said each word and each part of a sentence, using the word
timings ElevenLabs' speech-to-text returns, and writes that into the text the voice reads:

- **Stressed words** - a word clearly louder and/or higher than the words around it is written in
  CAPITALS, which v3 and v4 read as emphasis. The natural fall of a voice over a sentence is taken
  out first, so the first word isn't mistaken for stress. At most one word in five.
- **Drawn-out words** - a word held much longer than your usual pace gets its vowel repeated ("nooo").
- **Pauses** become "," or "...".
- **Each part separately** - a phrase is split where you paused or a sentence ended, and each part gets
  its own delivery: [excited], [shouts], [whispers], [softly] or [flatly] (monotone), or nothing when it
  sounds like your usual voice. Example: "no way... you actually did that again", the first half
  excited, "actually" stressed and the rest flat, becomes
  `[excited] Nooo way! [flatly] You ACTUALLY did that again.`
- **Questions** - your voice rising over the last word gives "?" even when the words don't ask.
- **Laughs, sighs, gasps** become [laughs] etc. where they happened.

It judges against your usual voice, learned from your first few phrases (talk normally for about
three after turning it on) and kept up to date from ordinary phrases. **Relearn my usual voice** starts
over, for example after changing microphones. Volume alone never counts: moving closer to the mic
doesn't make you "excited". Shouting also needs a pushed, brighter voice; excitement needs your pitch to
go up.

- **Translate:** stressed and drawn-out words can't be carried into another language (the words
  change). Each part's delivery, questions and laughs are kept. When the parts sound different, each
  is translated on its own, at the same time, so each keeps its own tag.
- **Models:** tags and capitals need Eleven v3 or v4 (v4 Turbo is the quickest). Other models would
  read brackets aloud and spell out capitals, so with them only pauses, questions and drawn-out words
  carry over.
- **Subtle** only picks up clear changes; **Strong** picks up smaller ones too.
- **Delay:** the measuring runs on your computer while the phrase is being transcribed: no added wait.
- **Limits:** it tells how you said something, not what you felt: happy and angry yelling both come out
  as excited or shouts.

The strip at the bottom shows what was heard, what was spoken, and how long each step took. With
Carry my emotion on, it also shows how the phrase sounded and the exact line sent to the voice.
**Stop speaking** cancels whatever is queued or playing.

## Listen

Switch to **Listen** at the top, choose what to listen to, and turn it on with the switch at the top
right. It always starts switched off, so it never transcribes (and bills) without you asking.

**Listen to**

- **App** (Windows 11, or an up-to-date Windows 10) - only that program's sound. Pick it from the
  list while it is running, or type its file name (`VRChat.exe`). If the program isn't running yet,
  CIMA Box waits for it, and reconnects when it restarts. Your own changed voice is never captioned.
- **Speakers** (Windows) - everything playing on one playback device: every program, and the new
  voice too if you hear it on that device.
- **Input** - a capture device, such as a virtual cable or a second microphone. Works on every system.

Set **Opens above** so the marker sits above the game's background sound and below the voices.
Constant music louder than the marker is treated as speech: it gets sent for transcription and may
show up as nonsense captions.

**Captions appear** trades speed for wording: *Sooner* cuts at short pauses (quickest, but half
sentences translate worse), *Fuller* waits for whole sentences.

**Translation** - leave **They speak** on "Detect automatically" unless everyone speaks one language
(naming it is more accurate). Speech already in your language is shown as it is, or hidden with
**Skip speech in my language**. The translation service is the same setting the My voice page uses.

**Several people talking** - with **Tell speakers apart** on, a caption that holds more than one
voice is split into "Voice 1", "Voice 2" lines. Those numbers only mean something inside that one
caption: Voice 1 in the next caption can be someone else. A game mixes everybody into one stream,
so people talking over each other at the same moment come out less accurate than people taking turns.
Phrases are transcribed two at a time and always shown in the order they were spoken. If captions
fall more than three phrases behind, the oldest waiting ones are dropped to stay live.

**Reading the captions in VR**

- **Captions only** shrinks the window to just the feed and keeps it on top of other windows. Pin
  that window inside VR with a desktop-overlay tool (XSOverlay and OVR Toolkit both do this), or
  keep it over a desktop game running in borderless-window mode.
- **Send to VRChat chatbox** shows the newest caption above your avatar through VRChat's OSC input
  (port 9000 on this computer). People near you can read it too. Turn OSC on in VRChat first:
  Action Menu -> Options -> OSC -> Enabled. VRChat limits the chatbox to 144 characters and mutes it
  when written to too often, so at most one caption is sent every 1.6 seconds.

CIMA Box does not draw its own overlay inside the headset.

## Languages

The language pickers (My voice and Listen) list all 91 languages Eleven v4 speaks, with a search
box: type the start of a name, another name ("Farsi", "Tagalog", "Mandarin") or the code ("ja").

- **The voice:** Eleven v3 and v4 speak all of them. Multilingual v2, Flash v2.5 and Turbo v2.5 speak
  32; with one of those models picked, the other languages are marked "v3 / v4 only".
- **Speech-to-text** (what you or others say) recognises all of them.
- **Translation:** DeepL covers many but not all. When DeepL refuses a language, that phrase goes to
  the free service instead, automatically. That needs the spoken language known (picked, or detected).
  The free service's quality varies a lot for less common languages.
- With v4, the app also tells the voice which language the text is in, for better pronunciation.

## Dialects

Under **Translate to** there is a **Dialect** picker: Mexico, Spain, Argentina, Puerto Rico,
Colombia... for Spanish; Kuwait, Saudi Arabia, Gulf, Egypt, Levant, Morocco... for Arabic; Brazil or
Portugal; Quebec; Austria, Swiss Standard German, Swiss German (Schweizerdeutsch); Kansai;
Gyeongsang; Hinglish; Taglish; and more (60 in all). **Other (type it)** takes anything else, such as
`ar-KW` or "Rioplatense with lunfardo".

- **The words** come from Claude or Gemini, told to speak that variety with its own everyday words and
  grammar, written the way people from there type it.
- **The accent** comes from the voice. Kuwaiti words read by an American-sounding voice still sound
  off: pick (or clone, with permission) a voice from that region. **A voice for each language** brings
  back the voice you last used with each language and dialect, so switching is one click.
- **DeepL and Free** only know the standard language, plus DeepL's few regional spellings (Latin
  American Spanish, European / Brazilian Portuguese, British / American English, Canadian French,
  Swiss Standard German, simplified / traditional Chinese), which are used when they match. When a
  phrase falls back to them, the status strip says so, e.g. "DeepL (Claude too slow; standard Arabic)".

## Gender: who is speaking, who is listening

Spanish, Arabic, Hebrew, Russian, Polish, Hindi, French, Portuguese and many others change words for
the speaker's gender (cansado / cansada) and, in some, for the listener's (Arabic enta / enti). With
Claude or Gemini:

- **I am:** Not set / A man / A woman / Nonbinary.
- **Talking to:** Auto / Don't know / A guy / A girl / A group. **Auto** goes by the last person the
  Listen page heard, from the pitch of their voice (under ~150 Hz a guy, over ~178 Hz a girl, in
  between "can't tell"). It's a guess: voice changers and avatar voices fool it. "Don't know" makes the
  AI prefer wording that fits anyone.
- Switch it fast: a **hotkey** (Settings > Hotkeys; global on Windows, each press goes to the next
  choice), or in VR the **CIMA Box tab in the SteamVR menu** (see below).
- Listen also uses the voice guess when translating others into a gendered language.

## Translation services

| | Wording | Needs |
|---|---|---|
| **Free** (MyMemory) | rough, textbook | nothing; about 5,000 characters a day |
| **DeepL** | accurate, textbook | a DeepL API key (free plan: 500,000 characters a month) |
| **Claude** | natural, slang-aware | a Claude API key from console.anthropic.com |
| **Gemini** | natural, slang-aware | a Gemini API key from aistudio.google.com |

DeepL and Free translate like a textbook, which native speakers notice right away: no casual speech
level (Korean 반말, Japanese タメ口, tu/tú/du/ты), no slang. **Claude** and **Gemini** are AI models told
to say your line the way a native speaker would actually say it out loud:

- **How you talk:** Polite (as with someone you just met), Casual (friends; the default) or Very casual
  (close friends gaming: more slang, swearing kept).
- **Sound like...:** optional, e.g. "a guy in his 20s from Seoul" or "a girl from Mexico City".
  Region and age change the slang a lot.
- **Match how they talk to me:** the AI also sees the last few lines from the Listen captions, so you
  answer at the level the others use with you and pick up their words. Needs Listen on.
- Written for speaking: no emoji, no "ㅋㅋ"/"lol"/"www", nothing added or censored.
- **Time limit** (default 2 s): if the AI hasn't answered by then, that phrase is translated by DeepL
  (if you added a key there too) or Free instead, so you're never left hanging. The status strip shows
  which service did each phrase.
- **AI model:** claude-haiku-5-5 and gemini-3.5-flash-lite are the fastest (the defaults). The bigger
  claude-sonnet-5-5 or gemini-3.6-flash word things a little better but are slower.
- On **Listen**, the same service translates what others say, so their slang is understood. It always
  translates faithfully there (the speech level setting is for your voice only).
- Cost: each phrase is a few hundred tokens, a tiny fraction of a cent on either service.

With an AI service, a phrase is translated in one go (Carry my emotion still sets one delivery for it),
because the AI words a whole sentence more naturally than separate parts.

Either way the text of what is said is sent to that service.

## VR captions (SteamVR)

The **VR** page puts the captions on a floating, see-through panel inside your headset, over any
SteamVR game, VRChat included. It uses SteamVR's own overlay system (the one XSOverlay and OVR Toolkit
use), so nothing else is needed. Off until you switch it on at the top right.

- **Where:** Head (moves with you, always in view; the default), Left hand / Right hand (on the back of
  the hand: look at your wrist), or Fixed (stays in one spot in the room: press "Put it in front of
  me"). Distance, height, sideways and size for Head and Fixed; size for hands.
- **Show:** what others say (from the Listen page, so turn Listen on), optionally what you say, and
  optionally the original words under translations. **Hide after** fades the panel out after a quiet
  while (or never).
- **Look:** text size, number of lines, panel opacity (0 = just floating text), overall opacity,
  glow, and colours for the text, the edge/glow and your own lines. The preview shows exactly what the
  headset will show.
- **Who you're talking to, in VR:** with **CIMA Box tab in the SteamVR menu** on, press the system
  button: the SteamVR menu has a CIMA Box tab with big buttons (Auto, Don't know, A guy, A girl,
  A group) to point and click with the laser. **Show who I'm talking to** adds a small note on the
  caption panel ("Talking to: a girl").
- **Light on resources:** the panel is only redrawn when the words or the look change (a few
  milliseconds), it is hidden completely when there's nothing to show, and the background thread just
  checks for changes 20 times a second. It never starts SteamVR; it waits for it, and lets go when
  SteamVR closes.
- Needs `openvr_api.dll` next to `CIMABox.exe`. The build downloads Valve's OpenVR SDK (header, import
  library and that DLL) and copies the DLL there. The program still starts without it; only VR captions
  won't. Build with `-DCIMA_VR=OFF` to leave VR out.

The main window also saves resources: minimized, it draws nothing; in the background, it draws at most
20 frames a second.

## App language

Settings > **App language**: English, Español, Português (Brasil), Français, Deutsch, Italiano,
Русский, 日本語, 한국어, 简体中文. Changes right away. Only the menus change; translations, captions and
voices have their own language settings.

**Adding a language** without rebuilding: the build puts a `lang` folder next to the program with
`_template.json.txt` (every English text). Copy it to, say, `lang/tr.json`, set `"code"` (e.g. `"tr"`)
and `"name"` (`"Türkçe"`), fill in the translations (anything left empty stays English) and restart.
The same kind of file with the code of a built-in language (`"ja"`) changes some of its texts. A
translation whose `%` codes don't match the English is ignored (the English shows), so a typo can't
crash the program.

Arabic, Hebrew, Persian and Hindi aren't offered as app languages: the text renderer can't join and
order those scripts properly.

Developers: the built-in tables are in `tools/i18n/` (one JSON per language). After changing the
interface, run `python tools/i18n/extract.py` then `python tools/i18n/build.py`; it reports missing
texts and broken `%` codes, and writes `src/i18n_data.h`.

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

1. Set the version in `CMakeLists.txt`: `project(CIMABox VERSION 2.6.0 ...)`. Use three numbers.
2. Commit, tag the same version with a `v`, and push both:

        git commit -am "CIMA Box 2.6.0"
        git tag v2.6.0
        git push origin main --tags

3. Watch it under the repository's **Actions** tab (5 to 10 minutes). When it's green, the release is
   on the Releases page, and apps pick it up the next time they start (or with Check for updates).
   If the tag and the version in `CMakeLists.txt` differ, it stops and says so.

Your friends install CIMA Box 2.5 from the release zip once, by hand; after that it updates itself.
Builds you make on your own PC have no updater unless you configure them with
`-DCIMA_UPDATE_REPO=YOUR-NAME/YOUR-REPO` (and `update_key.txt` present).

Notes:
- Only `CIMABox.exe` is replaced. If a release ever needs a new `openvr_api.dll` (rare), say so in
  the release notes so people take it from the zip.
- The program must be in a folder its user can write to (not `Program Files`), or the update can't
  replace it. It says so if that's the case.
- Windows SmartScreen may warn about the zip the first time ("unrecognized app"): More info > Run
  anyway. Updates installed from inside the app don't go through that.
- GitHub allows 60 update checks an hour per internet connection without logging in, far more than
  one check per start needs.

## Things to know

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
- **Text in some scripts** (Arabic, Persian, Urdu, Hebrew, Hindi and other Indian scripts, Burmese) can
  look wrong in the app's text lines: letters show, but aren't joined or ordered the way they should
  be (the text renderer doesn't do that). The voice and the chatbox are unaffected.
- **Settings** are saved to `cimabox.json` in the folder the app runs from, on exit or with
  Save settings. That file holds your API keys in plain text - don't share it.

## Layout

    src/core.h        ring buffer, phrase segmenter (voice activation / push to talk), WAV writer
    src/http.*        libcurl wrapper that keeps connections open
    src/eleven.*      ElevenLabs REST client
    src/translate.*   MyMemory, DeepL, Claude and Gemini translation; languages and dialects
    src/i18n.*        the app's own language (src/i18n_data.h is generated by tools/i18n/build.py)
    src/updater.*     updates from GitHub Releases, with signature check
    src/listen.*      Listen pipeline: sources, phrases, workers, caption feed
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
    src/ui.*          the interface
    src/main.cpp      devices, threads, window, settings file
    assets/           icon.png, cimabox.ico, cimabox.rc.in (Windows icon and version details)
    lang/             _template.json.txt, copied next to the program (add app languages)
    tools/i18n/       interface translations and the scripts that check and build them
    tools/updates/    make-update-key.sh: the signing key for updates
    .github/workflows/release.yml   builds, signs and publishes a release for each version tag
