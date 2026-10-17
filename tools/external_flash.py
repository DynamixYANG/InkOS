"""Inspect U5 through the USB maintenance bridge. Probe/reset never erases NOR data."""
import argparse,base64,hashlib,json,time
from pathlib import Path
import serial
class Bridge:
    def __init__(self,port):
        self.port=serial.Serial(baudrate=115200,timeout=.5);self.port.port=port;self.port.dtr=True;self.port.rts=False;self.port.open()
    def close(self):self.port.close()
    def call(self,cmd,**fields):
        self.port.write((json.dumps(dict(cmd=cmd,**fields))+'\n').encode());until=time.monotonic()+8
        while time.monotonic()<until:
            try:r=json.loads(self.port.readline())
            except (ValueError,UnicodeError):continue
            if r.get('bridge'):
                if not r.get('ok') and cmd!='info':raise RuntimeError('External Flash maintenance command failed')
                return r
        raise TimeoutError('Maintenance bridge did not respond')
    def read(self,offset,length):return base64.b64decode(self.call('read',offset=offset,length=length)['data'],validate=True)
    def backup(self,path,capacity):
        if path.exists():raise ValueError('External backup path already exists')
        with path.open('xb') as out:
            for offset in range(0,capacity,4096):
                data=self.read(offset,min(4096,capacity-offset))
                if len(data)!=min(4096,capacity-offset):raise RuntimeError('Incomplete external read')
                out.write(data)
                if offset%0x40000==0:print(f'External Flash backup {100*offset//capacity}%',flush=True)
        raw=path.read_bytes();assert len(raw)==capacity
        path.with_suffix('.json').write_text(json.dumps(dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest()),indent=2)+'\n')
    def stage(self,data):
        for offset in range(0,len(data),4096):
            self.call('erase',offset=offset,length=4096)
            for pos in range(offset,min(offset+4096,len(data)),256):
                part=data[pos:pos+256];self.call('write',offset=pos,length=len(part),data=base64.b64encode(part).decode())
            if offset%0x40000==0:print(f'External font staging {offset*100//len(data)}%',flush=True)
        digest=hashlib.sha256()
        for offset in range(0,len(data),4096):digest.update(self.read(offset,min(4096,len(data)-offset)))
        if digest.digest()!=hashlib.sha256(data).digest():raise RuntimeError('External font readback mismatch')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--port',required=True);parser.add_argument('--probe',action='store_true');args=parser.parse_args()
    bridge=Bridge(args.port)
    try:
        print(json.dumps(bridge.call('probe' if args.probe else 'info'),indent=2))
    finally:bridge.close()
