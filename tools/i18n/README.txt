Interface translations

  strings.json   every English text in the interface (made by extract.py)
  <code>.json    one language: {"English text": "translation"}
  extract.py     finds the texts in src/ (run after changing the interface)
  build.py       checks every language (missing texts, % codes) and writes src/i18n_data.h
                 and lang/_template.json.txt

To change a translation: edit <code>.json, run build.py, rebuild the program.
To add a language without rebuilding: see the Read Me (a file in the "lang" folder next to the exe).
