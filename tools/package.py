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
    parser.add_argument('--bridge', type=Path, required=True, help='Compiled storage_bridge.ino.bin for one-time USB migration')
    parser.add_argument('--assets', type=Path, default=ROOT / 'out/assets')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/release')
    args = parser.parse_args()
    if not args.bridge.is_file():parser.error('Missing USB storage maintenance bridge')
    args.output.mkdir(parents=True, exist_ok=True)
    for name in FIRMWARE:
        if not (args.firmware / name).is_file():
            parser.error(f'Missing firmware image: {name}')
    artifacts = []
    from migrate_ota import validate_layout
    from ota import package
    validate_layout(args.firmware / 'InkOS.ino.partitions.bin')
    if not (args.assets / 'fonts.bin').is_file():
        parser.error('Run tools/generate_cjk_fonts.py first.')
    ota = args.output / 'InkOS-OTA-0.7.2.zip'
    package(args.firmware / 'InkOS.ino.bin', args.assets / 'manifest.json', ota, version='0.7.2')
    artifacts.append(ota)
    fw = args.output / 'InkOS-USB-Migration-0.7.2.zip'
    with zipfile.ZipFile(fw, 'w', zipfile.ZIP_DEFLATED) as z:
        for name in FIRMWARE:
            z.write(args.firmware / name, name)
        for name in ['fonts.bin', 'manifest.json']:
            z.write(args.assets / name, 'assets/' + name)
        z.write(args.bridge,'storage_bridge.ino.bin')
        for relative in ['LICENSE', 'docs/OTA.md', 'tools/migrate_ota.py', 'tools/migrate_extfs.py', 'tools/external_flash.py', 'tools/ota.py', 'docs/HARDWARE.md', 'docs/BUILD.md', 'docs/THIRD_PARTY.md',
                         'assets/SunCalc-LICENSE', 'assets/opencc/LICENSE', 'assets/opencc/SOURCE.md', 'assets/fonts/license.txt', 'assets/icons/mdi/LICENSE', 'assets/icons/mdi/SOURCE.md', 'assets/icons/lucide/LICENSE', 'assets/icons/lucide/SOURCE.md', 'libraries/pugixml/LICENSE.md', 'libraries/pugixml/SOURCE.md', 'docs/ICLOUD.md',
                         'assets/licenses/LGPL-2.1.txt', 'assets/licenses/Apache-2.0.txt',
                         'assets/licenses/libb64-LICENSE.txt',
                         'assets/fonts/terminus/OFL.TXT', 'assets/fonts/source-han-sans/OFL.txt', 'assets/fonts/source-han-sans/SOURCE.md', 'libraries/uICAL/LICENSE', 'tools/flash.py']:
            z.write(ROOT / relative, relative)
        package_files = [(name,args.firmware/name) for name in FIRMWARE] + [('assets/'+name,args.assets/name) for name in ['fonts.bin','manifest.json']] + [('storage_bridge.ino.bin',args.bridge)]
        sums = ''.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + name + '\n' for name,path in package_files)
        z.writestr('SHA256SUMS', sums)
        z.writestr('README.txt', 'InkOS 0.7.2 / USB STORAGE MIGRATION / internal 5.9375 MiB A/B slots + fonts / external 4 MiB LittleFS\n'
                   'Read docs/OTA.md before use. Keep previous app/font images and a private full internal backup.\n'
                   'Use tools/migrate_ota.py with explicit device MAC, paths and backups; never erase-flash or use a merged image.\n'
                   'Normal future IP upgrades use the separate InkOS-OTA-0.7.2.zip (app-only).\n')
    artifacts.append(fw)
    if args.studio:
        if not (args.studio / 'InkOS Studio.exe').is_file():
            parser.error('Studio publish directory must contain InkOS Studio.exe')
        app = args.output / 'InkOS-Studio-0.7.2-Windows-x64.zip'
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
