# Changelog

What's new in each version of CIMA Box. This file is built into the app (Settings > Version
history, and the "What's new" window after an update) and becomes each GitHub release's notes.

To release a version, add a section for it at the top: `## 2.7.0`, then one line per change
starting with `- `. Write them for the people using the app.

## 2.8.2
- Push to talk can now leave your real voice on between presses (My voice > Input > While not holding the key > My real voice). Talk normally, and hold the key when you want a line translated or in the new voice. Your mic goes quiet from the press until that line has played, so people never hear both at once. Silent, the way it worked before, stays the default.

## 2.8.1
- Captions are tidier: voices no longer run up to "Voice 10". A voice gets a number only once it has been heard a few times (six at most), a line it can't place gets a plain bar instead of a guess, a voice is named once until the speaker changes, and the language and time show only when they change.
- VRChat chatbox on "Both": the translation on top with your own words under it in brackets, and when it's too long your words are shortened instead of the translation being cut off.
- Report a problem (Settings > About) now opens a new report on the GitHub page with your version and setup filled in. Say what happened, drag in the file it puts on your Desktop, and submit.

## 2.8.0
- OpenAI is now a translation choice next to Claude and Gemini (GPT-6 Luna: quick and cheap, and you can try it on OpenAI's free plan). The setup and Settings have a step-by-step guide with pictures, and Check key tells you if the key works or the account needs credit.
- Casual and Very casual really are casual now. They no longer translate your sentence word for word: they say what you mean the way you'd say it to a friend (or your closest friend), however neatly you said it. Polite forms are ruled out, slang is expected on Very casual, each common language gets told what casual speech looks like in it, and polite people around you no longer pull you back to formal. DeepL, as your service or as the backup, now also uses informal words (du, tu, tú...) on the casual styles and polite ones on Polite, in the languages where it can.
- New "My own" style: describe how you want to sound in your own words, like "laid back and a bit sarcastic, like with my best friend".
- Fewer lines falling back to DeepL when you use a dialect: dialect lines get a little more time, a line the AI answers wrongly or refuses is asked again once, and a moment of "too busy" is retried instead of giving up. Gemini no longer silently drops lines with swearing.
- Arabic, Persian, Urdu and Hebrew now display properly everywhere: letters join up and lines read right to left, in the app, the conversation, captions and the VR panel.
- CIMA Box in Arabic and Indonesian: pick them in the language step or in Settings > App language.
- A short, one-time note about costs after the setup (and once after this update): CIMA Box runs on your own keys, so keep an eye on what you leave switched on and set a spending limit. Read it again any time in Settings > About.
- Service status now shows DeepL and Gemini in the app too, instead of sending you to their websites.
- A calmer look: softer colors, fewer outlines and a little more room between things.
- New Features page (the tab next to VR). Everything below is there, sorted into Talking, Hearing and reading, and Handy. Each one is off until you turn it on.
- Screen translation: translate text in a game or app, like a chat box, signs or menus. Pick the window, take a snapshot, drag over the text in the preview and press Translate; the result shows with your captions and in the conversation. "Watch this area" keeps translating new lines in a chat box. In a game, a hotkey or the button in the SteamVR menu translates right away. The free Windows reader runs on your PC (the app tells you if a language is missing in Windows and opens the right settings page); AI reading handles any language but uses many more tokens. Read the notice first: never capture anything private.
- Hear them in my ear: a voice reads the translated captions in your headphones.
- Captions while they talk: words show up while someone is still speaking, then the translation replaces them.
- Answer in their language: when people speak to you in another language, your translations switch to it.
- Languages I speak / around me: "Detect automatically" only picks from these, so short phrases aren't mistaken for another language.
- Cancel before it's spoken: a short wait showing what the voice is about to say, with a Cancel button and key.
- Clean up my swearing: the voice tones down or bleeps your swear words. Captions can hide swearing too (f***).
- Best translations: bigger AI models and more time for the trickiest dialects and slang.
- Try a line: see how Polite, Casual, Very casual and My own say the same thing.
- My word list: names and words that keep coming out wrong, written your way and never translated.
- Captions: sound labels ([laughs], [music]), skip music and noise, big high-contrast text, the same color for the same voice, mute one person (right-click their line), and guessing gender from their words.
- A see-through caption window over the game that clicks pass through (borderless windowed games).
- Quick phrases on the Text page, plus the lines you type most often.
- Learning mode on the Text page: how to read a line (romaji, pinyin...), replay it in its language, and save lines to study.
- Keep a history: the conversation saved to a text file per day, searchable from the app.
- Profiles: save your setup and switch in one click. Share a setup with friends as a code (never your keys, word list or anything you typed).
- More hotkeys: hold for your real voice, and stop speaking.
- Settings > About > Report a problem puts a file on your Desktop with what's needed to find the problem, without keys or anything anyone said.

## 2.7.3
- The setup now walks you through getting every key, with pictures showing exactly where to click: ElevenLabs (required), then DeepL and Claude or Gemini (highly recommended, with what you'd miss without them). Each has a button that opens the right page and a Check key button that tells you if the key works, or what's wrong (like a Claude account with no credits yet).
- The same guides are in Settings > Accounts: click "How do I get this?" under any key. Click a picture to see it bigger.
- Getting the voice into a game or Discord is much clearer: a little drawing of how the virtual cable works, step-by-step install help for VB-CABLE, and where to pick it in Discord and in games.
- If the setup asks you to restart your PC (VB-CABLE does), it continues where you left off.

## 2.7.2
- CIMABox.exe is now the whole program: VR captions no longer need openvr_api.dll next to it, so downloading just the exe works for everything. (The old openvr_api.dll can be deleted.)
- Spending limit (Settings > Accounts): a notice when one session uses more ElevenLabs credits, or more money on Claude or Gemini, than you set. It can also switch off talking and listening until you press OK.
- Unplugged a headset or mic? CIMA Box notices and picks it up again when it's back, instead of going quiet.
- The log file says more when something goes wrong: which mic and outputs were opened (or couldn't be), your main settings at start, when another translation service had to step in, VR and Listen connecting, and when the last run didn't close normally. Still never any API keys or anything anyone said.

## 2.7.1
- A new setup walkthrough the first time you open CIMA Box: first choose your language, then connect ElevenLabs, pick what you want to do, pick a voice and where it should go, and play a test. It helps with VB-CABLE for games and Discord, and works without it too. Run it again any time from Settings > About.
- What's left, at the bottom of every page: your ElevenLabs credits and DeepL characters, straight from your accounts, and what Claude or Gemini cost in this session. Turns amber when you're running low and red when it's used up. Hover for the details.
- A log file (cimabox.log, in your settings folder) records errors and what happened, without any API keys. Settings > About > Open log file. Send it to whoever helps you when something goes wrong.

## 2.7.0
- New Text page: type instead of talking. The voice reads what you type, as typed or translated, with the same voices, languages, dialects and translation services as My voice, and it can show in the VRChat chatbox. Turn off "Say it out loud" to only send it to the chatbox.
- The Text page also shows the whole conversation: what other people say (from Listen), translated, next to what you said or typed, so you don't have to switch tabs.
- Service status: CIMA Box checks the status pages of ElevenLabs, Claude and DeepL every few minutes (Settings > Service status). When one you use is having problems, a small note appears at the top, so you know it's on their side and not the app. Click it to open their status page.

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
