"""Create or install an app-only InkOS OTA package over paired IP (no resident server)."""
import argparse,base64,getpass,hashlib,ipaddress,json,secrets,time,urllib.request,zipfile
from pathlib import Path
BOARD='xiao-esp32s3-plus-16m'
LAYOUT='inkos-ab594-extfs-v2'

def load_package(path):
    with zipfile.ZipFile(path) as z:
        if sorted(z.namelist())!=['firmware.bin','manifest.json']:raise ValueError('OTA ZIP must contain only manifest.json and firmware.bin')
        if z.getinfo('manifest.json').file_size>4096 or not 288<=z.getinfo('firmware.bin').file_size<=0x5f0000:raise ValueError('Invalid OTA package sizes')
        m=json.loads(z.read('manifest.json'));b=z.read('firmware.bin')
    if m.get('format')!=1 or m.get('board')!=BOARD or m.get('layout')!=LAYOUT:raise ValueError('Wrong board or partition layout')
    if m.get('size')!=len(b) or m.get('sha256')!=hashlib.sha256(b).hexdigest():raise ValueError('Firmware size/hash mismatch')
    if b[0]!=0xe9 or int.from_bytes(b[12:14],'little')!=9 or b[32:36]!=bytes.fromhex('3254cdab'):raise ValueError('Not an ESP32-S3 application')
    return m,b

def package(image,assets,output,version='0.7.2'):
    b=Path(image).read_bytes();fonts=json.loads(Path(assets).read_text())
    m=dict(format=1,board=BOARD,layout=LAYOUT,version=version,size=len(b),sha256=hashlib.sha256(b).hexdigest(),font_sha256=fonts['sha256'])
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:z.writestr('manifest.json',json.dumps(m,indent=2)+'\n');z.writestr('firmware.bin',b)
    load_package(output);return m

class OtaRejected(ValueError):pass

class Remote:
    def __init__(self,ip,key):
        from cryptography.hazmat.primitives.ciphers.aead import AESGCM
        self.url='http://'+str(ipaddress.ip_address(ip));self.aes=AESGCM(bytes.fromhex(key))
        self.http=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
    def call(self,payload):
        def request(path,data=None):
            raw=None if data is None else json.dumps(data).encode()
            r=urllib.request.Request(self.url+path,raw,{'Content-Type':'application/json'})
            with self.http.open(r,timeout=25) as response:return json.loads(response.read(65536))
        info=request('/api/challenge')
        if info.get('protocol')!='inkos-config-v1':raise ValueError('Unsupported protocol')
        challenge=info['challenge'];bytes.fromhex(challenge)
        if len(challenge)!=32:raise ValueError('Invalid challenge')
        plain=json.dumps(payload,separators=(',',':')).encode()
        if len(plain)>4095:raise ValueError('Command too large')
        aad=lambda direction:f'inkos-config-v1|{challenge}|{direction}'.encode()
        nonce=secrets.token_bytes(12);encrypted=self.aes.encrypt(nonce,plain,aad('request'))
        encode=lambda b:base64.b64encode(b).decode()
        response=request('/api/command',dict(challenge=challenge,nonce=encode(nonce),ciphertext=encode(encrypted[:-16]),tag=encode(encrypted[-16:])))
        decode=lambda k:base64.b64decode(response[k],validate=True)
        result=json.loads(self.aes.decrypt(decode('nonce'),decode('ciphertext')+decode('tag'),aad('response')))
        if not result.get('inkos') or not result.get('ok'):raise OtaRejected(result.get('message','Device rejected OTA command'))
        return result
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs):return None

def install(remote,path,progress=print):
    manifest,b=load_package(path);old=remote.call(dict(cmd='ota_info'))
    for k in ('board','layout','font_sha256'):
        if old.get(k)!=manifest[k]:raise ValueError('Device/package '+k+' mismatch; USB maintenance required')
    if not old.get('healthy'):raise ValueError('Wait for device boot health confirmation')
    start=remote.call(dict(cmd='ota_begin',header=base64.b64encode(b[:288]).decode(),**{k:manifest[k] for k in ('board','layout','font_sha256','size','sha256')}));session=start['session'];slot=start['slot']
    try:
        for offset in range(0,len(b),2560):
            data=b[offset:offset+2560];command=dict(cmd='ota_chunk',session=session,offset=offset,data=base64.b64encode(data).decode())
            for attempt in range(3):
                try:r=remote.call(command);break
                except (OSError,TimeoutError):
                    if attempt==2:raise
            if r['received']!=offset+len(data):raise ValueError('Device byte count mismatch')
            if offset//2560%40==0:progress(f'Uploading {100*r["received"]//len(b)}%')
    except Exception:
        try:remote.call(dict(cmd='ota_abort',session=session))
        except Exception:pass
        raise
    try:remote.call(dict(cmd='ota_finish',session=session))
    except (OSError,TimeoutError):pass # The verified device may already be rebooting.
    progress('Verified. Waiting for restart and boot health confirmation...')
    until=time.monotonic()+150
    while time.monotonic()<until:
        time.sleep(2)
        try:status=remote.call(dict(cmd='ota_info'))
        except (OSError,ValueError,TimeoutError):continue
        if status['boot_id']==old['boot_id']:continue
        if status['slot']!=slot:raise RuntimeError('New firmware failed to boot; device rolled back')
        if status['healthy']:progress('OTA complete: '+slot+' confirmed healthy');return status
    raise TimeoutError('Boot confirmation not received; inspect device before retrying')

def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='action',required=True)
    p=sub.add_parser('pack');p.add_argument('--image',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--version',default='0.7.2')
    p=sub.add_parser('install');p.add_argument('--ip',required=True);p.add_argument('--package',type=Path,required=True)
    args=parser.parse_args()
    if args.action=='pack':print(json.dumps(package(args.image,args.assets,args.output,args.version),indent=2))
    else:install(Remote(args.ip,getpass.getpass('Pairing key (hidden): ').strip()),args.package)
if __name__=='__main__':main()
