"""Temporary loopback-only PKCE + USB provisioning. Not needed during device operation."""
import os
import base64,hashlib,json,secrets,time,urllib.request,urllib.parse,urllib.error,threading
from flask import Flask,request,session,redirect,jsonify,render_template_string,abort
import serial
from waitress import serve
app=Flask(__name__);app.secret_key=secrets.token_bytes(32)
app.config.update(SESSION_COOKIE_HTTPONLY=True,SESSION_COOKIE_SAMESITE='Lax',MAX_CONTENT_LENGTH=8192)
CALLBACK='http://127.0.0.1:8767/callback';PORT=os.environ.get('INKOS_PORT','COM9');usb_lock=threading.Lock()
pending={};connected=False

def send(payload):
    with usb_lock:
        sp=serial.Serial(baudrate=115200,timeout=.25);sp.port=PORT;sp.dtr=True;sp.rts=False
        with sp:
            sp.write((json.dumps(payload,ensure_ascii=False)+'\n').encode())
            until=time.monotonic()+12
            while time.monotonic()<until:
                line=sp.readline().decode('utf-8','replace').strip()
                if not line.startswith('{'):continue
                try:data=json.loads(line)
                except ValueError:continue
                if data.get('inkos') and (data.get('saved') is not None or data.get('wifi') is not None):return data
    raise RuntimeError('USB configuration did not respond. Check the selected USB port and the InkOS firmware.')

@app.before_request
def local_only():
    if request.host!='127.0.0.1:8767':abort(403)
    if request.method=='POST' and request.headers.get('Origin','http://127.0.0.1:8767')!='http://127.0.0.1:8767':abort(403)

def csrf():
    if not secrets.compare_digest(str(request.form.get('csrf','')),str(session.get('csrf','missing'))):abort(403)

PAGE='''<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>InkOS Setup</title><style>body{background:#f6f5f0;color:#181818;font:16px/1.65 ui-monospace,monospace;margin:0}main{max-width:640px;margin:40px auto;padding:30px;background:white;border:1px solid #bbb}h1{font-weight:400}input{display:block;box-sizing:border-box;width:100%;padding:12px;font:inherit;margin:8px 0 16px;border:1px solid #777}button{font:inherit;padding:12px 20px;background:#111;color:white;border:0;cursor:pointer}section{border-top:1px solid #ccc;margin-top:28px;padding-top:20px}code{word-break:break-all}a{color:inherit}.message{padding:14px;border:1px solid #777}</style><main><h1>InkOS / Device setup</h1><p>Standalone ESP32 firmware · USB {{port}}</p>{%if message%}<p class=message>{{message}}</p>{%endif%}<section><h2>Spotify / One-time authorization</h2><ol><li>Create a <b>Web API</b> app in the <a href=https://developer.spotify.com/dashboard target=_blank rel=noopener>Spotify Developer Dashboard</a>, using your Premium account.</li><li>Register this exact Redirect URI:<br><code>{{callback}}</code></li><li>Copy your app's Client ID below. No Client Secret is required (PKCE).</li></ol><form method=post action=/connect><input type=hidden name=csrf value="{{csrf}}"><label>Client ID<input name=client_id pattern="[a-fA-F0-9]{32}" minlength=32 maxlength=32 required autocomplete=off></label><button>Authorize on Spotify</button></form><p>Authorization opens on accounts.spotify.com. The refresh token is sent directly over USB to the ESP32 and is not saved to a computer file. Authorization may need to be repeated if access expires or is revoked.</p></section><section><h2>Wi-Fi / Optional update</h2><p>Existing Wi-Fi settings are retained. Use this form only to change them.</p><form method=post action=/wifi><input type=hidden name=csrf value="{{csrf}}"><label>2.4 GHz SSID<input name=ssid placeholder="Your 2.4 GHz network" required maxlength=32></label><label>Password<input type=password name=password maxlength=63 autocomplete=new-password></label><button>Save Wi-Fi to device</button></form></section><p>This helper can be closed after setup. Weather, clocks and music then run on the device. Logins and tokens are never requested in chat.</p></main></html>'''

@app.get('/')
def index():
    session.setdefault('csrf',secrets.token_urlsafe(24))
    return render_template_string(PAGE,csrf=session['csrf'],callback=CALLBACK,port=PORT,message=session.pop('message',''))

@app.post('/connect')
def connect():
    csrf();client=request.form.get('client_id','').strip()
    if len(client)!=32 or any(c not in '0123456789abcdefABCDEF' for c in client):abort(400)
    state=secrets.token_urlsafe(32);verifier=secrets.token_urlsafe(64)
    pending.clear();pending[state]={'client':client,'verifier':verifier,'expires':time.monotonic()+600};session['state']=state
    challenge=base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b'=').decode()
    query=urllib.parse.urlencode({'client_id':client,'response_type':'code','redirect_uri':CALLBACK,'state':state,'code_challenge':challenge,'code_challenge_method':'S256','scope':'user-read-currently-playing user-read-playback-state'})
    return redirect('https://accounts.spotify.com/authorize?'+query)

@app.get('/callback')
def callback():
    global connected
    state=request.args.get('state','');expected=session.pop('state','')
    item=pending.pop(state,None)
    if not expected or not secrets.compare_digest(state,expected) or not item or time.monotonic()>item['expires']:abort(400,'Authorization expired or state did not match; start again.')
    if request.args.get('error'):session['message']='Spotify authorization was not completed.';return redirect('/')
    code=request.args.get('code','')
    try:
        form=urllib.parse.urlencode({'client_id':item['client'],'grant_type':'authorization_code','code':code,'redirect_uri':CALLBACK,'code_verifier':item['verifier']}).encode()
        req=urllib.request.Request('https://accounts.spotify.com/api/token',data=form,headers={'Content-Type':'application/x-www-form-urlencoded'})
        with urllib.request.urlopen(req,timeout=15) as response:tokens=json.load(response)
        token=tokens.get('refresh_token')
        if not token:raise RuntimeError('Spotify did not issue a refresh token.')
        result=send({'inkos':True,'cmd':'spotify','client_id':item['client'],'refresh_token':token})
        connected=bool(result.get('saved'));session['message']='Spotify authorization saved to the screen. Start music on your phone or speaker.' if connected else 'The screen did not save the authorization.'
    except urllib.error.HTTPError as e:session['message']=f'Spotify token exchange failed (HTTP {e.code}). Check your developer app settings.'
    except Exception:session['message']='Authorization could not be transferred. Check USB, then authorize again.'
    return redirect('/')

@app.post('/wifi')
def wifi():
    csrf()
    try:
        r=send({'inkos':True,'cmd':'wifi','ssid':request.form.get('ssid',''),'password':request.form.get('password','')})
        session['message']='Wi-Fi saved to the screen.' if r.get('saved') else 'Wi-Fi was not saved.'
    except Exception:session['message']='USB did not respond. Check the screen connection.'
    return redirect('/')

@app.get('/healthz')
def health():return jsonify(ok=True,service='inkos-temporary-setup',authorized_in_this_session=connected)

if __name__=='__main__':
    import logging
    logging.getLogger('waitress').setLevel(logging.WARNING)
    serve(app,host='127.0.0.1',port=8767,threads=4)
