import re, json, sys
import os
src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src') + os.sep
def literals(text):
    """Adjacent C string literals joined, decoded."""
    out=[]; i=0; cur=None; last_end=None
    while i < len(text):
        c=text[i]
        if c=='"':
            j=i+1; buf=''
            while text[j]!='"':
                if text[j]=='\\':
                    n=text[j+1]
                    if n=='n': buf+='\n'; j+=2
                    elif n=='"': buf+='"'; j+=2
                    elif n=='\\': buf+='\\'; j+=2
                    elif n=='x':
                        m=re.match(r'x([0-9A-Fa-f]{2})', text[j+1:]); buf+=chr(int(m.group(1),16)); j+=4
                    else: buf+=n; j+=2
                else: buf+=text[j]; j+=1
            if cur is not None and text[last_end:i].strip()=='' : cur+=buf
            else:
                if cur is not None: out.append(cur)
                cur=buf
            last_end=j+1; i=j+1
        elif c=="'":
            i = text.index("'", i+2)+1 if text[i+1]=='\\' else i+3
        else: i+=1
    if cur is not None: out.append(cur)
    # latin-1 decoded bytes from \x escapes -> utf-8
    res=[]
    for s in out:
        try: s=s.encode('latin-1').decode('utf-8')
        except Exception: pass
        res.append(s)
    return res
def balanced(text, start):
    depth=0; i=start
    while i < len(text):
        c=text[i]
        if c=='"':
            i+=1
            while text[i]!='"': i+= 2 if text[i]=='\\' else 1
        elif c=="'": i = text.index("'", i+2)+1 if text[i+1]=='\\' else i+3; continue
        elif c=='(': depth+=1
        elif c==')':
            depth-=1
            if depth==0: return text[start+1:i]
        i+=1
    return ''
calls = ['featureCard','para','bullet','pendingBar','chip','keyBox','guideLink','buttonPair','setupTitle','setupStepLine','setupCard','setupPageRow','T','TF','fieldLabel','note','heading','cardTitle','subheading','button','toggleRow','sliderF','sliderI','beginPanel','colorRow','statusLine','apiKeyField','setStatus','fail']
found=[]
def scan_file(name, which):
    text=open(src+name).read()
    text=re.sub(r'//[^\n]*', lambda m: '' if '"' not in m.group(0) or m.group(0).count('"')%2==0 else m.group(0), text)
    for fn in which:
        for m in re.finditer(r'(?<![A-Za-z0-9_])'+fn+r'\(', text):
            args=balanced(text, m.end()-1)
            for s in literals(args): found.append(s)
    for m in re.finditer(r'const char\* const k\w+\[\] = \{', text):
        end=text.index('};', m.end()); 
        for s in literals(text[m.end():end]): found.append(s)
scan_file('ui.cpp', calls)
_ui=open(src+'ui.cpp').read()
for _name in ['const KeyGuideText kKeyGuides', 'const char* top[]', 'const char* sub[]', 'for (const char* miss : {']:
    _i=_ui.index(_name); _j=_ui.index('};' if not _name.startswith('for') else '})', _i)
    for _s in literals(_ui[_i:_j]):
        if not _s.startswith('http') and '.com' not in _s and '.io' not in _s: found.append(_s)
scan_file('main.cpp', ['T','TF','quotaItem','notice','button'])
# Check my setup: every sentence in it is shown (translated) in the check window
_m=open(src+'main.cpp').read()
_i=_m.index('// ---------------------------------------------------------------- Check my setup'); _j=_m.index('static SetupCheck gCheck;')
found += [x for x in literals(_m[_i:_j]) if ' ' in x and not x.startswith('/')]
# the VR push-to-talk button's state
_v=open(src+'vroverlay.cpp').read()
for _line in _v.splitlines():
    if 'st.button =' in _line: found += literals(_line)
found += ['Connecting to SteamVR']
# the 3.0 frame: sidebar, page titles, Home, quick toggles
scan_file('ui3.inl', calls + ['navRow', 'sectionCard', 'sentenceWithChips', 'languageName'])
_u3=open(src+'ui3.inl').read()
for _m in re.finditer(r'return \{"([^"]+)"|return "([^"]+)";|\{kPage\w+, "([^"]+)", "([^"]+)"', _u3):
    found += [g for g in _m.groups() if g]
_i=_u3.index('const Tile kTiles[] = {'); _j=_u3.index('};', _i)
for _line in _u3[_i:_j].split('\n'):
    _l=literals(_line)
    if len(_l)==4: found += _l[1:]
_i=_u3.index('const char* names[3]'); found += literals(_u3[_i:_u3.index('};', _i)])
# dialect names, overlay/listen statuses
txt=open(src+'translate.cpp').read()
for m in re.finditer(r'\{"[a-z]+", "[A-Za-z0-9-]+", "([^"]+)",', txt): found.append(m.group(1))
for f in ['vroverlay.cpp','listen.cpp']:
    t=open(src+f).read()
    for m in re.finditer(r'(setStatus|placeProblem =|st.status =)\s*\(?', t):
        seg=t[m.end():m.end()+400].split(';')[0]
        for s in literals(seg): found.append(s)
    for m in re.finditer(r'placeProblem = ([^;]+);', t):
        for s in literals(m.group(1)): found.append(s)
# the updater's status messages (shown through T())
t=open(src+'updater.cpp').read()
for m in re.finditer(r'\{kUpdate\w+, ("[^"]*")\}', t): found.append(m.group(1)[1:-1])
for m in re.finditer(r'run\(kUpdate\w+, ("[^"]*")', t): found.append(m.group(1)[1:-1])
def keep(s):
    if not s.strip() or s.startswith('##'): return False
    if re.fullmatch(r'[%0-9.a-z ]*%[0-9.]*[a-z]', s) and ' ' not in s.strip(): return False
    if s in ('%s','%.2f','%.0f','%d','%.0f dB','%d ms','%.1fx','%.2f s','%s%s %.2f s',', ','.'): return False
    if s.endswith('.exe') or s.endswith('.dll'): return False
    return True
drop={'Google Gemini','meaning','mic','out','check','share','stop','sound','CABLE Input (VB-Audio ...)','CABLE Output (VB-Audio ...)','chatbox','emotion','listen','vr','#','Ctrl F','CIMA Box','CABLE Input','CABLE Output','ElevenLabs','textvoice','listensource','vrplace','vrlook','setapp','setkeys','setkeys2','input','output','fxtalk','fxhear','fxmore','langs','try','keys','captions','overlay','phrases','words','learn','history','profiles','cancel','answer','swear','best','screen','ear','live','mine','around','deeplwords','lookpreset','SteamVR: ','Waiting for ',' to start.',
      'Listening to ','Listening to everything playing on ','settings-folder','documents','program','%.1f s','%.2f m','%+.2f m','%.0f%%','%d s','%.0f MB'}
# the translation service dropdown: what each service is (the names are brands, apart from these)
_i=_ui.index('static const Choice kChoices[] = {'); _blk=_ui[_i:_ui.index('};', _i)]
for _row in re.findall(r'\{k\w+, "[^"]*", "[^"]*"\}', _blk): found.append(literals(_row)[1])
found += ["Voice fingerprints couldn't be loaded on this PC. Voices are matched the built-in way.","Couldn't save the download.","This version of CIMA Box has no voice fingerprint pack yet.","The download failed. Check your connection and try again.","The download couldn't be unpacked.","The download was incomplete."]
found += ['Free','Local','Gemini','Good fit','Tight','Push to talk key','Voice style','Voice speed','Volume others hear','Volume in my headphones','Match my microphone: amount','VR captions: distance','VR captions: height','VR captions: sideways','VR captions: size','VR captions: lines','VR captions: panel','Hear them in my ear: volume','Slow here','Not for this PC']
# models on this PC: the catalog's words and LocalAi's messages (shown through T())
_lc=open(src+'localai.cpp').read()
_i=_lc.index('static const std::vector<LocalModel> k = {'); _cat=_lc[_i:_lc.index('};', _i)]
for _row in re.findall(r'\{"[^}]*\}', _cat, re.S):
    _l=literals(_row)
    if len(_l)>=6: found += [_l[2], _l[-1]]
found += ["Not enough free space on the disk.","Couldn't download it. Check your connection and try again.",
          "Couldn't find this model's file to download.","The download was damaged on the way. Try again.","Couldn't save the model.",
          "Couldn't find the engine to download. Check your connection and try again.","The engine didn't download correctly. Try again.",
          "Couldn't unpack the engine.","The engine download didn't have what was expected in it.","The engine couldn't be started.",
          "The model couldn't be loaded.","Running on the processor (the graphics card couldn't load it), so it's slower."]
found += ['Off','Showing','Connected','Waiting for SteamVR','Problem','Hearing speech','Translating','Listening','Your mic',
          'Working','Some problems','Outage','Maintenance',"Couldn't check",'All Systems Operational','Problems reported',
          "Couldn't check right now","That picture couldn't be read. Pick a JPEG or PNG file.","Couldn't copy that picture into CIMA Box's settings folder.",'Answering normally (checked from this PC)',"This key isn't allowed to read usage (it needs User: Read access).",
          'The DeepL key was refused.','Couldn\'t check','Hi! This is how my new voice sounds.','Stopped at your spending limit. Press OK on the notice at the top to keep going.',
          'Paste a key first.','Couldn\'t reach the service. Check your internet connection.','It works.',
          'The key is right, but the account has no credits yet. Buy credits in the Claude Console (Billing).',
          'The key is right, but the account has no credit. Add credits on platform.openai.com (Billing), or pick the Build plan.',
          'The key is right, but your OpenAI plan can\'t use gpt-6-luna yet. Adding $5 of credit (Build plan) unlocks it.',
          'That key was refused. Copy it again, the whole thing.','The key works, but this month\'s DeepL characters are used up.',
          'The key works, but it\'s busy or out of credit right now.',
          'Change my voice','Talk into your mic and others hear a different voice, or your words in another language.',
          'Type and be heard','Type instead of talking. A voice says it for you, translated if you like.',
          'Read what others say','Live captions of what people say in a game or call, translated into your language.',
          'Just me','Play it on my headphones or speakers. Good for trying it out.',
          'Into a game, Discord or a call','Other people hear it as if it were your microphone.',
          'I have my own setup',"A virtual cable or mixer I already use. I'll pick the output myself.",
          'Start','Continue','Next','Back','Skip for now','Checking','Pick a voice','Voice','My voice','Text','Listen',"Couldn't read the status page",'Checking',
          'Taking a picture','Take a snapshot first.','No text found in the picture.',
          'The window is minimized. Bring it back on screen to translate it.','Pick the window to translate.',
          'The window is too small to read.',"Windows can't read this language yet. Add it in Windows Settings > Time & language > Language (with its text recognition), or use AI reading.",
          "Windows' text reader isn't available on this PC. Use AI reading instead.",
          "This copy was built without Windows' text reader. Use AI reading instead.",
          'AI reading needs Claude, Gemini or OpenAI as the translation service.',
          'Recording','Converting','Waiting for more','transcribe (live)','Level','Auto','Voice','Search voices','the default speakers','the default input',"Couldn't save there, so your settings stayed where they were.","ElevenLabs didn't answer within 20 seconds. Check your internet connection, VPN or firewall."]
uniq=[]
for s in found:
    if s in drop: continue
    if '##' in s: s=s.split('##')[0]
    if keep(s) and s not in uniq: uniq.append(s)
json.dump(uniq, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'strings.json'),'w'), ensure_ascii=False, indent=0)
print(len(uniq), 'strings,', sum(len(s) for s in uniq), 'characters')
