import importlib.util,unittest,urllib.parse
from pathlib import Path
spec=importlib.util.spec_from_file_location('ink_setup',Path(__file__).resolve().parents[1]/'tools/setup.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class SetupTests(unittest.TestCase):
    def setUp(self):
        module.app.config['TESTING']=True;self.client=module.app.test_client();module.pending.clear()
    def get(self,url):return self.client.get(url,base_url='http://127.0.0.1:8767')
    def post(self,url,data):return self.client.post(url,data=data,base_url='http://127.0.0.1:8767')
    def token(self):
        self.get('/')
        with self.client.session_transaction(base_url='http://127.0.0.1:8767') as sess:return sess['csrf']
    def test_loopback_host_only(self):self.assertEqual(self.client.get('/',base_url='http://192.168.1.2:8767').status_code,403)
    def test_csrf_required(self):self.assertEqual(self.post('/connect',{'client_id':'0'*32}).status_code,403)
    def test_invalid_client_id(self):self.assertEqual(self.post('/connect',{'csrf':self.token(),'client_id':'invalid'}).status_code,400)
    def test_official_pkce_redirect(self):
        response=self.post('/connect',{'csrf':self.token(),'client_id':'0'*32});self.assertEqual(response.status_code,302)
        url=urllib.parse.urlsplit(response.location);self.assertEqual(url.netloc,'accounts.spotify.com')
        args=urllib.parse.parse_qs(url.query);self.assertEqual(args['code_challenge_method'],['S256']);self.assertEqual(len(args['code_challenge'][0]),43);self.assertNotIn('client_secret',args)
        self.assertEqual(args['redirect_uri'],['http://127.0.0.1:8767/callback'])
    def test_callback_state_required(self):self.assertEqual(self.get('/callback?code=fixture&state=wrong').status_code,400)
    def test_declined_authorization(self):
        response=self.post('/connect',{'csrf':self.token(),'client_id':'0'*32});state=urllib.parse.parse_qs(urllib.parse.urlsplit(response.location).query)['state'][0]
        result=self.get('/callback?error=access_denied&state='+state);self.assertEqual(result.status_code,302);self.assertEqual(len(module.pending),0)
if __name__=='__main__':unittest.main()
