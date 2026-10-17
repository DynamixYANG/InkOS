using System.Collections.ObjectModel;
using System.Text;
using System.Text.Json;
using Avalonia.Threading;
namespace InkOS.Studio;
public sealed partial class MainViewModel {
 public ObservableCollection<LogFileItem> LogFiles{get;}=[];
 /// <summary>Bound to the file list's SelectedItems (multi-select).</summary>
 public ObservableCollection<object> SelectedLogs{get;}=[];
 private bool storageFirmware;public bool StorageAvailable=>LogsAvailable&&storageFirmware;
 private StorageInfo? storage;public StorageInfo? Storage{get=>storage;private set{storage=value;Notify();Notify(nameof(StorageSegments));UpdateStorageText();}}
 public StorageSegment[] StorageSegments=>storage==null?[]:LogStorage.Segments(storage);
 private string storageSummary="Storage status needs InkOS 0.7.8 or newer.";public string StorageSummary{get=>storageSummary;private set{storageSummary=value;Notify();}}
 private string storageBreakdown="";public string StorageBreakdown{get=>storageBreakdown;private set{storageBreakdown=value;Notify();}}
 private string fontsSummary="";public string FontsSummary{get=>fontsSummary;private set{fontsSummary=value;Notify();}}
 private string capacityText="";public string CapacityText{get=>capacityText;private set{capacityText=value;Notify();}}
 private string capacityWarning="";public string CapacityWarning{get=>capacityWarning;private set{capacityWarning=value;Notify();Notify(nameof(HasCapacityWarning));}}
 public bool HasCapacityWarning=>capacityWarning.Length>0;
 public string[] RetentionLabels=>LogStorage.RetentionLabels;
 private int retentionDays=35;
 /// <summary>Dropdown index; changing it sends logging_set retention_days immediately.</summary>
 public int RetentionIndex{get=>Math.Max(0,Array.IndexOf(LogStorage.RetentionDays,retentionDays));set{if(value<0||value>=LogStorage.RetentionDays.Length||LogStorage.RetentionDays[value]==retentionDays)return;retentionDays=LogStorage.RetentionDays[value];Notify();UpdateStorageText();if(StorageAvailable)SetRetentionCommand.Execute(retentionDays);}}
 public int LogRetentionDays=>retentionDays;
 private bool confirmDelete;public bool ConfirmDelete{get=>confirmDelete;set{confirmDelete=value;Notify();Notify(nameof(DeletePrompt));}}
 public string DeletePrompt=>$"Delete {SelectedLogs.Count} selected file{(SelectedLogs.Count==1?"":"s")}? This cannot be undone.";
 public StudioCommand DeleteSelectedLogsCommand{get;private set;}=null!;
 public StudioCommand ConfirmDeleteLogsCommand{get;private set;}=null!;
 public StudioCommand CancelDeleteLogsCommand{get;private set;}=null!;
 public StudioCommand SetRetentionCommand{get;private set;}=null!;
 private bool logsFirmware,logPreview;public bool LogsAvailable=>(Connected||logPreview)&&logsFirmware;
 private void UpdateStorageText(){
  if(storage is not {} s){StorageSummary="Storage status needs InkOS 0.7.8 or newer.";StorageBreakdown=FontsSummary=CapacityText=CapacityWarning="";return;}
  StorageSummary=$"LittleFS {LogArchive.Size(s.Used)} used of {LogArchive.Size(s.Total)} · {LogArchive.Size(s.Free)} free · {LogArchive.Size(s.Reserve)} always kept free";
  StorageBreakdown=$"Logs {LogArchive.Size(s.Logs)} ({s.ArchiveCount} archive{(s.ArchiveCount==1?"":"s")} {LogArchive.Size(s.ArchiveBytes)} + current.log {LogArchive.Size(s.CurrentBytes)}) · Calendars {LogArchive.Size(s.Calendars)} · Energy {LogArchive.Size(s.Energy)} · Other + filesystem {LogArchive.Size(s.Other+s.Overhead)}";
  FontsSummary=$"Fonts / assets: {LogArchive.Size(s.FontsUsed)} of the {LogArchive.Size(s.FontsPartition)} internal-flash fonts partition (not on LittleFS).";
  var e=LogStorage.Estimate(s,retentionDays);CapacityText=e.Text+(s.ClockValid?"":" · clock not set: no age-based deletion yet");
  CapacityWarning=e.Warn?$"Space limit will remove older logs first: about {e.DaysFit:0} days fit, retention is {retentionDays} days.":"";
 }
 private bool loggingEnabled=true;
 /// <summary>Bound to the device NVS setting; changing it sends logging_set (no settings revision).</summary>
 public bool LoggingEnabled{get=>loggingEnabled;set{if(loggingEnabled==value)return;loggingEnabled=value;Notify();if(LogsAvailable)SetLoggingCommand.Execute(value);}}
 private LogFileItem? selectedLog;public LogFileItem? SelectedLog{get=>selectedLog;set{selectedLog=value;Notify();RefreshCommands();}}
 private string logSummary="Connect to list the device logs.";public string LogSummary{get=>logSummary;set{logSummary=value;Notify();}}
 private string logTitle="NO FILE OPEN";public string LogTitle{get=>logTitle;set{logTitle=value;Notify();}}
 private string logSource="";private string logText="";public string LogText{get=>logText;private set{logText=value;Notify();}}
 private string logFilter="";public string LogFilter{get=>logFilter;set{logFilter=value;Notify();ApplyLogFilter();}}
 private bool confirmClear;public bool ConfirmClear{get=>confirmClear;set{confirmClear=value;Notify();}}
 private byte[]? openedBytes;private string? openedName;
 public bool CanSaveLog=>openedBytes!=null&&!busy;
 private bool liveTail;private DispatcherTimer? tailTimer;private List<string> tailLines=[];private bool tailing;
 public bool LiveTail{get=>liveTail;set{liveTail=value;Notify();UpdateTailTimer();}}
 public StudioCommand RefreshLogsCommand{get;private set;}=null!;
 public StudioCommand OpenLogCommand{get;private set;}=null!;
 public StudioCommand ClearLogsCommand{get;private set;}=null!;
 public StudioCommand ConfirmClearLogsCommand{get;private set;}=null!;
 public StudioCommand CancelClearLogsCommand{get;private set;}=null!;
 public StudioCommand SetLoggingCommand{get;private set;}=null!;
 public StudioCommand TailOnceCommand{get;private set;}=null!;
 private StudioCommand[] InitializeLogCommands(){
  RefreshLogsCommand=new(_=>RunAsync(RefreshLogsAsync),()=>!busy&&LogsAvailable);
  OpenLogCommand=new(_=>RunAsync(OpenSelectedLogAsync),()=>!busy&&LogsAvailable&&SelectedLog!=null);
  ClearLogsCommand=new(_=>{ConfirmClear=true;return Task.CompletedTask;},()=>!busy&&LogsAvailable);
  ConfirmClearLogsCommand=new(_=>RunAsync(ClearLogsAsync),()=>!busy&&LogsAvailable);
  CancelClearLogsCommand=new(_=>{ConfirmClear=false;return Task.CompletedTask;});
  SetLoggingCommand=new(p=>RunAsync(()=>SetLoggingAsync(p is true)),()=>LogsAvailable);
  TailOnceCommand=new(_=>RunAsync(()=>TailAsync(true)),()=>!busy&&LogsAvailable);
  DeleteSelectedLogsCommand=new(_=>{if(SelectedLogs.Count>0)ConfirmDelete=true;return Task.CompletedTask;},()=>!busy&&LogsAvailable);
  ConfirmDeleteLogsCommand=new(_=>RunAsync(DeleteSelectedLogsAsync),()=>!busy&&LogsAvailable);
  CancelDeleteLogsCommand=new(_=>{ConfirmDelete=false;return Task.CompletedTask;});
  SetRetentionCommand=new(p=>RunAsync(()=>SetRetentionAsync((int)p!)),()=>StorageAvailable);
  SelectedLogs.CollectionChanged+=(_,_)=>{Notify(nameof(DeletePrompt));if(SelectedLogs.Count==0)ConfirmDelete=false;};
  return [DeleteSelectedLogsCommand,ConfirmDeleteLogsCommand,SetRetentionCommand,RefreshLogsCommand,OpenLogCommand,ClearLogsCommand,ConfirmClearLogsCommand,SetLoggingCommand,TailOnceCommand];
 }
 /// <summary>Called after a successful connect; older firmware has no log commands.</summary>
 private async Task DetectLogsAsync(JsonElement status){
  logsFirmware=status.TryGetProperty("logging_enabled",out var on)&&on.ValueKind is JsonValueKind.True or JsonValueKind.False;
  if(logsFirmware){loggingEnabled=on.GetBoolean();Notify(nameof(LoggingEnabled));}
  else{LogFiles.Clear();LogSummary="This firmware has no persistent logs (InkOS 0.7.7 or newer).";}
  storageFirmware=status.TryGetProperty("log_retention_days",out var days)&&days.ValueKind==JsonValueKind.Number;
  if(storageFirmware){retentionDays=days.GetInt32();Notify(nameof(RetentionIndex));}else Storage=null;
  Notify(nameof(StorageAvailable));
  Notify(nameof(LogsAvailable));
  if(logsFirmware)await RefreshLogsAsync(true);
 }
 public async Task RefreshLogsAsync()=>await RefreshLogsAsync(false);
 private async Task RefreshLogsAsync(bool quiet){
  var v=await Device.LogListAsync();
  loggingEnabled=v.GetProperty("enabled").GetBoolean();Notify(nameof(LoggingEnabled));
  string? keep=SelectedLog?.Name;LogFiles.Clear();
  foreach(var f in v.GetProperty("files").EnumerateArray())
   LogFiles.Add(new(f.GetProperty("name").GetString()!,f.GetProperty("size").GetInt64(),f.GetProperty("mtime").GetInt64()));
  SelectedLogs.Clear();
  SelectedLog=LogFiles.FirstOrDefault(f=>f.Name==keep)??LogFiles.LastOrDefault();
  long total=LogFiles.Sum(f=>f.Size);
  string free=v.TryGetProperty("fs_free_bytes",out var fb)?LogArchive.Size(fb.GetInt64()):"?";
  LogSummary=$"{(loggingEnabled?"LOGGING ON":"LOGGING OFF")} · {LogFiles.Count} files · {LogArchive.Size(total)} · RAM ring {LogArchive.Size(v.GetProperty("ring_bytes").GetInt64())} · "+
   $"rotations {v.GetProperty("rotations").GetInt64()} · dropped USB {v.GetProperty("dropped").GetInt64()} / file {v.GetProperty("file_dropped").GetInt64()} · free {free}"+
   (v.TryGetProperty("host_active",out var h)&&h.GetBoolean()?" · USB disk mounted (rotation paused)":"");
  if(storageFirmware)Storage=StorageInfo.Parse(await Device.FsInfoAsync());
  if(!quiet)Status="LOGS / File list refreshed.";
 }
 public async Task OpenSelectedLogAsync(){
  var file=SelectedLogs.OfType<LogFileItem>().LastOrDefault()??SelectedLog??throw new InvalidOperationException("Choose a log file first.");
  var sw=System.Diagnostics.Stopwatch.StartNew();
  var bytes=await Device.LogReadFileAsync(file.Name,(done,size)=>LogTitle=$"{file.Name} / {done*100/Math.Max(1,size)}%");
  string text;
  try{text=LogArchive.ToText(file.Name,bytes);}catch(Exception ex) when (ex is InvalidDataException or IOException){throw new InvalidOperationException($"{file.Name} could not be decompressed.");}
  openedBytes=bytes;openedName=file.Name;Notify(nameof(CanSaveLog));
  LiveTail=false;logSource=text;ApplyLogFilter();
  LogTitle=$"{file.Name} / {LogArchive.Size(bytes.Length)}{(LogArchive.IsArchive(file.Name)?$" → {LogArchive.Size(Encoding.UTF8.GetByteCount(text))}":"")} / {LogArchive.Lines(text).Count} lines";
  Status=$"LOGS / {file.Name} downloaded in {sw.ElapsedMilliseconds} ms.";
 }
 /// <summary>Writes the file exactly as stored on the device (.tar.gz stays compressed).</summary>
 public async Task SaveOpenedLogAsync(string path){
  if(openedBytes==null)throw new InvalidOperationException("Open a log file first.");
  await File.WriteAllBytesAsync(path,openedBytes);Status=$"LOGS / Saved {openedName} to {path}.";
 }
 public string SuggestedLogFileName=>openedName??"inkos.log";
 internal async Task ClearLogsAsync(){
  ConfirmClear=false;
  await Device.LogClearAsync();
  openedBytes=null;openedName=null;Notify(nameof(CanSaveLog));logSource="";tailLines=[];ApplyLogFilter();LogTitle="NO FILE OPEN";
  await Task.Delay(400);await RefreshLogsAsync(true);Status="LOGS / Device logs cleared.";
 }
 internal async Task DeleteSelectedLogsAsync(){
  ConfirmDelete=false;
  var names=SelectedLogs.OfType<LogFileItem>().Select(f=>f.Name).Distinct().ToArray();
  if(names.Length==0)throw new InvalidOperationException("Select log files first.");
  var (deleted,missing)=await Device.LogDeleteAsync(names);
  if(openedName!=null&&deleted.Contains(openedName)){openedBytes=null;openedName=null;Notify(nameof(CanSaveLog));}
  await RefreshLogsAsync(true);
  Status=$"LOGS / Deleted {deleted.Length} file{(deleted.Length==1?"":"s")}{(missing.Length>0?$", {missing.Length} already gone":"")}.";
 }
 internal async Task SetRetentionAsync(int days){
  retentionDays=await Device.SetLogRetentionAsync(days);Notify(nameof(RetentionIndex));
  await RefreshLogsAsync(true);Status=$"LOGS / Retention set to {LogStorage.RetentionLabels[Array.IndexOf(LogStorage.RetentionDays,retentionDays)]}.";
 }
 private async Task SetLoggingAsync(bool on){
  var v=await Device.SetLoggingAsync(on);
  loggingEnabled=v;Notify(nameof(LoggingEnabled));
  await RefreshLogsAsync(true);Status=on?"LOGS / Logging enabled on the device.":"LOGS / Logging disabled; pending lines were flushed.";
 }
 private async Task TailAsync(bool announce){
  var text=await Device.LogTailAsync();
  tailLines=LogArchive.MergeTail(tailLines,LogArchive.Lines(text));
  logSource=string.Join('\n',tailLines);ApplyLogFilter();
  LogTitle=$"LIVE TAIL / RAM ring / {tailLines.Count} lines";
  openedBytes=Encoding.UTF8.GetBytes(logSource+"\n");openedName="inkos-tail.log";Notify(nameof(CanSaveLog));
  if(announce)Status="LOGS / Tail updated.";
 }
 private void ApplyLogFilter()=>LogText=LogArchive.Filter(logSource,logFilter);
 private void UpdateTailTimer(){
  if(liveTail&&tailTimer==null){
   tailTimer=new DispatcherTimer{Interval=TimeSpan.FromSeconds(2)};
   tailTimer.Tick+=async(_,_)=>{
    if(tailing||busy||!LogsAvailable)return;
    tailing=true;try{await TailAsync(false);}catch{LiveTail=false;Status="LOGS / Live tail stopped: the device did not respond.";}finally{tailing=false;}
   };
   tailTimer.Start();
  }else if(!liveTail&&tailTimer!=null){tailTimer.Stop();tailTimer=null;}
 }
 /// <summary>Synthetic data for headless previews; never touches a device.</summary>
 internal void LoadLogPreview(){
  logsFirmware=true;logPreview=true;loggingEnabled=true;Notify(nameof(LoggingEnabled));
  LogFiles.Clear();
  long t=DateTimeOffset.Parse("2026-10-17T20:24:00Z").ToUnixTimeSeconds();
  LogFiles.Add(new("inkos-000001.tar.gz",599,t));LogFiles.Add(new("inkos-000002.tar.gz",6864,t+319));LogFiles.Add(new("current.log",26099,t+420));
  SelectedLog=LogFiles[1];
  storageFirmware=true;retentionDays=180;Notify(nameof(RetentionIndex));
  Storage=new StorageInfo{Total=4194304,Used=86016,Free=4108288,Reserve=524288,Block=4096,Logs=33562,LogFiles=3,Calendars=38457,Energy=1676,
   CurrentBytes=26099,ArchiveBytes=7463,ArchiveCount=2,Budget=1804288,MaxFiles=300,RotateBytes=65536,OldestArchive=t,NewestArchive=t+319,RetentionDays=180,ClockValid=true,FontsPartition=4194304,FontsUsed=4185866};

  LogSummary="LOGGING ON · 3 files · 32.8 KB · RAM ring 15.9 KB · rotations 2 · dropped USB 506 / file 0 · free 3.93 MB";
  logSource=string.Join('\n',new[]{
   "INKOS boot version=0.7.7 reset=3","INKOS WiFi online","INKOS navigation dispatch page=1 sequence=260 age_us=158876",
   "INKOS display mode=partial kind=navigation ms=309 partials=5 x=8 y=13 w=224 h=17 wave=nav500 on_us=0 xfer_us=3632 wave_us=305261 off_us=0 total_us=308893 power=held",
   "INKOS navigation preview page=1 sequence=260","INKOS navigation accepted page=5 lock_ms=2000 age_us=526512",
   "INKOS display mode=full kind=clock ms=1704 partials=0 x=0 y=0 w=800 h=480 wave=otp-fast on_us=0 xfer_us=83111 wave_us=1579754 off_us=40782 total_us=1703647 power=reused-off",
   "INKOS navigation committed page=5 sequence=264","INKOS log rotated inkos-000002.tar.gz bytes=66404 packed=6864 ms=297"});
  ApplyLogFilter();LogTitle="inkos-000002.tar.gz / 6.7 KB → 64.8 KB / 735 lines";
  Notify(nameof(LogsAvailable));RefreshCommands();
 }
}
