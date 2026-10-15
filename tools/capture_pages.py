"""Capture each real 800x480 device framebuffer, leaving the device on Home."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import urllib.parse
import urllib.request
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PAGES = [(0, '01-home'), (1, '02-music'), (2, '03-forecast'), (3, '04-week'),
         (4, '05-moon'), (5, '06-sun'), (6, '07-energy')]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True, help='Local device URL, e.g. http://192.168.1.100')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/captures')
    args = parser.parse_args()
    base = args.url.rstrip('/')
    args.output.mkdir(parents=True, exist_ok=True)
    frames = {}
    def select(n):
        with urllib.request.urlopen(urllib.request.Request(base + '/page',
                  data=urllib.parse.urlencode({'n': n}).encode()), timeout=30) as r:
            r.read()
        time.sleep(5)
    try:
        for n, name in PAGES:
            select(n)
            with urllib.request.urlopen(base + '/frame.pbm', timeout=30) as response:
                raw = response.read()
            assert len(raw) == 48011 and raw.startswith(b'P4\n800 480\n')
            path = args.output / (name + '.pbm')
            path.write_bytes(raw)
            with Image.open(path) as im:
                assert im.size == (800, 480) and im.mode == '1'
                im.save(args.output / (name + '.png'))
            frames[name] = hashlib.sha256(raw).hexdigest()
            print('Captured', name, flush=True)
    finally:
        select(0)
    (args.output / 'frames.json').write_text(json.dumps(frames, indent=2) + '\n')

if __name__ == '__main__':
    main()
