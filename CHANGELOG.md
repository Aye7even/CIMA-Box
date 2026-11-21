# Changelog

What's new in each version of CIMA Box. This file is built into the app (Settings > Version
history, and the "What's new" window after an update) and becomes each GitHub release's notes.

To release a version, add a section for it at the top: `## 2.7.0`, then one line per change
starting with `- `. Write them for the people using the app.

## 3.0.3
- Fixed: VR captions that stopped showing new lines after a while (while the app still showed them). CIMA Box now checks every picture it hands SteamVR and, every few seconds, that the panel is still there and really showing. When SteamVR drops, hides or refuses it, the panel is rebuilt (or SteamVR reconnected) by itself, and what happened goes in the log.
- New: Reset VR captions, on the VR page and in the SteamVR menu: starts the headset side over and redraws everything.
- The SteamVR menu's CIMA Box tab does a lot more, so you don't have to take the headset off: the voice changer on/off and its mode, the language people hear (from the ones you used last) and Swap, who you're talking to, Listen and Hear them in my ear on/off, caption size, In front of me, Reset, Stop speaking, Cancel and Translate screen. The title shows what it's doing, and every press shows a short note.
- Carry my emotion is much more accurate, especially for people it used to get wrong:
  - It compares you with your own usual voice AND how much it normally varies, so an expressive speaker's normal ups and downs no longer read as excited, and a calm speaker's real excitement still does.
  - It learns your usual voice from more speech and keeps learning, so a few quiet or odd phrases at the start correct themselves (before, they could make normal talking read as excited for good).
  - Pitch only counts when it's read steadily: half-whispers and breathy speech now come out as whispers, not excited.
  - Your mic's loudness and brightness are kept apart from your voice: switching mics relearns only the mic part, and leaning into the mic is never an emotion. Fixed: the voice's brightness wasn't saved, so after a restart it was compared with a made-up value.
  - New: hesitant ("um", "uh"), rushed, slow and sad (only when lower, narrower, slower and quieter all at once). Speed and hesitation come from timing and words, so they work the same on any mic.
  - The usual voice saved by older versions is learned again (it takes a few phrases).
- Translations through Claude, Gemini and OpenAI fall back far less, in every language:
  - Each language gets the time it needs. Filipino (and other languages whose lines take longer to write out) said "too slow" on almost every line, because the time limit was the same for every language. Filipino and Cebuano now start with a little extra, and any language that runs out of time gets a bit more by itself (never more than double your limit), easing back when it answers quickly.
  - With nothing to fall back on (no DeepL key, and Free used up for the day or unable to tell the language), the AI is no longer cut off at the time limit and the line lost: it's waited for.
  - Too many requests for your plan (common on free plans, especially with Listen and My voice both on): CIMA Box stops sending second copies of slow requests and doesn't ask again right away, for every part of the app at once, instead of making it worse.
  - Out of credit, or the key refused: every part of the app goes straight to DeepL or Free for a few minutes, saying why, instead of waiting on the AI for each line. It tries the AI again after that, or as soon as you paste a key.
  - Clearer messages: "out of credit", "too many requests for the plan's limit" and "the plan's daily limit is used up" are told apart (before, all of them said "too many requests or out of credit").
  - Free works with the spoken language on automatic: when nothing reported the language, CIMA Box guesses it from the words, instead of "The free service needs to know the spoken language".
  - Once Free says its daily limit is reached, it isn't asked again until the next day (each ask only added a wait).
- When the AI holds back on a line (a word with a second meaning can do that, even when it was meant innocently), the backup starts on it right away, and the bottom strip says "declined this line" instead of "too slow". Those lines also no longer make the app think that language is slow.
- New: a second AI as the backup. With keys for two AIs (say Claude and Gemini), the other one translates the lines the chosen one says no to, is out of credit for, or fails on, instead of Free. It never steps in on lines that are only slow, so no line is paid for at two AIs. "Use my other AI key as a backup" under Translation service (on by default).
- New: Microsoft Translator as an optional free backup (Settings > Accounts): 2 million characters a month and 100+ languages. Used after DeepL, and for languages DeepL doesn't have. Needs a Microsoft Azure account, so it's never required.
- New: go back to an earlier version (Settings > Updates > Go back to an earlier version), in case an update breaks something for you. It lists the recent versions that can still update themselves, so you can always come forward again, and installs the one you pick the same way as an update (signature checked, settings and keys kept). From this version on, the version you went back from isn't offered again until a newer one is out. Settings an earlier version doesn't know are kept aside and put back when you update again.
- New: while Listen is on, the top of every page says what it's listening to ("Listening to VRChat", or your speakers or input), glows while someone is talking, says "Waiting for VRChat" until the game is open, and turns red if Listen has a problem. Click it to open the Listen page.
- Fixed: DeepL never translated into or from Filipino. DeepL calls it Tagalog ("TL"), and CIMA Box asked for "FIL", which DeepL doesn't know, so every Filipino line went to Free instead (or nowhere). Languages that only DeepL's newer model does (like Tagalog and Cebuano) are now asked of that model automatically.
- DeepL as the backup is sturdier: a spoken language it can't read is left for DeepL to work out (before, that sent the line to Free); only a language DeepL can't translate into sends a line to Free; any other DeepL problem is shown instead of hidden behind Free's message; and a key that DeepL refuses at one address is tried at the other (free and Pro keys go to different places).
- Fixed: the key box under Translation service was shared by all services, so a key being pasted while switching services could end up under the wrong one.
- Taglish and Hinglish read better and come back faster: the AI was told both to mix in English and to never use English, and stopped to work out which one to follow.
- The log no longer repeats "ElevenLabs usage: HTTP 401" every 30 seconds when the ElevenLabs key isn't allowed to read the account's credits: it says it once, and everything else keeps working. The first line of the log also lists which keys are filled in (never the keys themselves) and the AI time limit.

## 3.0.2
- New: Check my setup (My voice > Where it goes, or a quick toggle on Home). A one-minute test that walks you through it: your mic hears you, the voice goes to a cable, both ends of the cable run at the same rate, a quiet test beep goes in and comes out, nothing else sends your mic into the cable (why people sometimes hear you twice), push to talk works, and your ElevenLabs key loads your voices. Anything wrong comes with a button to the fix.
- New: push to talk with a VR controller button (any SteamVR headset). Turn on "A VR controller button too" under push to talk and pick the button in SteamVR's controller bindings. It buzzes when recording starts and stops, and works with the VR captions off.
- New: tap to talk. Under push to talk, "Tap to start and stop": tap once to start recording and again to stop, instead of holding. Great for long stories or a VR button.
- New: hear a voice before picking it. The play button next to each voice plays its sample in your headphones, for free, and nobody in your call hears it.
- When a hotkey or the SteamVR menu changes something, a short note says so at the bottom of the window ("Talking to: A girl", "Stopped speaking"), also in the mini window and the caption window over your game.
- Problems come with a button that fixes them: "Pick a microphone", "Open Accounts", "Look for devices again" and so on, on Home, in the Last line card and in the mini window.
- My word list and Share a setup are easier to find: Home's "New to you" suggests them, and they can be quick toggles.
- Your real voice no longer crackles going through VB-Cable (or any virtual cable). When CIMA Box passes your own mic through (with the switch off and "Pass my mic through" on, the "my real voice" key, or push to talk set to "My real voice"), it now keeps 50 ms of sound ready before playing, so a block that arrives a little late is no longer a gap.
- The mic and the cable each keep their own time and slowly drift apart. CIMA Box now evens that out one sample at a time, which you can't hear, instead of skipping 10 ms at once, which clicked.
- Sound moves through the app more evenly: Windows' timer is set to 1 ms while CIMA Box runs (it rounds short waits up to about 15 ms otherwise).
- Messages and tips that still pointed to the old layout ("under Input", "bottom of the window") point to the right places.

## 3.0.1
- Home shows which service translated each line again ("Translated by Claude", or Gemini, OpenAI or DeepL), next to how long it took. 3.0.0 only said "Translated".

## 3.0.0
- A new look. The whole window is redesigned to be easier to find your way around, without taking anything away.
  - The pages are now down the left side, and each one says under its name what's on it ("Voice, language, dialect", "Captions of others"...). A dot next to a page means that part is on.
  - Settings, the Mini window button and what's left on your accounts are at the bottom left.
  - Each page has its title at the top, a "Find a setting or feature" box (Ctrl+F still works), and the switch with what it's doing right now.
- New: Home, where CIMA Box now opens. Everything at a glance:
  - Your setup in one sentence: "You speak English, they hear Japanese in George." Click any underlined word to change it, and the line under it shows where the voice comes out.
  - "All voice settings" goes to My voice for everything else: dialect, how you talk, the translation service, the model, the mic and more.
  - Your mic level, the mode, what you just said and what they heard, and how long it took from your last word to their ears (hearing, translating and the voice, each in its own colour).
  - Quick toggles: shortcuts to the extras you use most. Click a tile to switch it on or off, or the arrow for its full settings. "Edit tiles" picks which ones show (up to 12).
  - Around you: the last few captions from Listen. New to you: one feature you haven't tried yet, with a button that takes you to it.
- New: the Mini window. A small window that stays on top of your game with the last line, the mode, Stop and the switch. "Full window" brings everything back.
- My voice is now three sections from top to bottom: Microphone, Voice and language, and Where it goes. Each shows the everyday settings, with "More ... settings" at the bottom for the rest (it stays open if you leave it open). The last line and "Try a line" are on the right.
- The first-time setup now ends on Home. Until you've added your ElevenLabs key, Home shows a "Start the setup" button.
- All of it is translated into all 11 app languages, and Arabic lays out right to left on Home too.
- The Read Me and the pictures on GitHub show the new look.
- New: Live transcription (My voice > Microphone; Translate and Re-speak; off to start, so try it out first). Your voice goes to ElevenLabs while you talk instead of being uploaded when you stop, so your words are ready almost the moment you finish, and "You said" fills in as you speak. The timing under the last line says "transcribe (live)" when it was used.
  - It keeps everything else the same: CIMA Box still decides where a phrase ends (and keeps sentences whole), so only the waiting for the transcript goes away.
  - Built to fall back: if the live connection has any problem (it couldn't connect, it dropped, it was slow to answer, the key or credits were refused), that phrase is uploaded the regular way, so nothing you say is lost. After a refusal it uses the regular way for a while by itself, and the log says why.
  - It only sends audio while you're talking, and closes the connection when you've been quiet for a bit. ElevenLabs bills it per second of audio sent, at their live speech-to-text rate.
- Sentences stay whole. A pause in the middle of a sentence (to breathe, or after "and", "because", a comma) used to send half a sentence on its own, and translated alone it came out with broken grammar. Now, in Translate and Re-speak, CIMA Box looks at what you said: if the sentence sounds unfinished, it waits a moment for you to go on, then translates and says the whole sentence at once. A sentence you finished is never kept waiting, so short replies are just as quick. While it waits, the top right says "Waiting for more".
  - New setting: My voice > Microphone > "Wait for the rest of a sentence" (900 ms to start). Raise it if you often stop to think mid-sentence, lower it if it waits when you were done, or set it to 0 to turn it off.
  - It knows how sentences continue in many languages (English, Spanish, Portuguese, French, German, Italian, Dutch, Indonesian, Malay, Filipino, Russian, Ukrainian, Polish, Turkish, Arabic, Hindi, Vietnamese, Japanese, Korean and Chinese), and the rest of your sentence is only transcribed once, so waiting costs nothing extra.
  - It never waits forever: after a few pieces, or 40 seconds, what you said goes out as it is. With push to talk you decide where a line ends, so it only joins pieces while you're still holding the key.
- Stories flow. When you talk for a long time, the sentences you've finished are said while you keep talking, and only the one you're in the middle of waits for its end, so people hear your story about a sentence behind you instead of all at once at the end. A phrase that runs too long is no longer cut mid-sentence.
  - Each sentence is translated knowing the one or two you said just before (the AI services, and DeepL through its own context option), so "he", "she" and "it" stay right, the same things keep the same words, and the style stays the same across the story.
- Find a setting now also finds "Pause that ends a phrase", "Wait for the rest of a sentence" and "Longest phrase", and lights them up like the others.
- Faster, and the AI does more of the translating. A slow moment no longer means waiting or a switch to DeepL:
  - A second copy of a slow request. If the AI is clearly later than it usually is (it learns its usual time as you talk), the same request goes out again on a second connection, and whichever answers first is used. Same AI, same model, same instructions, so the translation is no different. A request that gets stuck at the service now usually comes back from the AI in time instead of falling back to DeepL. The copy is only sent on slow lines, and never while the service says it's busy (a fraction of a cent each with Claude Haiku).
  - The same for hearing you: if ElevenLabs is slow to transcribe a phrase, a second copy goes out and the first answer is used. Before, a stuck transcription could hold everything up for up to 2 minutes; now it gives up after 20 seconds at most.
  - Your mic's audio now goes to ElevenLabs as raw audio instead of a WAV file, which ElevenLabs says it answers sooner (Listen already did this).
  - Claude: when it won't say a line with swearing in it, it goes straight to the swear words by DeepL after the first no, within your normal time limit. Before, it asked Claude a second time (which almost never works for swearing) and then gave the last step a whole new time limit: up to about 4 seconds of translating, or 8 with Best translations.
  - Gemini: asks for the least thinking a model offers ("minimal" on Flash models). Models that don't offer it step up to "low", or no setting, on their own.
  - Words for DeepL: DeepL looks up your listed words at the same time the AI translates the rest of the line, instead of after it (about 0.7 s quicker for two words).
  - DeepL is kept for last: it only starts 0.7 s before your time limit, as a backup ready in case the AI runs out of time, and is stopped the moment the AI answers.
  - Every connection CIMA Box uses (the AI, its second connection, ElevenLabs, DeepL) is opened at once while you're still talking and kept open between lines, so no line waits to connect. A connection that doesn't answer is dropped after 3 s instead of 5, so it can't hold up your next phrase.
  - Stopping a request now takes effect right away. Before, the Cancel key (and a backup that wasn't needed) could take up to a second to let go.
  - What each AI model accepts (thinking and effort settings) is learned once for the whole app and checked again when you pick a different model, instead of costing an extra request in each part of the app.

## 2.8.3
- How you talk, checked line by line: Very casual no longer hints at swearing or teasing (it only swears if you did), "Clean up my swearing" now always wins over Very casual's slang, My own changes how a line sounds but never what it says, and a dialect's own words win over the standard language's casual hints (Kansai gets Kansai endings, not Tokyo ones).
- The bottom strip now names the service that really translated your listed words: "your words by Free" when DeepL doesn't have the language and the Free service did it (before, it always said DeepL). A word neither can translate is left out.
- Translations no longer leave your own words in the original language. Filipino and Hindi were told to mix in English the way people do casually, so a line like "go ahead" could stay in English; now only the few English words people really use (okay, sorry) are kept, and every language is told that someone who only speaks it has to understand the whole line.
- When the AI won't say a word (some harsh profanity, for example), it now leaves just that word out instead of swapping your whole line for a different, friendlier message you never said.
- New: Find a setting. Click the magnifier next to the gear (or press Ctrl+F), type a few letters like "swear", "key" or "chatbox", and it takes you straight to the setting and lights it up. On/off settings can be switched right in the search list.
- New: Settings > Look. Make CIMA Box yours: text size (85% to 130%), four color themes (Default, Midnight, Forest, Rose) or your own accent, text, background and panel colors, and a background picture of your own (JPEG or PNG) with how strongly it shows. "Back to the original look" undoes it all. The dog logo stays as it is.
- The page tabs make room in long languages instead of running into the switch.
- New: Words for DeepL (Features > Talking). Make your own list of words the AI tends to soften or refuse, like strong swearing or slang. Those words never go through the AI: it translates the rest of your line around them, and DeepL (or Free, without a DeepL key) translates just those words. If the AI can't keep the gaps in place, the whole line goes to DeepL instead. DeepL gets the whole sentence as context, so it picks the right meaning for each word, and if DeepL swaps a word for a whole different phrase instead of translating it, that word is left out rather than saying something you never said.
- How you talk stays just as casual, but it no longer adds things you didn't say: no extra sentences, reactions or nicknames like "bro" or "dude", and nothing borrowed from what others said before. At most one small filler word per line.
- Very casual no longer adds swear words or crude words you didn't say. It keeps your swearing when you swear, but it won't invent any (in Arabic especially, a word that sounds a bit rough in English can be a real insult).
- Arabic with no dialect picked now uses one neutral, widely understood spoken Arabic instead of mixing words from different dialects in the same line. For the most natural result, pick the dialect of the people you're talking to.
- Fixed: higher voices (most women's, and excited voices) were often measured an octave or more too low. That threw off how your tone is followed on My voice, learning your usual voice, and telling voices apart on Listen. All three should be steadier now, especially for women.
- Learning your usual voice works with more voices: higher and more excited voices are measured now, and shorter phrases count. If it still doesn't move, the log says why.

## 2.8.2
- Push to talk can now leave your real voice on between presses (My voice > Microphone > While not holding the key > My real voice). Talk normally, and hold the key when you want a line translated or in the new voice. Your mic goes quiet from the press until that line has played, so people never hear both at once. Silent, the way it worked before, stays the default.

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
