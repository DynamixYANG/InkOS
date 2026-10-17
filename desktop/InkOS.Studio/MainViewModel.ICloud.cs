using System.Text.Json;
namespace InkOS.Studio;
public sealed partial class MainViewModel {
 private string icloudUsername="",icloudPassword="",icloudStatus="Connect your Apple Account with an app-specific password.";
 private bool icloudEnabled,icloudHasPassword,icloudFirmware;
 private string storedICloudUsername="";
 private string discoveredICloudUsername="";
 public string ICloudUsername {get=>icloudUsername;set{icloudUsername=value;Notify();}}
 public string ICloudPassword {get=>icloudPassword;set{icloudPassword=value;Notify();}}
 public bool ICloudEnabled {get=>icloudEnabled;set{icloudEnabled=value;Notify();}}
 public string ICloudStatus {get=>icloudStatus;set{icloudStatus=value;Notify();}}
 private IReadOnlyList<ICloudCalendar> icloudCalendars=[];
 public IReadOnlyList<ICloudCalendar> ICloudCalendars {get=>icloudCalendars;set{icloudCalendars=value;Notify();}}
 private ICloudCalendar? selectedICloudCalendar;
 public ICloudCalendar? SelectedICloudCalendar {get=>selectedICloudCalendar;set{selectedICloudCalendar=value;Notify();}}
 public StudioCommand DiscoverICloudCommand {get;private set;}=null!;
 public StudioCommand SaveICloudCommand {get;private set;}=null!;
 public StudioCommand DisconnectICloudCommand {get;private set;}=null!;
 private void InitializeICloudCommands(){
  DiscoverICloudCommand=new(_=>RunAsync(DiscoverICloudAsync),()=>!busy);
  SaveICloudCommand=new(_=>RunAsync(SaveICloudAsync),()=>!busy&&Connected&&icloudFirmware);
  DisconnectICloudCommand=new(_=>RunAsync(DisconnectICloudAsync),()=>!busy&&Connected&&icloudFirmware);
 }
 private async Task ReadICloudAsync(){
  var discovered=ICloudCalendars;
  ICloudPassword="";SelectedICloudCalendar=null;
  if(!icloudFirmware){ICloudStatus="Private iCloud calendars require InkOS 0.7 or later.";return;}
  var value=await Device.SendAsync(new{cmd="icloud_get"},v=>v.TryGetProperty("has_password",out _));
  storedICloudUsername=ICloudUsername=value.GetProperty("username").GetString()??"";
  icloudHasPassword=value.GetProperty("has_password").GetBoolean();ICloudEnabled=value.GetProperty("enabled").GetBoolean();
  string url=value.GetProperty("calendar_url").GetString()??"",name=value.GetProperty("calendar_name").GetString()??"";
  if(url.Length>0){var current=new ICloudCalendar(name,url);ICloudCalendars=discoveredICloudUsername==ICloudUsername&&discovered.Any(c=>c.Url==url)?discovered:[current];SelectedICloudCalendar=ICloudCalendars.First(c=>c.Url==url);}
  else ICloudCalendars=[];
  ICloudStatus=icloudHasPassword?"Credentials stored on the screen. Re-enter the app-specific password to reload the calendar list.":"Enter your Apple Account and an app-specific password, then Load calendars.";
 }
 public async Task DiscoverICloudAsync(){
  string previous=SelectedICloudCalendar?.Url??"";ICloudStatus="Reading calendars from iCloud…";
  using var client=new ICloudCalendarClient();
  try{
   ICloudCalendars=await client.DiscoverAsync(ICloudUsername.Trim(),ICloudPassword.Trim());
   discoveredICloudUsername=ICloudUsername.Trim();ICloudEnabled=true;
   SelectedICloudCalendar=ICloudCalendars.FirstOrDefault(c=>c.Url==previous);
   ICloudStatus=$"{ICloudCalendars.Count} calendars found. Choose one and Save iCloud to screen.";
  }catch{ICloudStatus="Calendar discovery failed. Check the account and app-specific password.";throw;}
 }
 public async Task SaveICloudAsync(){
  string username=ICloudUsername.Trim(),password=ICloudPassword.Trim();
  if(ICloudEnabled&&SelectedICloudCalendar==null)throw new InvalidOperationException("Choose an iCloud calendar first.");
  if(password.Length>0)ICloudCalendarClient.ValidateAccount(username,password);
  else if(ICloudEnabled&&(!icloudHasPassword||username!=storedICloudUsername))throw new InvalidOperationException("Enter an app-specific password for this Apple Account.");
  if(SelectedICloudCalendar!=null)ICloudCalendarClient.ValidateUrl(SelectedICloudCalendar.Url);
  await Device.SaveAsync(new{cmd="icloud_set",enabled=ICloudEnabled,username,password,password_mode=password.Length>0?1:0,calendar_url=SelectedICloudCalendar?.Url??"",calendar_name=SelectedICloudCalendar?.Name??""});
  ICloudPassword="";await ReadICloudAsync();Status="ICLOUD SAVED / The screen fetches only the selected iCloud calendar directly over Wi-Fi.";
 }
 private async Task DisconnectICloudAsync(){await Device.SaveAsync(new{cmd="icloud_disconnect"});await ReadICloudAsync();Status="ICLOUD / Device credentials removed. The Apple app-specific password can be revoked on account.apple.com.";}
}
