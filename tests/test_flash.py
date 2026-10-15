"""Guard flash boundaries without a USB device or an esptool installation."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('flash', Path(__file__).resolve().parents[1] / 'tools/flash.py')
flash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flash)

class FlashContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in flash.IMAGE_NAMES:
            (self.root / name).write_bytes(b'\xff' * 32)
        self.entries = [(1, 2, 0x9000, 0x6000, b'nvs'), (0, 0, 0x20000, 0xC00000, b'factory'),
                        (1, 0x82, 0xC20000, 0x3E0000, b'storage')]
        self.write_table()

    def write_table(self):
        raw = b''.join(struct.pack('<HBBII16sI', 0x50aa, kind, sub, offset, size, label, 0)
                       for kind, sub, offset, size, label in self.entries)
        (self.root / flash.IMAGE_NAMES[1]).write_bytes(raw)

    def test_nvs_is_not_a_write_target(self):
        self.assertEqual([offset for offset, _ in flash.image_plan(self.root)], [0, 0x8000, 0x20000])

    def test_incompatible_nvs_layout_is_rejected(self):
        self.entries[0] = (1, 2, 0x9000, 0x5000, b'nvs')
        self.write_table()
        with self.assertRaises(ValueError): flash.image_plan(self.root)

    def test_default_application_offset_is_rejected(self):
        self.entries[1] = (0, 0, 0x10000, 0xC00000, b'factory')
        self.write_table()
        with self.assertRaises(ValueError): flash.image_plan(self.root)

    def test_missing_image_is_rejected(self):
        (self.root / flash.IMAGE_NAMES[0]).unlink()
        with self.assertRaises(ValueError): flash.image_plan(self.root)

    def test_oversized_bootloader_cannot_reach_partition_table(self):
        (self.root / flash.IMAGE_NAMES[0]).write_bytes(b'\xff' * 0x8001)
        with self.assertRaises(ValueError): flash.image_plan(self.root)

if __name__ == '__main__': unittest.main()
