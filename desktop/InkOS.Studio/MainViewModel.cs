using System.Collections.ObjectModel;
using System.ComponentModel;
using System.IO.Ports;
using System.Runtime.CompilerServices;
using System.Text.Json;
namespace InkOS.Studio;
public sealed partial class MainViewModel:INotifyPropertyChanged,IDisposable {
 public event PropertyChangedEventHandler? PropertyChanged;
 private void Notify([CallerMemberName]string? n=null)=>PropertyChanged?.Invoke(this,new(n));
 public DeviceClient Device {get;}=new();
 public string StudioVersion=>"STUDIO / "+typeof(MainViewModel).Assembly.GetName().Version!.ToString(3);
 public ObservableCollection<string> Ports {get;}=[];
 public string SelectedPort {get;set;}="COM9";
 public string[] Zones=>Catalog.Zones;public Place[] Places=>Catalog.Places;
 private DisplaySettings settings=new();public DisplaySettings Settings{get=>settings;set{settings=value;LatitudeText=value.latitude.ToString(System.Globalization.CultureInfo.InvariantCulture);LongitudeText=value.longitude.ToString(System.Globalization.CultureInfo.InvariantCulture);PageSwitchDelayText=(value.page_switch_delay_ms/1000m).ToString("0.###",System.Globalization.CultureInfo.InvariantCulture);EncoderLockText=(value.encoder_lock_ms/1000m).ToString("0.###",System.Globalization.CultureInfo.InvariantCulture);Notify();Notify(nameof(LatitudeText));Notify(nameof(LongitudeText));Notify(nameof(PageSwitchDelayText));Notify(nameof(EncoderLockText));}}
 public string PageSwitchDelayText{get;set;}="0.5";
 public string EncoderLockText{get;set;}="2";
 internal static bool SupportsFirmware(string? version)=>Version.TryParse(version,out var v)&&v.Major==0&&v.Minor>=3&&v.Minor<=7;
 public string LatitudeText{get;set;}="65.0121";public string LongitudeText{get;set;}="25.4651";
 private List<CalendarFeed> feeds=[new(){name="Personal",provider="Apple"},new(){name="Google",provider="Google"},new(){name="Work",provider="Outlook"}];
 public List<CalendarFeed> Feeds{get=>feeds;set{feeds=value;Notify();}}
 private string status="Connect over USB or paired IP to read the screen settings.";public string Status{get=>status;set{status=value;Notify();}}
 private string spotify="Not checked";public string SpotifyStatus{get=>spotify;set{spotify=value;Notify();}}
 private bool deviceValidated;
 public bool Connected=>deviceValidated&&Device.Connected;
 public MainViewModel(){InitializeCommands();RefreshPorts();}
 public void RefreshPorts(){Ports.Clear();foreach(var p in SerialPort.GetPortNames().Order())Ports.Add(p);Notify(nameof(SelectedPort));}
 public async Task ConnectAsync(){
  deviceValidated=false;
  try{
   if(UseNetwork)Device.OpenNetwork(NetworkAddress,PairingKey);else Device.Open(SelectedPort);
   var v=await Device.StatusAsync();
   if(!v.TryGetProperty("version",out var ver)||!SupportsFirmware(ver.GetString()))throw new InvalidOperationException("This Studio version needs InkOS 0.3–0.7 firmware.");
   icloudFirmware=Version.Parse(ver.GetString()!).Minor>=7;
   modernFirmware=!ver.GetString()!.StartsWith("0.3.");
   if(UseUsb&&v.TryGetProperty("ip",out var ip))NetworkAddress=ip.GetString()??"";
   await ReloadAsync();deviceValidated=true;await DetectLogsAsync(v);Status=$"CONNECTED / {(UseNetwork?NetworkAddress:SelectedPort)} / InkOS {ver.GetString()}";
  }
  catch{Device.Dispose();throw;}
  finally{Notify(nameof(Connected));Notify(nameof(ConnectionLabel));}
 }
 public async Task ReloadAsync(){var s=await Device.ReadSettingsAsync();Settings=s.GetProperty("settings").Deserialize<DisplaySettings>()??new();Feeds=s.GetProperty("feeds").Deserialize<List<CalendarFeed>>()??[];while(Feeds.Count<3)Feeds.Add(new());Energy=s.GetProperty("energy").Deserialize<EnergySettings>()??new();var st=await Device.StatusAsync();SpotifyStatus=st.GetProperty("spotify").GetBoolean()?"Account connected":"No Spotify account connected";await ReadWifiAsync();await ReadICloudAsync();}
 public async Task SaveAsync(){
  ValidatePageSwitchDelay();
  ValidateEncoderLock();
  if(!double.TryParse(LatitudeText,System.Globalization.NumberStyles.Float,System.Globalization.CultureInfo.InvariantCulture,out var lat)||!double.TryParse(LongitudeText,System.Globalization.NumberStyles.Float,System.Globalization.CultureInfo.InvariantCulture,out var lon))throw new InvalidOperationException("Enter coordinates as numbers, using a decimal point.");
  Settings.latitude=lat;Settings.longitude=lon;
  if(!double.IsFinite(Settings.latitude)||!double.IsFinite(Settings.longitude)||Math.Abs(Settings.latitude)>90||Math.Abs(Settings.longitude)>180)throw new InvalidOperationException("Check latitude and longitude.");
  if(!Catalog.Zones.Contains(Settings.zone)||Settings.worlds.Count!=2||Settings.worlds.Any(w=>!Catalog.Zones.Contains(w.zone)||string.IsNullOrWhiteSpace(w.label)))throw new InvalidOperationException("Choose valid time zones and clock labels.");
  for(int i=0;i<3;i++){var f=Feeds[i];var u=f.url.Trim();if(u.StartsWith("webcal://",StringComparison.OrdinalIgnoreCase))u="https://"+u[9..];if(u.Length>1500||f.enabled&&(!Uri.TryCreate(u,UriKind.Absolute,out var uri)||uri.Scheme!="https"))throw new InvalidOperationException($"Calendar {i+1}: paste an HTTPS or webcal ICS URL.");f.url=u;}
  ValidateEnergy();
  await Device.SaveAsync(new{cmd="settings_set",settings=Settings});
  for(int i=0;i<3;i++){var f=Feeds[i];f.provider=new[]{"Apple","Google","Outlook"}[i];await Device.SaveAsync(new{cmd="feed_set",index=i,name=f.name,provider=f.provider,url=f.url,enabled=f.enabled});}
  await SaveEnergyAsync();
  Status="SAVED / Weather, calendars and electricity update on the screen.";
 }
 internal void ValidatePageSwitchDelay(){
  if(!decimal.TryParse(PageSwitchDelayText,System.Globalization.NumberStyles.AllowDecimalPoint,System.Globalization.CultureInfo.InvariantCulture,out var seconds)||seconds<0.1m||seconds>5m||seconds*1000!=decimal.Truncate(seconds*1000))throw new InvalidOperationException("Page selection delay: enter 0.1–5 seconds (up to three decimal places).");
  Settings.page_switch_delay_ms=(int)(seconds*1000);
 }
 internal void ValidateEncoderLock(){
  if(!decimal.TryParse(EncoderLockText,System.Globalization.NumberStyles.AllowDecimalPoint,System.Globalization.CultureInfo.InvariantCulture,out var seconds)||seconds<0m||seconds>10m||seconds*1000!=decimal.Truncate(seconds*1000))throw new InvalidOperationException("Encoder lock: enter 0-10 seconds (up to three decimal places). Zero disables the lock.");
  Settings.encoder_lock_ms=(int)(seconds*1000);
 }
 public async Task AuthorizeAsync(string clientId,CancellationToken ct){Status="Authorize in your browser, then return here.";string token=await SpotifyAuthorization.AuthorizeAsync(clientId.Trim(),ct);await Device.SaveAsync(new{cmd="spotify",client_id=clientId.Trim(),refresh_token=token});SpotifyStatus="Authorization saved; start music on your phone or speaker.";Status="SPOTIFY / Saved directly to the screen.";}
 public async Task DisconnectSpotifyAsync(){await Device.SaveAsync(new{cmd="spotify_disconnect"});SpotifyStatus="Account disconnected from this screen";Status="SPOTIFY / Device credentials removed.";}
 public void ApplyPlace(Place p){Settings.location=p.Name;Settings.latitude=p.Latitude;Settings.longitude=p.Longitude;Settings.zone=p.Zone;LatitudeText=p.Latitude.ToString(System.Globalization.CultureInfo.InvariantCulture);LongitudeText=p.Longitude.ToString(System.Globalization.CultureInfo.InvariantCulture);Notify(nameof(Settings));Notify(nameof(LatitudeText));Notify(nameof(LongitudeText));}
 public void Dispose(){LiveTail=false;authorization?.Cancel();authorization?.Dispose();Device.Dispose();}
}
