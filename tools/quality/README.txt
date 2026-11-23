Translation quality test

  cases.json     the lines to translate: common languages, gender ("I am", "Talking to"), dialects,
                 slang, gaming and VRChat talk, lines that must be said and not answered, lines with
                 a conversation around them, Carry my emotion, and Listen's direction
  quality.cpp    runs them through the app's own translation code against a model on this PC
  run.ps1        starts the engine, runs the test, stops the engine
  report.txt     what came out (made by a run; every line and its translation, for a person to read)

To run it (CIMA Box must have downloaded a model on this PC once):

  cmake --build build --config Release --target cima_quality
  powershell -ExecutionPolicy Bypass -File tools\quality\run.ps1

It runs on the processor, so it can't take graphics memory from a game or from CIMA Box; that makes
it slow (a couple of seconds a line) and has no effect on what the model says. With -Gpu it runs on
the graphics card: close CIMA Box and any game first.

  -Only gender       only the cases whose id contains "gender" (ids are in cases.json)
  -Model <file>      another .gguf than the smallest one in the models folder

What it checks, and what it can't:
  It checks what shows in the letters: the right alphabet, the gender forms for "I am" and "Talking
  to", polite or casual forms, names kept, a question still a question, nothing taken from the
  conversation, slang not translated word for word.
  It can't tell whether a line sounds like a native speaker. That is what report.txt is for: give it
  to someone who speaks the language.
  A FAIL is something to look at, not always a mistake: a model may find a correct wording the case
  didn't list. Add it to the case's "has_any" then.

A case, in cases.json:
  id, group          a name, and the heading it goes under in the report
  to, from           language codes ("from" is "en" unless given)
  dialect            a dialect id as in the app (ar-KW, es-MX, pt-BR, ja-KS ...)
  level              "polite", "casual" or "very casual"
  i_am               "man", "woman" or "nonbinary";  talking_to: "man", "woman" or "group"
  line               what was said
  context            the recent conversation, one "Them: ..." or "Me: ..." per line
  marked             the line with Carry my emotion's directions in it ("[laughs] ...", a word in CAPITALS)
  listen             true for Listen's direction (someone else's speech, into your language)
  has_any, why_any   at least one of these must be in the answer (and what to say if none is)
  has_none, why_none none of these may be in it
  keeps              names that must stay as they are
  is_question        the answer must still be a question

Run it after changing the AI instructions (aiInstructions in src/translate.cpp), and compare the
report with the one before.
