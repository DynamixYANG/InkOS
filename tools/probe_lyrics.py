"""Run fixed live lyrics-provider checks on ESP32; never export lyrics or account data."""
import argparse
import json
from pathlib import Path
import time
import serial

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {0: 'lrclib_search', 1: 'netease'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--fixtures', type=int, nargs='+', choices=range(3), default=[0, 1, 2])
    parser.add_argument('--output', type=Path, default=ROOT / 'out/lyrics-probes.json')
    args = parser.parse_args()
    port = serial.Serial(baudrate=115200, timeout=.2)
    port.port = args.port
    port.dtr, port.rts = True, False
    results = []
    with port:
        def command(payload):
            port.reset_input_buffer()
            port.write((json.dumps(payload) + '\n').encode())
            deadline, pending = time.monotonic() + 8, bytearray()
            while time.monotonic() < deadline:
                pending.extend(port.read_until(b'\n'))
                if len(pending) > 4096:
                    raise RuntimeError('USB response exceeded its limit')
                if not pending.endswith(b'\n'):
                    continue
                raw = bytes(pending)
                pending.clear()
                try:
                    value = json.loads(raw)
                except ValueError:
                    continue
                if isinstance(value, dict) and value.get('inkos'):
                    return value
            raise TimeoutError('USB command timed out')
        status = command({'cmd': 'status'})
        version = status.get('version', '')
        for fixture in args.fixtures:
            response = command({'cmd': 'lyrics_probe', 'fixture': fixture})
            if not response.get('accepted'):
                raise RuntimeError('Probe unavailable or another probe is running; requires firmware 0.4.3+')
            job, deadline = response['job'], time.monotonic() + 100
            while time.monotonic() < deadline:
                time.sleep(.75)
                response = command({'cmd': 'lyrics_probe'})
                if response.get('job') == job and response.get('complete'):
                    break
            else:
                raise TimeoutError('Live probe timed out; check Wi-Fi and device time')
            result = {key: response[key] for key in
                      ['fixture', 'source', 'timed', 'lines', 'requests', 'candidates',
                       'match_score', 'lrclib_http', 'netease_http', 'deferred']}
            result['passed'] = bool(result['timed'] and result['lines'] > 0 and
                                    (fixture not in EXPECTED or result['source'] == EXPECTED[fixture]))
            results.append(result)
            print(json.dumps(result), flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'firmware': version, 'fixtures': results}, indent=2) + '\n', encoding='utf-8')
    return 0 if all(item['passed'] for item in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
