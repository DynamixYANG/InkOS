using System.Diagnostics;
namespace InkOS.Studio;
public sealed partial class MainViewModel {
 private CancellationTokenSource? authorization;private bool busy;
 public bool IsBusy=>busy;
 public string ClientId{get;set;}="";
 public string RedirectUri=>SpotifyAuthorization.RedirectUri;
 public string ConnectionLabel=>Connected?$"CONNECTED / {(UseNetwork?"IP":SelectedPort)}":$"{(UseNetwork?"IP":"USB")} / DISCONNECTED";
 public StudioCommand ConnectCommand{get;private set;}=null!;
 public StudioCommand RefreshPortsCommand{get;private set;}=null!;
 public StudioCommand SaveCommand{get;private set;}=null!;
 public StudioCommand NavigateCommand{get;private set;}=null!;
 public StudioCommand AuthorizeCommand{get;private set;}=null!;
 public StudioCommand CancelAuthorizationCommand{get;private set;}=null!;
 public StudioCommand DisconnectSpotifyCommand{get;private set;}=null!;
 public StudioCommand OpenLinkCommand{get;private set;}=null!;
 private StudioCommand[] commands=[];
 private void InitializeCommands(){
  ConnectCommand=new(_=>RunAsync(ConnectAsync),()=>!busy);
  RefreshPortsCommand=new(_=>RunAsync(()=>{RefreshPorts();return Task.CompletedTask;}),()=>!busy);
  SaveCommand=new(_=>RunAsync(SaveAsync),()=>!busy&&Connected);
  NavigateCommand=new(p=>RunAsync(async()=>{await Device.NavigateAsync((string)p!);Status="Page changed.";}),()=>!busy&&Connected);
  AuthorizeCommand=new(_=>RunAsync(async()=>{authorization?.Dispose();authorization=new(TimeSpan.FromMinutes(5));RefreshCommands();await AuthorizeAsync(ClientId,authorization.Token);}),()=>!busy&&Connected);
  CancelAuthorizationCommand=new(_=>{authorization?.Cancel();return Task.CompletedTask;},()=>busy&&authorization!=null);
  DisconnectSpotifyCommand=new(_=>RunAsync(DisconnectSpotifyAsync),()=>!busy&&Connected);
  OpenLinkCommand=new(p=>RunAsync(()=>{var url=(string)p!;if(!Uri.TryCreate(url,UriKind.Absolute,out var uri)||uri.Scheme!="https")throw new InvalidOperationException("Invalid guide URL.");Process.Start(new ProcessStartInfo(url){UseShellExecute=true});return Task.CompletedTask;}),()=>!busy);
  InitializeNetworkCommands();
  InitializeFirmwareCommands();
  InitializeICloudCommands();
  var logCommands=InitializeLogCommands();
  commands=[..logCommands,DiscoverICloudCommand,SaveICloudCommand,DisconnectICloudCommand,UpgradeFirmwareCommand,SaveWifiCommand,PairNetworkCommand,DisableNetworkCommand,ConnectCommand,RefreshPortsCommand,SaveCommand,NavigateCommand,AuthorizeCommand,CancelAuthorizationCommand,DisconnectSpotifyCommand,OpenLinkCommand];
 }
 private async Task RunAsync(Func<Task> work){
  if(busy)return;busy=true;RefreshCommands();
  try{await work();}
  catch(OperationCanceledException){Status="Authorization cancelled or timed out.";}
  catch(Exception ex){Status=ex is InvalidOperationException or TimeoutException?ex.Message:"The operation failed. Check USB, the network and app settings.";}
  finally{authorization?.Dispose();authorization=null;busy=false;RefreshCommands();}
 }
 private void RefreshCommands(){Notify(nameof(IsBusy));Notify(nameof(LogsAvailable));Notify(nameof(CanSaveLog));Notify(nameof(CanChooseFirmware));Notify(nameof(Connected));Notify(nameof(ConnectionLabel));foreach(var c in commands)c.Refresh();}
}
