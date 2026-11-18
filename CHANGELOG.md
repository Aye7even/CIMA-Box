# Changelog

What's new in each version of CIMA Box. This file is built into the app (Settings > Version
history, and the "What's new" window after an update) and becomes each GitHub release's notes.

To release a version, add a section for it at the top: `## 2.7.0`, then one line per change
starting with `- `. Write them for the people using the app.

## 2.6.1
- Choose where your settings are saved: your settings folder, a "CIMA Box Config" folder in Documents, or right next to CIMABox.exe so everything stays in one folder (Settings > About > Save settings in). Switching moves the file for you.
- A cimabox.json placed next to CIMABox.exe is used right there.
- "Loading voices" can no longer hang: it gives up after 20 seconds with a clear message, copes with unusual voices in your library, and ignores stray spaces or line breaks pasted along with an API key.

## 2.6.0
- Settings now save by themselves, a moment after you change something, into your own settings folder (%APPDATA%\CIMA Box). Moving or updating the program no longer touches them. Your old cimabox.json is copied over automatically.
- "What's new": after an update, a window shows what changed. Settings > Version history shows every version.
- One permanent download link for the newest version (see the GitHub page).

## 2.5.0
- Updates install themselves: an "Update to ..." button appears when a new version is out. Every update is signed, and anything without the right signature is refused.
- Claude no longer explains garbled or unclear speech instead of translating it. If an AI answer is an explanation anyway, that line goes through DeepL or Free instead.

## 2.4.0
- Dialects: Mexican, Puerto Rican, Argentine Spanish, Kuwaiti, Saudi, Egyptian, Levantine Arabic, Swiss German, Kansai Japanese, Hinglish and many more, or type your own (Claude or Gemini).
- Gender: say whether you're a man, a woman or nonbinary, and who you're talking to (a guy, a girl, a group), so languages that change words for it come out right. Auto guesses from the other person's voice.
- Switch who you're talking to with a hotkey, or in VR from the CIMA Box tab in the SteamVR menu.
- Volume sliders for the new voice and for your headphones.
- Settings (the gear): the app in 10 languages, all your API keys in one place, hotkeys.
- Friendlier layout, with first steps for new users. Push-to-talk reacts instantly even while a game has focus.

## 2.3.0
- Claude and Gemini translation: says it the way a native speaker would, slang and casual speech included. Polite, Casual or Very casual, and "Sound like..." for region and age.
- VR captions: a see-through panel in your headset in any SteamVR game, on your head, hand or fixed in the room, with your own colours and size.
- The window uses almost nothing while minimized or in the background.

## 2.2.0
- All 91 languages of Eleven v4, with search in every language picker.

## 2.1.0
- Audio quality setting: Standard, High (like the website) or Best.

## 2.0.0
- Carry my emotion now follows each word and part of a sentence: stressed words, drawn-out words, pauses, laughs, whispers, shouts.
- Show what you said, what was spoken, or both in the VRChat chatbox.
- Match my mic, pass your real mic through when off, and clone strength for professional voice clones.
- New name: CIMA Box, with its own icon.
