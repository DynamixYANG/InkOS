"""Inspect an ESP32-S3, back up its boot/NVS, then write only the three InkOS images."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
IMAGE_NAMES = ('InkOS.ino.bootloader.bin', 'InkOS.ino.partitions.bin', 'InkOS.ino.bin')
OFFSETS = (0, 0x8000, 0x20000)

def image_plan(folder):
    paths = [folder / name for name in IMAGE_NAMES]
    for p in paths:
        if not p.is_file() or not p.stat().st_size:
            raise ValueError(f'Missing/empty image: {p.name}')
    entries = []
    raw = paths[1].read_bytes()
    if len(raw) < 32 or len(raw) % 32:
        raise ValueError('Invalid partition table length')
    for pos in range(0, len(raw), 32):
        magic, kind, subtype, offset, size, label, flags = struct.unpack('<HBBII16sI', raw[pos:pos+32])
        if magic == 0x50aa:
            entries.append((label.rstrip(b'\0').decode('ascii'), kind, subtype, offset, size))
    expected = [('nvs', 1, 2, 0x9000, 0x6000), ('factory', 0, 0, 0x20000, 0xC00000),
                ('storage', 1, 0x82, 0xC20000, 0x3E0000)]
    if not all(entry in entries for entry in expected):
        raise ValueError('Partition layout differs from the supported InkOS contract')
    limits = (0x8000, 0x1000, 0xC00000)
    if any(p.stat().st_size > limit for p, limit in zip(paths, limits)):
        raise ValueError('Image exceeds its flash region')
    return list(zip(OFFSETS, paths))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--images', type=Path, default=ROOT / 'out/firmware')
    parser.add_argument('--esptool', help='Optional esptool executable; default: python -m esptool')
    parser.add_argument('--inspect', action='store_true', help='Read chip identity without writing flash')
    parser.add_argument('--expected-mac', help='MAC shown by --inspect; required for writing')
    parser.add_argument('--backup', type=Path, help='New private backup filename, e.g. .local/backups/device.bin')
    parser.add_argument('--dry-run', action='store_true', help='Validate images and show the plan; no USB access')
    args = parser.parse_args()
    tool = [args.esptool] if args.esptool else [sys.executable, '-m', 'esptool']
    prefix = tool + ['--chip', 'esp32s3', '--port', args.port, '--baud', '460800',
                     '--before', 'default-reset', '--after', 'hard-reset']
    if args.inspect:
        return subprocess.run(prefix + ['chip-id']).returncode
    if not args.expected_mac or not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', args.expected_mac):
        parser.error('Use --inspect first, then pass its MAC with --expected-mac.')
    if not args.backup or args.backup.exists():
        parser.error('--backup must name a new private file; existing backups are never overwritten.')
    try:
        images = image_plan(args.images)
    except ValueError as e:
        parser.error(str(e))
    write = prefix + ['write-flash', '--flash-size', '16MB']
    for offset, path in images:
        write += [hex(offset), str(path)]
    if args.dry_run:
        print('No USB access. A 0x20000-byte private backup precedes these writes:')
        print(json.dumps([{'offset': hex(offset), 'file': p.name, 'bytes': p.stat().st_size}
                          for offset, p in images], indent=2))
        return 0
    identity = subprocess.run(prefix + ['chip-id'], capture_output=True, text=True, errors='replace')
    if identity.returncode or args.expected_mac.lower() not in identity.stdout.lower():
        raise SystemExit('Chip identity does not match; nothing written.')
    args.backup.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(prefix + ['read-flash', '0x0', '0x20000', str(args.backup)], check=True)
    if args.backup.stat().st_size != 0x20000:
        raise SystemExit('Backup size verification failed; nothing written.')
    subprocess.run(write, check=True)
    manifest = {'images': [{'file': p.name, 'offset': hex(offset),
                            'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for offset, p in images],
                'nvs_region_written': False}
    (args.images / 'flash-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print('Verified image writes completed. NVS region was not written; keep the backup private.')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
