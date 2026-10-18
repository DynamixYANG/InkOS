"""Host builds of hardware-independent firmware pieces (log ring, log naming/retention policy).

Needs a C++17 compiler: $CXX, c++/g++/clang++ on PATH, or `python -m ziglang c++`.
Skipped when none is available.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'firmware/InkOS/src'
HOST = ROOT / 'tests/host'

def compiler():
    if os.environ.get('CXX'):
        return os.environ['CXX'].split()
    for name in ('c++', 'g++', 'clang++'):
        if shutil.which(name):
            return [name]
    if importlib.util.find_spec('ziglang'):
        return [sys.executable, '-m', 'ziglang', 'c++']
    return None

CXX = compiler()

@unittest.skipUnless(CXX, 'no C++ compiler available')
class HostFirmwareTests(unittest.TestCase):
    def build_run(self, sources, *extra, cwd):
        exe = Path(cwd) / 'test.exe'
        subprocess.run(CXX + ['-std=c++17', '-O1', '-DARDUINO_USB_MODE=1', '-I', str(HOST / 'stub'),
                              '-I', str(SRC), *extra, *map(str, sources), '-o', str(exe)],
                       check=True, capture_output=True, text=True)
        return subprocess.run([str(exe)], cwd=cwd, check=True, capture_output=True, text=True)

    def test_log_ring(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = self.build_run([HOST / 'log_ring_test.cpp', SRC / 'Log.cpp'], cwd=tmp)
            self.assertIn('OK', out.stdout)

    def test_log_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = self.build_run([HOST / 'log_policy_test.cpp', SRC / 'LogPolicy.cpp'], cwd=tmp)
            self.assertIn('OK', out.stdout)

    def test_moon_events(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = self.build_run([HOST / 'moon_events_test.cpp', SRC / 'MoonEvents.cpp'], cwd=tmp)
            self.assertIn('OK', out.stdout)

if __name__ == '__main__':
    unittest.main()
