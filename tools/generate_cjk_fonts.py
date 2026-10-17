"""Build the bundled Chinese fallback at native monochrome sizes (no runtime scaling)."""
import hashlib
import json
from pathlib import Path
from PIL import ImageFont, __version__, features

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/fonts/source-han-sans'
SOURCE = ASSETS / 'SourceHanSansSC-Light.otf'
EXPECTED = 'b6dfc945aae0daf9b15019f59c2f7b18d17da3b92f00c3ec32fb5f0333696d9c'
SIZES = (14, 18, 24, 28)
RANGES = ((0x2000, 0x2070), (0x3000, 0x3100), (0x3100, 0x3130),
          (0x31A0, 0x31C0), (0x3400, 0x4DC0), (0x4E00, 0xA000),
          (0xF900, 0xFB00), (0xFF00, 0xFF70), (0xFFE0, 0xFFE7))


def main():
    assert hashlib.sha256(SOURCE.read_bytes()).hexdigest() == EXPECTED, 'Font source changed'
    manifest = {'source': 'https://github.com/adobe-fonts/source-han-sans',
                'file': SOURCE.name, 'sha256': EXPECTED, 'license': 'OFL-1.1',
                'family': 'Source Han Sans SC Light', 'sizes': {},
                'pillow': __version__, 'freetype': features.version_module('freetype2'),
                'raster': 'FreeType native-size monochrome, no antialiasing or runtime scaling',
                'coverage': 'BMP CJK unified/Extension A/compatibility ideographs, Chinese punctuation, kana and Bopomofo; no emoji or supplementary-plane ideographs'}
    fonts = []
    for size in SIZES:
        face = ImageFont.truetype(str(SOURCE), size)
        missing = bytes(face.getmask(chr(0x10FFFF), mode='1'))
        bits, glyphs = bytearray(), []
        for start, end in RANGES:
            for cp in range(start, end):
                mask, (x, y) = face.getmask2(chr(cp), mode='1', anchor='ls')
                data = bytes(mask)
                if data == missing:
                    continue
                w, h = mask.size
                glyphs.append((cp, len(bits), x, y, w, h))
                for pos in range(0, len(data), 8):
                    bits.append(sum(128 >> i for i, value in enumerate(data[pos:pos+8]) if value))
        assert len(glyphs) < 65536
        codes = {g[0] for g in glyphs}
        assert all(ord(c) in codes for c in '中文日程音乐歌词繁體測試，。')
        fonts.append((size, bytes(bits), glyphs))
        manifest['sizes'][size] = {'glyphs': len(glyphs), 'bitmap_bytes': len(bits)}
        print(f'{size}px: {len(glyphs)} glyphs, {len(bits)} bitmap bytes', flush=True)
    from font_pack import write_archive
    manifest['archive'] = write_archive(fonts, ROOT)
    (ASSETS / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
