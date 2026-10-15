"""Portable Arduino build. All generated files stay in ignored directories."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FQBN = 'esp32:esp32:XIAO_ESP32S3_Plus:PSRAM=opi,CDCOnBoot=default,USBMode=hwcdc'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', default=os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli'))
    parser.add_argument('--config-file', default=os.environ.get('ARDUINO_CLI_CONFIG_FILE'))
    parser.add_argument('--build-dir', type=Path, default=ROOT / '.build/firmware')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/firmware')
    args = parser.parse_args()
    if not args.cli:
        parser.error('Install Arduino CLI or set ARDUINO_CLI to its executable.')
    args.output.mkdir(parents=True, exist_ok=True)
    # GCC embeds __FILE__ in assertions. Publish reproducible virtual paths,
    # including libraries/toolchains installed below the developer's home.
    mappings = {}
    for path, target in [(Path.home(), '/build-user'), (ROOT, '/project')]:
        mappings[path.as_posix()] = target
        mappings[str(path)] = target
    flags = ' '.join('"-ffile-prefix-map=' + source.replace('\\', '\\\\') + '=' + target + '"'
                     for source, target in mappings.items())
    cmd = [args.cli, 'compile', '--fqbn', FQBN, '--build-property', 'upload.maximum_size=12582912',
           '--build-property', 'compiler.cpp.extra_flags=' + flags,
           '--build-property', 'compiler.c.extra_flags=' + flags,
           '--build-path', str(args.build_dir.resolve()), '--output-dir', str(args.output.resolve()),
           '--libraries', str(ROOT / 'libraries')]
    if args.config_file:
        cmd += ['--config-file', args.config_file]
    cmd.append(str(ROOT / 'firmware/InkOS'))
    result = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    log = result.stdout + result.stderr
    (args.output / 'build.log').write_text(log, encoding='utf-8')
    print(log[-7000:])
    # The SDK can produce a merged image; distributing it risks overwriting NVS.
    for merged in args.output.glob('*.merged.bin'):
        merged.unlink()
    return result.returncode

if __name__ == '__main__':
    raise SystemExit(main())
