namespace InkOS.Studio;
public sealed partial class MainViewModel {
 private FirmwarePackage? firmware;
 private string firmwareLabel="Choose an InkOS OTA ZIP. First-time A/B setup requires USB maintenance.";
 public string FirmwareLabel{get=>firmwareLabel;private set{firmwareLabel=value;Notify();}}
 private string firmwareState="No firmware package selected.";
 public string FirmwareState{get=>firmwareState;private set{firmwareState=value;Notify();}}
 private int firmwareProgress;
 public int FirmwareProgress{get=>firmwareProgress;private set{firmwareProgress=value;Notify();}}
 public bool CanChooseFirmware=>!busy;
 public StudioCommand UpgradeFirmwareCommand{get;private set;}=null!;
 private void InitializeFirmwareCommands(){UpgradeFirmwareCommand=new(_=>RunAsync(UpgradeFirmwareAsync),()=>!busy&&Connected&&UseNetwork&&firmware!=null);}
 public void ChooseFirmware(string path){
  try{firmware=FirmwarePackage.Load(path);FirmwareLabel=$"InkOS {firmware.Version} / {firmware.Image.Length/1024} KiB / SHA-256 verified";FirmwareState="Ready. Upgrade writes the inactive slot, then restarts automatically.";FirmwareProgress=0;}
  catch{firmware=null;FirmwareLabel="Invalid OTA package.";FirmwareState="Choose the app-only InkOS OTA ZIP for this hardware.";}
  RefreshCommands();
 }
 private async Task UpgradeFirmwareAsync(){
  if(firmware==null||!UseNetwork)throw new InvalidOperationException("Connect using the screen IP and select an OTA package first.");
  var state=await FirmwareUpdater.Install(Device,firmware,(percent,text)=>{FirmwareProgress=percent;FirmwareState=text;});
  await ReloadAsync();Status=$"UPDATED / InkOS {state.GetProperty("version").GetString()} / healthy";
 }
}
