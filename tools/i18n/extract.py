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
calls = ['T','TF','fieldLabel','note','heading','subheading','button','toggleRow','sliderF','sliderI','beginPanel','colorRow','statusLine','apiKeyField','setStatus','fail']
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
scan_file('main.cpp', ['T','TF'])
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
drop={'textvoice','listensource','vrplace','vrlook','setapp','setkeys','setkeys2','input','output','SteamVR: ','Waiting for ',' to start.',
      'Listening to ','Listening to everything playing on ','settings-folder','documents','program','%.1f s','%.2f m','%+.2f m','%.0f%%','%d s'}
found += ['Off','Showing','Connected','Waiting for SteamVR','Problem','Hearing speech','Translating','Listening','Your mic',
          'Working','Some problems','Outage','Maintenance','See its page','All Systems Operational','Problems reported',
          'Open the status page to check',"Couldn't read the status page",'Checking',
          'Recording','Converting','Level','Auto','Voice','Search voices','the default speakers','the default input',"Couldn't save there, so your settings stayed where they were.","ElevenLabs didn't answer within 20 seconds. Check your internet connection, VPN or firewall."]
uniq=[]
for s in found:
    if s in drop: continue
    if '##' in s: s=s.split('##')[0]
    if keep(s) and s not in uniq: uniq.append(s)
json.dump(uniq, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'strings.json'),'w'), ensure_ascii=False, indent=0)
print(len(uniq), 'strings,', sum(len(s) for s in uniq), 'characters')
