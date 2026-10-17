using System.Text;
namespace InkOS.Studio;
public sealed partial class MainViewModel {
 private int connectionMode;
 public int ConnectionMode { get=>connectionMode;set { if(connectionMode==value)return;connectionMode=value;Device.Dispose();deviceValidated=false;Notify();Notify(nameof(UseNetwork));Notify(nameof(UseUsb));Notify(nameof(Connected));RefreshCommands(); } }
 public bool UseNetwork=>ConnectionMode==1;
 public bool UseUsb=>!UseNetwork;
 private string networkAddress="";
 public string NetworkAddress {get=>networkAddress;set{if(networkAddress==value)return;networkAddress=value;InvalidateNetworkConnection();Notify();}}
 private string pairingKey="";
 public string PairingKey {get=>pairingKey;set{if(pairingKey==value)return;pairingKey=value;InvalidateNetworkConnection();Notify();}}
 private void InvalidateNetworkConnection(){if(UseNetwork&&deviceValidated){Device.Dispose();deviceValidated=false;Status="Connection changed. Click Connect / Read before saving.";RefreshCommands();}}
 private bool showPairingKey;
 public bool ShowPairingKey {get=>showPairingKey;set{showPairingKey=value;Notify();Notify(nameof(PairingKeyMask));}}
 public char PairingKeyMask=>ShowPairingKey?'\0':'●';
 private string wifiSsid="",wifiPassword="";
 public string WifiSsid {get=>wifiSsid;set{wifiSsid=value;Notify();}}
 public string WifiPassword {get=>wifiPassword;set{wifiPassword=value;Notify();}}
 public int WifiPasswordMode {get;set;}
 private string wifiState="Read the device to view its Wi-Fi configuration.";
 public string WifiState {get=>wifiState;set{wifiState=value;Notify();}}
 private string currentSsid="";
 private bool modernFirmware;
 public StudioCommand SaveWifiCommand{get;private set;}=null!;
 public StudioCommand PairNetworkCommand{get;private set;}=null!;
 public StudioCommand DisableNetworkCommand{get;private set;}=null!;
 private void InitializeNetworkCommands(){
  SaveWifiCommand=new(_=>RunAsync(SaveWifiAsync),()=>!busy&&Connected&&modernFirmware);
  PairNetworkCommand=new(_=>RunAsync(()=>PairNetworkAsync(true)),()=>!busy&&Connected&&UseUsb&&modernFirmware);
  DisableNetworkCommand=new(_=>RunAsync(()=>PairNetworkAsync(false)),()=>!busy&&Connected&&UseUsb&&modernFirmware);
 }
 private async Task ReadWifiAsync(){
  if(!modernFirmware){WifiState="Update the screen to InkOS 0.4 for Wi-Fi and IP setup.";return;}
  var v=await Device.SendAsync(new{cmd="wifi_get"},x=>x.TryGetProperty("ssid",out _));
  currentSsid=WifiSsid=v.GetProperty("ssid").GetString()??"";
  bool has=v.GetProperty("has_password").GetBoolean();
  WifiPassword="";WifiPasswordMode=has?0:2;Notify(nameof(WifiPasswordMode));
  WifiState=has?"Password stored on device. It is never read back.":"No stored password / open network.";
 }
 internal static void ValidateWifi(string ssid,string password,int mode,string current){
  int ssidBytes=Encoding.UTF8.GetByteCount(ssid),passwordBytes=Encoding.UTF8.GetByteCount(password);
  if(ssidBytes<1||ssidBytes>32)throw new InvalidOperationException("Wi-Fi name must contain 1-32 UTF-8 bytes.");
  if(mode<0||mode>2)throw new InvalidOperationException("Choose a Wi-Fi password action.");
  if(mode==0&&ssid!=current)throw new InvalidOperationException("Choose Replace password when changing the Wi-Fi network.");
  if(mode==1&&(passwordBytes<8||passwordBytes>63))throw new InvalidOperationException("Wi-Fi password must contain 8-63 UTF-8 bytes.");
 }
 private async Task SaveWifiAsync(){
  ValidateWifi(WifiSsid,WifiPassword,WifiPasswordMode,currentSsid);
  await Device.SaveAsync(new{cmd="wifi",ssid=WifiSsid,password=WifiPasswordMode==1?WifiPassword:"",password_mode=WifiPasswordMode});
  WifiPassword="";
  if(UseNetwork){Device.Dispose();deviceValidated=false;Status="WI-FI SAVED / Reconnecting to Wi-Fi. Connect again using the device's new IP, or USB.";}
  else {await ReadWifiAsync();Status="WI-FI SAVED / The screen is reconnecting. Read again to obtain its new IP.";}
 }
 private async Task PairNetworkAsync(bool enable){
  var v=await Device.SendAsync(new{cmd="remote_pair",enabled=enable},x=>x.TryGetProperty("saved",out _));
  if(!v.GetProperty("saved").GetBoolean())throw new InvalidOperationException("Could not update IP pairing.");
  PairingKey=enable?v.GetProperty("pairing_key").GetString()!:"";
  Status=enable?"PAIRED / Keep the key private. Select IP, enter the device address, then Connect / Read.":"IP ACCESS DISABLED / Existing pairing keys revoked.";
 }
}
