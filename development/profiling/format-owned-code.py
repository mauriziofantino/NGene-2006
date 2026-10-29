"""Apply the repository C/C++ style and verify tokens are preserved.

Vendor libraries and generated files are excluded. Comment cleanup is performed
before the token comparison, so formatting cannot hide code changes.
"""
import argparse
import difflib
import hashlib
import json
import pathlib
import re
import subprocess

root = pathlib.Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--formatter', required=True)
a = p.parse_args()
folders = ['receiver', 'frontEnd', 'acquisition', 'tracking', 'pvt', 'utility',
           'utilityGnss', 'osnma', 'chimera', 'warmStart', 'asmFolder', 'platform',
           'tools', 'development/profiling']
files = sorted({f for folder in folders for f in (root/folder).rglob('*')
                if f.is_file() and (f.suffix in ['.c', '.h', '.cpp'] or f.name.endswith('.c~'))})
token = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|'
                   r'[A-Za-z_]\w*|0[xX][\da-fA-F.]+(?:[pP][+-]?\d+)?[uUlLfF]*|'
                   r'\d+(?:\.\d*)?(?:[eE][+-]?\d+)?[uUlLfF]*|'
                   r'>>=?|<<=?|->|::|\+\+|--|&&|\|\||[+*/%&|^!=<>-]=|\S')


def tokens(text):
    logical = re.sub(r'\\\r?\n', '', text)
    return [t for t in token.findall(logical) if not t.startswith(('//', '/*'))]


def cleanup(text, filename):
    # Empty block comments are old layout markers, not documentation.
    text = re.sub(r'/\*\s*\*/', '', text)
    text = re.sub(r'(?m)^(\s*\* @file\s+)[^\r\n]+', lambda m:m[1]+filename, text)
    return text


before = {}
manifest = {}
for path in files:
    text = cleanup(path.read_text(encoding='utf-8-sig'), path.name)
    before[path] = tokens(text)
    if path.read_bytes() != text.encode('utf-8'):
        path.write_text(text, encoding='utf-8', newline='\n')
for start in range(0, len(files), 48):
    subprocess.run([a.formatter, '-i', '--style=file', *map(str, files[start:start+48])], check=True)
for path in files:
    after = tokens(path.read_text(encoding='utf-8'))
    if before[path] != after:
        change = '\n'.join(difflib.unified_diff(before[path], after))
        raise RuntimeError(f'Formatter changed tokens in {path}:\n{change[:4000]}')
    manifest[str(path.relative_to(root))] = dict(tokens=len(after),
        token_sha256=hashlib.sha256('\0'.join(after).encode()).hexdigest())

# Refresh the preserved optional instrumentation against the same style.
instrumented = root/'build/style-refresh/instrumented'
patch = []
for name in ['receiver/ngene.c', 'utility/profilingSupport.c', 'utility/profilingSupport.h']:
    temp = instrumented/name
    source = cleanup(temp.read_text(encoding='utf-8-sig'), temp.name)
    # stdin avoids the build/ exclusion in .clang-format-ignore.
    formatted = subprocess.run([a.formatter, '--style=file:'+str(root/'.clang-format'),
                                '--assume-filename='+str(root/name)], input=source,
                               capture_output=True, encoding='utf-8', check=True).stdout
    temp.write_text(formatted, encoding='utf-8', newline='\n')
    patch.append(f'diff --git a/{name} b/{name}\n')
    patch.extend(difflib.unified_diff((root/name).read_text().splitlines(True),
                                    temp.read_text().splitlines(True),
                                    fromfile='a/'+name, tofile='b/'+name))
(root/'development/profiling/instrumentation.patch').write_text(''.join(patch), encoding='utf-8', newline='\n')
(root/'build/style-refresh/token-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print(f'Formatted {len(files)} project C/C++ files; all formatting token sequences match.')
