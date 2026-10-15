"""Package explicitly supplied build directories; never traverse device backups."""
import argparse
import hashlib
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ('InkOS.ino.bootloader.bin', 'InkOS.ino.partitions.bin', 'InkOS.ino.bin')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--studio', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/release')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in FIRMWARE:
        if not (args.firmware / name).is_file():
            parser.error(f'Missing firmware image: {name}')
    artifacts = []
    fw = args.output / 'InkOS-Firmware-0.3.1.zip'
    with zipfile.ZipFile(fw, 'w', zipfile.ZIP_DEFLATED) as z:
        for name in FIRMWARE:
            z.write(args.firmware / name, name)
        for relative in ['LICENSE', 'docs/HARDWARE.md', 'docs/BUILD.md', 'docs/THIRD_PARTY.md',
                         'assets/SunCalc-LICENSE', 'assets/fonts/license.txt',
                         'assets/licenses/LGPL-2.1.txt', 'assets/licenses/Apache-2.0.txt',
                         'assets/licenses/libb64-LICENSE.txt',
                         'assets/fonts/terminus/OFL.TXT', 'libraries/uICAL/LICENSE', 'tools/flash.py']:
            z.write(ROOT / relative, relative)
        sums = ''.join(hashlib.sha256((args.firmware / name).read_bytes()).hexdigest() + '  ' + name + '\n'
                       for name in FIRMWARE)
        z.writestr('SHA256SUMS', sums)
        z.writestr('README.txt', 'InkOS firmware 0.3.1 / Seeed TRMNL 7.5 OG DIY / XIAO ESP32-S3 Plus\n'
                   '16MB Flash / 8MB OPI PSRAM / UC8179 800x480\n'
                   'Use tools/flash.py --images . from this extracted directory. Inspect MAC first and make a private backup.\n'
                   'Write only: bootloader 0x0, partitions 0x8000, app 0x20000. Never erase-flash or write boot_app0/merged.bin.\n'
                   'Full instructions: https://github.com/DynamixYANG/InkOS/blob/main/docs/BUILD.md\n')
    artifacts.append(fw)
    if args.studio:
        if not (args.studio / 'InkOS Studio.exe').is_file():
            parser.error('Studio publish directory must contain InkOS Studio.exe')
        app = args.output / 'InkOS-Studio-0.3.0-Windows-x64.zip'
        with zipfile.ZipFile(app, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for path in sorted(args.studio.rglob('*')):
                if not path.is_file() or path.suffix.lower() in {'.pdb', '.log', '.bin'}:
                    continue
                if any(name in {'.local', 'backups', 'private', 'obj'} for name in path.relative_to(args.studio).parts):
                    continue
                z.write(path, Path('InkOS Studio') / path.relative_to(args.studio))
            z.write(ROOT / 'LICENSE', 'InkOS Studio/InkOS-LICENSE.txt')
            z.writestr('README.txt', 'Extract the entire folder, then run InkOS Studio/InkOS Studio.exe.\n'
                       'The Windows x64 .NET runtime and notices are included. Keep all files together.\n'
                       'https://github.com/DynamixYANG/InkOS/blob/main/docs/CONFIGURATION.md\n')
        artifacts.append(app)
    sums = ''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n' for path in artifacts)
    (args.output / 'SHA256SUMS').write_text(sums, encoding='utf-8')
    for path in artifacts:
        print(f'{path.name}: {path.stat().st_size} bytes')

if __name__ == '__main__':
    main()
