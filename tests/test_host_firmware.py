"""Host builds of hardware-independent firmware pieces (log ring, virtual FAT disk).

Needs a C++17 compiler: $CXX, c++/g++/clang++ on PATH, or `python -m ziglang c++`.
Skipped when none is available. The FAT image is also checked with pyfatfs when installed.
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

    def test_virtual_fat(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.build_run([HOST / 'virtual_fat_test.cpp', SRC / 'VirtualFat.cpp'], cwd=tmp)
            image = Path(tmp) / 'img.bin'
            data = image.read_bytes()
            self.assertEqual(len(data), 8192 * 512)
            self.assertEqual(data[510:512], b'\x55\xaa')
            self.assertEqual(data[43:54], b'INKOS_LOGS ')
            if importlib.util.find_spec('pyfatfs') is None:
                return
            from pyfatfs.PyFatFS import PyFatFS
            disk = PyFatFS(str(image), read_only=True)
            names = disk.listdir('/')
            self.assertEqual(len(names), 18)
            for i in range(1, 17):
                expected = ''.join(chr(ord('a') + (k * 7 + i) % 26) for k in range(9000 + i * 1234))
                self.assertEqual(disk.readbytes('/inkos-%06d.tar.gz' % i).decode(), expected)
            current = next(name for name in names if name.lower() == 'current.log')
            self.assertEqual(disk.readtext('/' + current), ''.join('line %d\n' % k for k in range(3000)))
            disk.close()

if __name__ == '__main__':
    unittest.main()
