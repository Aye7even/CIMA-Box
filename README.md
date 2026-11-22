# CIMA Box

Change your voice, speak other languages like a native, and read what everyone around you is saying,
live, in any game or voice chat: Discord, VRChat, Valorant, Fortnite, Rec Room, you name it.
Powered by ElevenLabs.

**[Download CIMA Box for Windows](https://github.com/Aye7even/CIMA-Box/releases/latest/download/CIMABox.exe)**

That one file is the whole program. Put `CIMABox.exe` in a normal folder (like Documents or
Desktop) and run it. It updates itself from then on, and tells you what's new after each update.

![CIMA Box 3.0: the Home page](docs/home.png)

## What it does

**Home**: everything at a glance. Your setup in one sentence ("You speak English, they hear Japanese
in George"): click any underlined word to change it. What you just said, how fast people heard it,
shortcuts to the extras you use most (pick your own), what people around you are saying, and one
feature you haven't tried yet. **All voice settings** takes you to My voice for everything else.

**My voice**: your microphone, turned into any ElevenLabs voice (including a clone of your own).
- **Changer** keeps your own timing and emotion. The fastest and most natural mode.
- **Re-speak** has the voice say your words in its own delivery.
- **Translate** speaks your words in another language: 91 of them.
- With Claude, Gemini or OpenAI, translations sound like a real person talking, not a textbook: casual
  speech, slang and **dialects** (Mexican or Puerto Rican Spanish, Kuwaiti or Egyptian Arabic,
  Kansai Japanese, Swiss German, Hinglish and many more), and the right words for **who's speaking
  and who you're talking to** in languages that change with gender.
- **Carry my emotion** passes on how you said it: the words you stressed, pauses, laughs, whispers.
- Works anywhere that takes a microphone: games, Discord, Zoom, OBS and streams.
- In VRChat it can also show what you said, or the translation, in the chatbox above your avatar.

![My voice: microphone, voice and language, and where it goes](docs/my-voice.png)

**Text**: type instead of talking. The voice reads what you type, as typed or translated into
another language, and it can go in the VRChat chatbox too. What other people say (from Listen)
shows up translated in the same window, so you can follow the whole conversation without switching
pages. Handy when you can't or don't want to talk out loud.

![The Text page: type to talk, with the whole conversation translated](docs/text.png)

**Listen**: live captions of what other people say, translated into your language. It can listen to
just one program (your game, Discord...), everything on your speakers, or a microphone, and tells
apart who is speaking. Keep the captions in a small window on top of your game.

![The Listen page](docs/listen.png)

**VR**: those captions on a small see-through panel inside your headset, in any SteamVR game. Put it
in front of your eyes, on your wrist, or in a fixed spot. The CIMA Box tab in the SteamVR menu lets you
switch the voice changer, its mode, the language people hear and who you're talking to, turn Listen on and
off, resize or reset the captions, and stop a line, all without taking the headset off. If the headset panel
ever stops updating, it fixes itself (or press Reset VR captions).

![The VR page, with a preview of the caption panel](docs/vr.png)

**Extra features**: extras you turn on one at a time, so the app stays simple if you don't need them.
- **Screen translation**: pick a window, take a snapshot, drag over the text and press Translate.
  Or let it watch a chat box and translate new lines by itself. Reads on your PC for free, or with
  AI for any language.
- **Hear them in your ear**: a voice reads the translated captions in your headphones.
- **Captions while they talk**, **answer in their language**, a **see-through caption window**
  that clicks pass through, sound labels, high-contrast captions and muting one person.
- **Cancel before it's spoken**, **clean up your swearing** (tone down or bleep), **words for DeepL** (your own list of words that skip the AI), **Best
  translations**, and **Try a line** to compare styles.
- **Quick phrases**, **your own word list**, **learning mode** (how to read a line, replay, save),
  a searchable **history**, and **profiles** you can share with friends as a code (never with keys).

![The Extra features page: screen translation, extras for talking, and handy tools](docs/features.png)

**Mini window**: shrink CIMA Box to a small window that stays on top of your game, with the last
line, the mode, Stop and the switch. With Listen on, what other people say shows right under your
own line, translated, so the whole conversation is in one small window.

![The mini window](docs/mini.png)

The app itself speaks English, Español, Português, Français, Deutsch, Italiano, Bahasa Indonesia,
Русский, العربية, 日本語, 한국어 and 简体中文 (Settings, bottom left). Arabic, Persian, Urdu
and Hebrew are shown right to left with joined letters, in the app and in VR.

![Settings: app language, API keys, hotkeys, updates and service status](docs/settings.png)

## What you need

- **Windows 10 or 11.**
- **An [ElevenLabs](https://elevenlabs.io) account and API key.** CIMA Box uses your own account,
  so you pay ElevenLabs directly for what you use. Higher plans unlock better audio quality.
- **A virtual audio cable** so games and apps can hear the new voice, for example the free
  [VB-Audio Virtual Cable](https://vb-audio.com/Cable/).
- Optional, for natural translation: a [Claude](https://console.anthropic.com),
  [Gemini](https://aistudio.google.com) or [OpenAI](https://platform.openai.com) API key (a
  fraction of a cent per sentence), or [DeepL](https://www.deepl.com/pro-api).
  [Microsoft Translator](https://azure.microsoft.com/products/ai-services/ai-translator) works too,
  with a free Azure key.
  Or no key at all: **Local** runs a translation model on your own PC (free, private, works offline;
  uncensored Gemma 4 at 3.5 to 7.4 GB, best with a graphics card, or the uncensored Qwen 3.8 at
  13 to 21 GB for big graphics cards).
- For VR captions: SteamVR.

## Getting started

1. Run `CIMABox.exe`. The first time, a short setup walks you through everything. To do it by
   hand: click **Settings** (bottom left), paste your ElevenLabs API key under **Accounts** and
   click **Load voices and models**.
2. On **My voice**, pick your microphone (Microphone), a voice (Voice and language), and where the
   new voice goes (Where it goes): choose **CABLE Input** there.
   Then click **Check my setup** right there: a one-minute test that makes sure people will hear
   you, once, without crackling, and shows how to fix anything that's off.
3. In your game, Discord or any other app, choose **CABLE Output** as your microphone.
4. Flip the switch at the top right and talk.

To read what others say, open **Listen**, pick your game (or Discord, or any program) and the
language to translate into, and switch it on. Playing in VR? Turn on **VR** too, for captions inside
your headset.

Every button and setting is explained in **[`CIMA Box - Read Me.txt`](CIMA%20Box%20-%20Read%20Me.txt)**.

## Good to know

- **Put the program anywhere**, Desktop included. Settings save by themselves, and you choose
  where (Settings > About > Save settings in): your settings folder (the default), a
  `CIMA Box Config` folder in Documents, or right next to `CIMABox.exe` to keep everything in one
  folder.
- **Your keys stay on your PC.** They're saved in that settings file (`cimabox.json`) and only ever
  sent to the services they belong to. Don't share that file.
- **What's sent where:** your voice goes to ElevenLabs; text to translate goes to the translation
  service you picked. With Listen on, other people's voices are sent to ElevenLabs too, to caption
  them. Captions are never saved, unless you turn on Keep a history (a text file on your PC).
  Screen translation only captures the window you pick, when you ask; with AI reading that picture
  goes to the AI service, so never capture anything private.
- **Updates** are checked when the app starts and install with one click (Settings > Updates). Each
  one is signed, and the app refuses anything that isn't.
- **Your browser or Windows SmartScreen** may warn about the download or the first run ("Windows
  protected your PC"), because the program is new and not from a big company. In the browser choose
  **Keep**; in SmartScreen click **More info** > **Run anyway**.
- Keep CIMA Box in a folder you can write to (not Program Files) so it can update itself.
- **Make it yours:** Settings > Look changes the text size, the colors (four themes or your own),
  and can put your own picture behind everything.
- **Find any setting:** click the magnifier (or press Ctrl+F), type a word like "swear" or "key", and it
  takes you right there. On/off settings switch right in the list.
- **First time?** A short setup walks you through everything, starting with your language. Run it
  again from Settings > About.
- **Your keys, your costs.** CIMA Box is free and runs on your own accounts, so whatever it uses is
  charged to your keys. Switch off what you aren't using, set a spending limit (Settings >
  Accounts), and remember that friends using your key count on your account. The author isn't
  responsible for charges or used-up credits: you use it at your own risk.
- **What's left** of your ElevenLabs credits (and DeepL characters, or what Claude, Gemini or OpenAI cost
  this session) shows quietly at the bottom of the window.
- **Something stopped working?** Settings > Service status shows whether ElevenLabs, Claude,
  OpenAI or DeepL are having problems right now, and a small note appears at the top of the window when a
  service you use is. Then it's on their side: wait a bit and try again. Settings > About > Open
  log file shows what went wrong (it never contains your keys), so you can send it to whoever helps
  you.
- What changed in each version: [CHANGELOG.md](CHANGELOG.md).

## Powered by

CIMA Box is the glue. The voices, the words and the translations come from these, on your own keys
or your own PC:

| | What it does in CIMA Box |
|---|---|
| **[ElevenLabs](https://elevenlabs.io)** | Every voice you speak with, and writing down what you and others say |
| **[Claude](https://www.anthropic.com/claude)** (Anthropic), **[Gemini](https://ai.google.dev)** (Google), **[OpenAI](https://platform.openai.com)** | Translations that sound like a person: slang, dialects, tone |
| **[DeepL](https://www.deepl.com)**, **[Microsoft Azure Translator](https://azure.microsoft.com/products/ai-services/ai-translator)**, **[MyMemory](https://mymemory.translated.net)** | Fast, accurate wording, and the backup when an AI is slow (MyMemory needs no key) |
| **[Gemma 4](https://ai.google.dev/gemma)** (Google), **[Qwen 3.8](https://huggingface.co/Qwen)** (Alibaba) | The Local models that translate on your own PC, run by **[llama.cpp](https://github.com/ggml-org/llama.cpp)** |
| **[sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx)** with a WeSpeaker model | Voice fingerprints: telling who is who in a busy room |
| **[SteamVR](https://store.steampowered.com/app/250820/SteamVR/)** (OpenVR), **[Dear ImGui](https://github.com/ocornut/imgui)**, **[miniaudio](https://miniaud.io)**, **[curl](https://curl.se)** | Captions in your headset, the window, sound in and out, and talking to the internet |

CIMA Box is an independent, free project. It isn't made, sponsored or endorsed by any of these
companies; their names belong to them.

## Credits

A lot of this program was built with the help of Claude Fable and Claude Opus by Anthropic, so
shout out to them.

Building it yourself or publishing releases: see [DEVELOPING.md](DEVELOPING.md).
