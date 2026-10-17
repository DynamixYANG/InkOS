"""Capture synthetic calendar layouts through USB, without changing device data or screen."""
import argparse
import base64
import io
import json
from pathlib import Path
import time
import serial
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ('home-weeks', 'week-grid', 'week-crowded', 'home-iso-year', 'week-dst', 'home-six-rows',
            'week-chinese', 'music-chinese', 'home-chinese', 'forecast-mdi', 'moon-layout', 'sun-layout', 'energy-layout', 'agenda-year-boundary')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/calendar-previews')
    parser.add_argument('--fixtures', type=int, nargs='+', choices=range(len(FIXTURES)))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(baudrate=115200, timeout=.2)
    port.port = args.port
    port.dtr = True
    port.rts = False
    with port:
        def command(payload):
            port.reset_input_buffer()
            port.write((json.dumps(payload) + '\n').encode())
            deadline = time.monotonic() + 20
            pending = bytearray()
            while time.monotonic() < deadline:
                pending.extend(port.read_until(b'\n'))
                if len(pending) > 4096:
                    raise RuntimeError('Preview chunk exceeded its limit')
                if not pending.endswith(b'\n'):
                    continue
                line = bytes(pending)
                pending.clear()
                try:
                    data = json.loads(line)
                except ValueError:
                    continue
                if isinstance(data, dict) and data.get('inkos'):
                    return data
            raise TimeoutError('USB calendar preview timed out')
        for fixture in args.fixtures or range(len(FIXTURES)):
            name = FIXTURES[fixture]
            chunks, offset, total = [], 0, None
            while total is None or offset < total:
                data = command({'cmd': 'calendar_preview', 'fixture': fixture, 'offset': offset})
                if 'preview_chunk' not in data:
                    raise RuntimeError('Calendar previews require InkOS 0.4.1 firmware')
                total = data['total']
                assert data['offset'] == offset and 0 < total < 70000
                chunk = data['preview_chunk']
                assert len(chunk) == min(1024, total - offset)
                chunks.append(chunk)
                offset += len(chunk)
            raw = base64.b64decode(''.join(chunks), validate=True)
            assert len(raw) == 48011 and raw.startswith(b'P4\n800 480\n')
            with Image.open(io.BytesIO(raw)) as image:
                assert image.size == (800, 480) and image.mode == '1'
                image.save(args.output / (name + '.png'))
            (args.output / (name + '.pbm')).write_bytes(raw)
            print('Rendered', name, flush=True)

if __name__ == '__main__':
    main()
