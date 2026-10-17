"""OTA package rejection and immutable font archive contract checks."""
import hashlib,importlib.util,json,struct,sys,tempfile,unittest,zipfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ota import load_package,package
from font_pack import pack_fonts,compress_archive,SIZES
from migrate_ota import validate_layout

class OtaTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
  b=bytearray(512);b[0]=0xe9;b[12]=9;b[32:36]=bytes.fromhex('3254cdab');self.image=self.root/'image.bin';self.image.write_bytes(b)
  self.assets=self.root/'assets.json';self.assets.write_text(json.dumps({'sha256':'a'*64}));self.path=self.root/'ota.zip'
  self.manifest=package(self.image,self.assets,self.path)
 def tearDown(self):self.temp.cleanup()
 def edit(self,edit,extra=False):
  m,b=load_package(self.path);edit(m)
  with zipfile.ZipFile(self.path,'w') as z:
   z.writestr('manifest.json',json.dumps(m));z.writestr('firmware.bin',b)
   if extra:z.writestr('../credentials.txt','unused')
 def test_valid_app_package(self):self.assertEqual(load_package(self.path)[1],self.image.read_bytes())
 def test_wrong_layout(self):
  self.edit(lambda m:m.update(layout='legacy'))
  with self.assertRaises(ValueError):load_package(self.path)
 def test_corrupt_image_hash(self):
  self.edit(lambda m:m.update(sha256='0'*64))
  with self.assertRaises(ValueError):load_package(self.path)
 def test_archive_extra_path(self):
  self.edit(lambda m:None,True)
  with self.assertRaises(ValueError):load_package(self.path)
 def test_no_bootloader_package(self):
  with zipfile.ZipFile(self.path,'w') as z:z.writestr('bootloader.bin',b'wrong')
  with self.assertRaises(ValueError):load_package(self.path)
 def test_expanded_slot_accepts_more_than_four_mib(self):
  b=self.image.read_bytes()+bytes(0x400100-512);self.image.write_bytes(b)
  package(self.image,self.assets,self.path)
  self.assertEqual(len(load_package(self.path)[1]),0x400100)
 def test_expanded_slot_rejects_overflow(self):
  self.image.write_bytes(self.image.read_bytes()+bytes(0x5f0001-512))
  with self.assertRaises(ValueError):package(self.image,self.assets,self.path)
 def test_native_glyph_bytes_unchanged(self):
  fonts=[(s,b'\xa8',[(0x4e00,0,1,-2,3,2)]) for s in SIZES];b,stats=pack_fonts(fonts)
  header=struct.unpack_from('<8s7I',b);index,data=header[-2:]
  for i in range(4):
   offset,x,y,w,h=struct.unpack_from('<IbbBB',b,index+i*8)
   self.assertEqual((x,y,w,h),(1,-2,3,2));self.assertEqual(b[data+offset:data+offset+1],b'\xa8')
 def test_lossless_compressed_blocks_and_tail(self):
  import zlib
  original=bytes(range(256))*2200+b'last block / native pixels'
  packed=compress_archive(original);h=struct.unpack_from('<8s6I32s',packed)
  self.assertEqual(h[0],b'INKFZ1\0\0');self.assertEqual(h[3],len(original));self.assertEqual(h[-1],hashlib.sha256(original).digest())
  positions=struct.unpack_from('<%dI'%(h[5]+1),packed,64)
  decoded=b''.join(zlib.decompress(packed[positions[i]:positions[i+1]],-15) for i in range(h[5]))
  self.assertEqual(decoded,original);self.assertEqual(positions[-1],len(packed));self.assertGreater(h[5],1)
 def test_partition_contract(self):
  source=Path(__file__).resolve().parents[1]/'out/extfs-firmware/InkOS.ino.partitions.bin'
  if not source.exists():self.skipTest('Firmware not built')
  validate_layout(source)
if __name__=='__main__':unittest.main()
