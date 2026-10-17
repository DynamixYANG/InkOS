"""USB maintenance: move legacy/AB4 LittleFS to U5, enlarge both OTA slots.
Requires an existing verified full internal backup. Keep the private backup folder.
"""
import argparse,hashlib,json,struct,subprocess,time
from pathlib import Path
from external_flash import Bridge

LAYOUT=[('nvs',1,2,0x9000,0x6000),('otadata',1,0,0x10000,0x2000),
 ('ota_0',0,16,0x20000,0x5f0000),('ota_1',0,17,0x610000,0x5f0000),('fonts',1,64,0xc00000,0x400000)]
def entries(raw):
 result=[]
 for pos in range(0,len(raw),32):
  magic,kind,subtype,offset,size,label,flags=struct.unpack('<HBBII16sI',raw[pos:pos+32])
  if magic==0x50aa:result.append((label.rstrip(b'\0').decode(),kind,subtype,offset,size))
 return result
def validate_layout(path):
 if entries(Path(path).read_bytes())!=LAYOUT:raise ValueError('Unexpected external-FS A/B v2 partition contract')
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('port','expected-mac','esptool','images','assets','previous-images','previous-assets','bridge-image','backup','existing-internal-backup'):p.add_argument('--'+name,required=True)
 a=p.parse_args();images=Path(a.images);assets=Path(a.assets);folder=Path(a.backup)
 validate_layout(images/'InkOS.ino.partitions.bin')
 font=(assets/'fonts.bin').read_bytes();meta=json.loads((assets/'manifest.json').read_text());app=(images/'InkOS.ino.bin').read_bytes()
 if hashlib.sha256(font).hexdigest()!=meta['sha256'] or not 64<len(font)<=0x400000 or meta['sha256'].encode() not in app or not 288<len(app)<=0x5f0000:raise ValueError('Image/asset contract mismatch')
 full=Path(a.existing_internal_backup)
 if full.stat().st_size!=0x1000000:raise ValueError('Complete private 16 MiB recovery backup required')
 folder.mkdir(parents=True,exist_ok=True)
 previous=Path(a.previous_images);previous_assets=Path(a.previous_assets)
 for file in ('InkOS.ino.bin','InkOS.ino.bootloader.bin','InkOS.ino.partitions.bin'):
  if not (previous/file).is_file():raise ValueError('Previous images required for USB recovery')
 old_fonts=(previous_assets/'fonts.bin').read_bytes();old_meta=json.loads((previous_assets/'manifest.json').read_text())
 if hashlib.sha256(old_fonts).hexdigest()!=old_meta['sha256'] or old_meta['sha256'].encode() not in (previous/'InkOS.ino.bin').read_bytes():raise ValueError('Previous font/application recovery identity mismatch')
 def tool(*command,no_stub=False):
  args=[a.esptool,'--chip','esp32s3','--port',a.port,'--baud','460800']+(['--no-stub'] if no_stub else [])+list(map(str,command))
  r=subprocess.run(args,capture_output=True,text=True,errors='replace')
  with (folder/'maintenance-private.log').open('a',encoding='utf-8') as f:f.write(r.stdout+r.stderr)
  if r.returncode:raise RuntimeError('USB operation failed; private log retained')
  return r.stdout
 if a.expected_mac.lower() not in tool('chip-id').lower():raise RuntimeError('Device identity differs; nothing written')
 prefix=folder/'boot-nvs-before.bin';cache=folder/'internal-storage-before.bin'
 if not prefix.exists():tool('read-flash',0,0x20000,prefix,no_stub=True)
 if not cache.exists():tool('read-flash',0xc20000,0x3e0000,cache,no_stub=True)
 if prefix.stat().st_size!=0x20000 or cache.stat().st_size!=0x3e0000:raise ValueError('Incomplete mutable-data backups')
 old=entries(prefix.read_bytes()[0x8000:0x8c00])
 if ('storage',1,130,0xc20000,0x3e0000) not in old:raise ValueError('Source layout must have the supported internal filesystem')
 if old!=entries((previous/'InkOS.ino.partitions.bin').read_bytes()):raise ValueError('Previous recovery images do not match live layout')
 record=dict(full_internal_backup=str(full.resolve()),full_sha256=digest(full),prefix_sha256=digest(prefix),filesystem_sha256=digest(cache),new_layout='inkos-ab594-extfs-v2')
 (folder/'migration.json').write_text(json.dumps(record,indent=2))
 blank=folder/'otadata-empty.bin';blank.write_bytes(b'\xff'*0x2000)
 def recover_previous():
  saved=prefix.read_bytes();selection=folder/'otadata-previous.bin';selection.write_bytes(saved[0x10000:0x12000])
  command=['write-flash','--flash-size','16MB','0x0',previous/'InkOS.ino.bootloader.bin','0x8000',previous/'InkOS.ino.partitions.bin','0x10000',selection]
  for label,kind,subtype,offset,size in old:
   if kind==0:command.extend([hex(offset),previous/'InkOS.ino.bin'])
   elif label=='fonts':command.extend([hex(offset),previous_assets/'fonts.bin'])
   elif label=='storage':
    fresh=folder/'internal-storage-at-clone.bin'
    command.extend([hex(offset),fresh if fresh.exists() and fresh.stat().st_size==size else cache])
  tool(*command)
  print('Previous internal layout/apps/fonts/filesystem restored; NVS was not written. External backup retained.',flush=True)
 try:
  print('Recovery backups verified. Loading temporary USB storage bridge.',flush=True)
  tool('write-flash','--flash-size','16MB','0x10000',blank,'0x20000',a.bridge_image)
  b=Bridge(a.port)
  try:
   info=b.call('info')
   if info['jedec']!=0x5e4016 or info['capacity']!=0x400000:raise RuntimeError('Wrong external Flash identity; external storage untouched')
   external=folder/'external-before.bin'
   if external.exists():raise ValueError('External backup exists; use an audited recovery/resume plan instead of overwriting it')
   print('Backing up all 4 MiB external NOR before any erase/program operation.',flush=True)
   b.backup(external,info['capacity']);record['external_before_sha256']=digest(external)
   (folder/'migration.json').write_text(json.dumps(record,indent=2))
   # Internal cache can change after a backup reset, before the bridge starts.
   # Refresh its exact image through the bridge while no filesystem is mounted.
   fresh=folder/'internal-storage-at-clone.bin'
   with fresh.open('xb') as out:
    for offset in range(0,0x3e0000,4096):
     import base64
     out.write(base64.b64decode(b.call('read_internal',offset=offset,length=4096)['data'],validate=True))
     if offset%0x40000==0:print(f'Internal filesystem snapshot {100*offset//0x3e0000}%',flush=True)
   if fresh.stat().st_size!=0x3e0000:raise RuntimeError('Incomplete exact filesystem backup')
   record['filesystem_sha256']=digest(fresh);record['filesystem_snapshot']=fresh.name;(folder/'migration.json').write_text(json.dumps(record,indent=2))
   command=dict(cmd='clone',external_before_sha256=record['external_before_sha256'],internal_sha256=record['filesystem_sha256'])
   b.port.write((json.dumps(command)+'\n').encode());until=time.monotonic()+180
   while time.monotonic()<until:
    line=b.port.readline()
    if line.startswith(b'CLONE '):print(line.decode().strip(),flush=True)
    try:result=json.loads(line)
    except (ValueError,UnicodeError):continue
    if result.get('bridge'):
     if not result.get('ok'):raise RuntimeError('Filesystem clone/readback failed; internal layout remains unchanged')
     break
   else:raise TimeoutError('External clone timeout; internal layout remains unchanged')
   mounted=b.call('mount')
   if mounted.get('total')!=0x400000:raise RuntimeError('External filesystem did not grow to full chip capacity')
   record['external_mount_verified']=True;record['filesystem_used']=mounted['used'];record['file_count']=mounted['files'];(folder/'migration.json').write_text(json.dumps(record,indent=2))
  finally:b.close()
  print('External filesystem cloned, readback verified, mounted and expanded. Updating internal layout.',flush=True)
  tool('write-flash','--flash-size','16MB','0x0',images/'InkOS.ino.bootloader.bin','0x8000',images/'InkOS.ino.partitions.bin','0x10000',blank,'0x20000',images/'InkOS.ino.bin','0x610000',images/'InkOS.ino.bin','0xc00000',assets/'fonts.bin')
  record['internal_layout_written']=True;(folder/'migration.json').write_text(json.dumps(record,indent=2))
  print('Verified migration writes complete. Await production boot health confirmation.',flush=True)
 except Exception:
  print('Migration failed; restoring the previous usable internal layout.',flush=True)
  recover_previous()
  raise
if __name__=='__main__':main()
