#!/usr/bin/env python3
"""Lists interface strings (tr("...") in src/) that have no Arabic translation yet.

Untranslated strings still work: the app shows them in English. Add translations to
resources/i18n/ar.json (key = the English text exactly as written in tr()).
Usage: python3 scripts/check_translations.py [--strict]   (--strict exits 1 if any are missing)
"""
import glob, json, os, re, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def unescape(s):
    return bytes(s, 'utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8')

strings = []
for f in sorted(glob.glob(os.path.join(root, 'src', '*.cpp'))):
    src = open(f, encoding='utf-8').read()
    for m in re.finditer(r'\btr\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)', src):
        s = ''.join(unescape(l) for l in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1)))
        if s not in strings:
            strings.append(s)

ar = json.load(open(os.path.join(root, 'resources', 'i18n', 'ar.json'), encoding='utf-8'))
missing = [s for s in strings if s not in ar]
unused = [k for k in ar if k not in strings]
for s in missing:
    print('missing:', repr(s))
for k in unused:
    print('unused: ', repr(k))
print(f'{len(strings)} strings, {len(missing)} missing, {len(unused)} unused')
sys.exit(1 if missing and '--strict' in sys.argv else 0)
