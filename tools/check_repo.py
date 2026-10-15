"""Check local Markdown links and obvious accidental secret/artifact inclusion."""
import os
from pathlib import Path
import re
import sys
import urllib.parse

ROOT = Path(__file__).resolve().parents[1]
SKIP = {'.git', '.build', 'out', 'dist', 'bin', 'obj', 'build', '.venv', 'venv', '__pycache__', '.local', 'private', 'backups'}
SECRET = [re.compile(r'gh[pousr]_[A-Za-z0-9]{30,}'),
          re.compile(r'github_pat_[A-Za-z0-9_]{40,}'),
          re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')]

def files():
    for parent, dirs, names in os.walk(ROOT):
        dirs[:] = [name for name in dirs if name not in SKIP]
        for name in names:
            yield Path(parent) / name

def main():
    errors, count = [], 0
    for path in files():
        count += 1
        rel = path.relative_to(ROOT)
        if path.suffix.lower() in {'.bin', '.elf', '.map', '.pdb', '.zip', '.log', '.pyc'}:
            errors.append(f'{rel}: generated/private artifact outside an ignored output directory')
        if path.suffix.lower() in {'.ttf', '.png', '.gif'}:
            continue
        try:
            body = path.read_text(encoding='utf-8')
        except UnicodeDecodeError:
            continue
        if any(pattern.search(body) for pattern in SECRET):
            errors.append(f'{rel}: possible credential/private key (value suppressed)')
        if path.suffix.lower() != '.md':
            continue
        body = re.sub(r'```.*?```', '', body, flags=re.S)
        for target in re.findall(r'!?\[[^\]]*\]\(([^)]+)\)', body):
            target = target.split(' "', 1)[0].strip('<>')
            parsed = urllib.parse.urlsplit(target)
            if parsed.scheme or target.startswith('#') or not parsed.path:
                continue
            if not (path.parent / urllib.parse.unquote(parsed.path)).exists():
                errors.append(f'{rel}: missing local link {target}')
    if errors:
        print('\n'.join(errors))
        return 1
    print(f'Checked {count} public source/document/asset files: local links valid, no blocked artifacts or recognized secret patterns.')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
