"""Generate a small character-folding table from the pinned Apache-2.0 OpenCC data."""
import hashlib
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
source = ROOT / 'assets/opencc/TSCharacters.txt'
assert hashlib.sha256(source.read_bytes()).hexdigest() == '9ff46a7d30e5765375eb13d33f2b03a34d298913caf2b120380679f33ae1642d'
pairs = []
for line in source.read_text(encoding='utf-8').splitlines():
    if not line or line.startswith('#'):
        continue
    original, alternatives = line.split('\t')
    target = alternatives.split()[0]
    if len(original) == len(target) == 1 and original != target:
        pairs.append((ord(original), ord(target)))
pairs.sort()
out = ['// Generated from OpenCC TSCharacters.txt, Apache-2.0; see assets/opencc/LICENSE.',
       '#include <stdint.h>', 'namespace ink {',
       'static const uint32_t pairs[][2] = {']
out += ['{%d,%d},' % pair for pair in pairs]
out += ['};', 'uint32_t foldHan(uint32_t cp) {',
        '  int left = 0, right = sizeof(pairs) / sizeof(pairs[0]) - 1;',
        '  while (left <= right) {', '    int mid = (left + right) / 2;',
        '    if (pairs[mid][0] == cp) return pairs[mid][1];',
        '    if (pairs[mid][0] < cp) left = mid + 1; else right = mid - 1;',
        '  }', '  return cp;', '}', '}']
(ROOT / 'firmware/InkOS/src/LyricHan.cpp').write_text('\n'.join(out)+'\n', encoding='utf-8')
print('Generated', len(pairs), 'character folds (search only; display text is unchanged).')
