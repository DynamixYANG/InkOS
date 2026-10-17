using System.Collections.ObjectModel;
using System.Text;
using System.Text.Json;
using Avalonia.Threading;
namespace InkOS.Studio;
public sealed partial class MainViewModel {
 public ObservableCollection<LogFileItem> LogFiles{get;}=[];
 private bool logsFirmware,logPreview;public bool LogsAvailable=>(Connected||logPreview)&&logsFirmware;
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
  return [RefreshLogsCommand,OpenLogCommand,ClearLogsCommand,ConfirmClearLogsCommand,SetLoggingCommand,TailOnceCommand];
 }
 /// <summary>Called after a successful connect; older firmware has no log commands.</summary>
 private async Task DetectLogsAsync(JsonElement status){
  logsFirmware=status.TryGetProperty("logging_enabled",out var on)&&on.ValueKind is JsonValueKind.True or JsonValueKind.False;
  if(logsFirmware){loggingEnabled=on.GetBoolean();Notify(nameof(LoggingEnabled));}
  else{LogFiles.Clear();LogSummary="This firmware has no persistent logs (InkOS 0.7.7 or newer).";}
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
  SelectedLog=LogFiles.FirstOrDefault(f=>f.Name==keep)??LogFiles.LastOrDefault();
  long total=LogFiles.Sum(f=>f.Size);
  string free=v.TryGetProperty("fs_free_bytes",out var fb)?LogArchive.Size(fb.GetInt64()):"?";
  LogSummary=$"{(loggingEnabled?"LOGGING ON":"LOGGING OFF")} · {LogFiles.Count} files · {LogArchive.Size(total)} · RAM ring {LogArchive.Size(v.GetProperty("ring_bytes").GetInt64())} · "+
   $"rotations {v.GetProperty("rotations").GetInt64()} · dropped USB {v.GetProperty("dropped").GetInt64()} / file {v.GetProperty("file_dropped").GetInt64()} · free {free}"+
   (v.TryGetProperty("host_active",out var h)&&h.GetBoolean()?" · USB disk mounted (rotation paused)":"");
  if(!quiet)Status="LOGS / File list refreshed.";
 }
 public async Task OpenSelectedLogAsync(){
  var file=SelectedLog??throw new InvalidOperationException("Choose a log file first.");
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
 private async Task ClearLogsAsync(){
  ConfirmClear=false;
  await Device.LogClearAsync();
  openedBytes=null;openedName=null;Notify(nameof(CanSaveLog));logSource="";tailLines=[];ApplyLogFilter();LogTitle="NO FILE OPEN";
  await Task.Delay(400);await RefreshLogsAsync(true);Status="LOGS / Device logs cleared.";
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
