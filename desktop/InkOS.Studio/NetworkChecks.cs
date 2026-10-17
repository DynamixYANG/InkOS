using System.Net;
using System.Net.Http;
using System.Text;
using System.Security.Cryptography;
using System.Text.Json;
namespace InkOS.Studio;
internal static class NetworkChecks {
 public static void ValidateInputs() {
  MainViewModel.ValidateWifi("Example","12345678",1,"");
  MainViewModel.ValidateWifi("Example","",0,"Example");
  MainViewModel.ValidateWifi("Open","",2,"");
  foreach(var action in new Action[]{
   ()=>MainViewModel.ValidateWifi("","12345678",1,""),
   ()=>MainViewModel.ValidateWifi("Other","",0,"Example"),
   ()=>MainViewModel.ValidateWifi("Example","short",1,""),
   ()=>MainViewModel.ValidateWifi(new string('界',11),"12345678",1,"")}) {
    try {action();throw new Exception("Invalid Wi-Fi input accepted");}catch(InvalidOperationException){}
  }
  foreach(var address in new[]{"192.168.1.100","http://192.168.1.100:80","http://[::1]"})NetworkTransport.ParseAddress(address);
  foreach(var address in new[]{"https://192.168.1.100","example.com","http://user:pass@192.168.1.100","http://192.168.1.100/path"}) {
   try{NetworkTransport.ParseAddress(address);throw new Exception("Invalid IP accepted");}catch(InvalidOperationException){}
  }
  using var model=new MainViewModel();if(model.SaveWifiCommand.CanExecute(null)||model.PairNetworkCommand.CanExecute(null))throw new Exception("Network command gating failed");
 }
 // Explicit hardware check. Credentials and subscription responses stay in memory.
 public static async Task<int> DeviceSmokeAsync() {
  using var usb=new DeviceClient();usb.Open("COM9");
  var status=await usb.StatusAsync();string address=status.GetProperty("ip").GetString()!;
  var before=await usb.ReadSettingsAsync();
  var wifi=await usb.SendAsync(new{cmd="wifi_get"},x=>x.TryGetProperty("ssid",out _));
  var pair=await usb.SendAsync(new{cmd="remote_pair",enabled=true},x=>x.TryGetProperty("saved",out _));
  if(!pair.GetProperty("saved").GetBoolean())throw new Exception("USB pairing failed");
  string key=pair.GetProperty("pairing_key").GetString()!;
  try {
   using var model=new MainViewModel{ConnectionMode=1,NetworkAddress=address,PairingKey=key};
   await model.ConnectAsync();if(!model.Connected||!model.SaveCommand.CanExecute(null)||model.PairNetworkCommand.CanExecute(null))throw new Exception("IP view model connection failed");
   var networkRead=await model.Device.ReadSettingsAsync();
   if(before.GetRawText()!=networkRead.GetRawText())throw new Exception("USB/IP settings differ");
   await model.SaveAsync();await model.ReloadAsync();
   var after=await model.Device.ReadSettingsAsync();
   // Normalized provider metadata is the only allowed compatibility change.
   for(int i=0;i<3;i++) {
    if(before.GetProperty("feeds")[i].GetProperty("url").GetString()!=after.GetProperty("feeds")[i].GetProperty("url").GetString())throw new Exception("Calendar URL changed");
   }
   if(before.GetProperty("settings").GetRawText()!=after.GetProperty("settings").GetRawText()||before.GetProperty("energy").GetRawText()!=after.GetProperty("energy").GetRawText())throw new Exception("Configuration changed on round trip");
   await model.Device.NavigateAsync("prev");await model.Device.NavigateAsync("next");await model.Device.NavigateAsync("home");
   if((await model.Device.StatusAsync()).GetProperty("page").GetInt32()!=0)throw new Exception("IP navigation failed");
   var forbidden=await model.Device.SendAsync(new{cmd="remote_pair",enabled=false},x=>x.TryGetProperty("saved",out _));
   if(forbidden.GetProperty("saved").GetBoolean())throw new Exception("Pairing was allowed over IP");
   using(var wrong=new NetworkTransport(address,new string(key[0]=='0'?'1':'0',64))) {
    try{await wrong.SendAsync(new{cmd="status"},_=>true);throw new Exception("Wrong key accepted");}catch(InvalidOperationException){}
   }
   using var http=new HttpClient(new HttpClientHandler{UseProxy=false}){BaseAddress=NetworkTransport.ParseAddress(address)};
   using var codec=new RemoteCodec(key);
   using var info=JsonDocument.Parse(await http.GetStringAsync("api/challenge"));
   string challenge=info.RootElement.GetProperty("challenge").GetString()!;
   var envelope=codec.Encrypt(new{cmd="status"},challenge);
   using(var response=await http.PostAsync("api/command",new StringContent(envelope.GetRawText(),Encoding.UTF8,"application/json"))) {
    response.EnsureSuccessStatusCode();using var data=JsonDocument.Parse(await response.Content.ReadAsStringAsync());
    var decoded=codec.Decrypt(data.RootElement,challenge);if(!decoded.GetProperty("inkos").GetBoolean())throw new Exception("Encrypted response invalid");
    try{codec.Decrypt(data.RootElement,new string('f',32));throw new Exception("Wrong challenge accepted");}catch(CryptographicException){}
   }
   using(var replay=await http.PostAsync("api/command",new StringContent(envelope.GetRawText(),Encoding.UTF8,"application/json")))if(replay.StatusCode!=HttpStatusCode.Unauthorized)throw new Exception("Replay was accepted");
   var invalid=await model.Device.SendAsync(new{cmd="wifi",ssid="",password_mode=0},x=>x.TryGetProperty("saved",out _));
   if(invalid.GetProperty("saved").GetBoolean())throw new Exception("Invalid Wi-Fi saved");
   // Re-save the real current network with password_mode=keep; never read the password.
   await model.Device.SaveAsync(new{cmd="wifi",ssid=wifi.GetProperty("ssid").GetString(),password_mode=wifi.GetProperty("has_password").GetBoolean()?0:2});
   var wifiAfter=await usb.SendAsync(new{cmd="wifi_get"},x=>x.TryGetProperty("ssid",out _));
   if(wifi.GetRawText()!=wifiAfter.GetRawText())throw new Exception("Wi-Fi metadata changed");
   model.NetworkAddress="192.0.2.1";
   if(model.Connected||model.SaveCommand.CanExecute(null))throw new Exception("Editing endpoint left the old device writable");
   File.WriteAllText("studio-network-smoke.json",JsonSerializer.Serialize(new{passed=true,usb_ip_settings_equal=true,viewmodel_roundtrip=true,navigation=true,pairing_usb_only=true,wrong_key_rejected=true,replay_rejected=true,response_challenge_bound=true,invalid_wifi_rejected=true,wifi_keep_roundtrip=true,credentials_logged=false}));
   return 0;
  } finally {await usb.SaveAsync(new{cmd="remote_pair",enabled=false});}
 }
}
