using System.Diagnostics;
using System.Formats.Tar;
using System.IO.Compression;
using System.Text;
using System.Text.Json;
namespace InkOS.Studio;
/// <summary>Offline log-format checks and the real-device --logs-smoke run.</summary>
internal static class LogChecks {
 /// <summary>Builds an archive byte-for-byte the way the firmware does: ustar header, raw
 /// deflate wrapped in a 10-byte gzip header (OS=3) and a CRC32/ISIZE trailer.</summary>
 internal static byte[] FirmwareStyleArchive(string member,string text,uint mtime){
  var body=Encoding.UTF8.GetBytes(text);
  var tar=new MemoryStream();var h=new byte[512];
  void Put(int at,string s){var b=Encoding.ASCII.GetBytes(s);Array.Copy(b,0,h,at,b.Length);}
  Put(0,member);Put(100,"0000644");Put(108,"0000000");Put(116,"0000000");
  Put(124,Convert.ToString(body.Length,8).PadLeft(11,'0'));Put(136,Convert.ToString(mtime,8).PadLeft(11,'0'));
  for(int i=148;i<156;i++)h[i]=(byte)' ';h[156]=(byte)'0';Put(257,"ustar");Put(263,"00");Put(265,"inkos");Put(297,"inkos");
  int sum=h.Sum(b=>b);Put(148,Convert.ToString(sum,8).PadLeft(6,'0'));h[154]=0;h[155]=(byte)' ';
  tar.Write(h);tar.Write(body);tar.Write(new byte[(512-body.Length%512)%512]);tar.Write(new byte[1024]);
  var raw=tar.ToArray();var outp=new MemoryStream();
  outp.Write([0x1f,0x8b,8,0,(byte)mtime,(byte)(mtime>>8),(byte)(mtime>>16),(byte)(mtime>>24),0,3]);
  using(var d=new DeflateStream(outp,CompressionLevel.Optimal,leaveOpen:true))d.Write(raw);
  uint crc=Crc32(raw);
  outp.Write(BitConverter.GetBytes(crc));outp.Write(BitConverter.GetBytes((uint)raw.Length));
  return outp.ToArray();
 }
 private static uint Crc32(byte[] data){
  uint crc=0xFFFFFFFF;
  foreach(byte b in data){crc^=b;for(int k=0;k<8;k++)crc=(crc>>1)^(0xEDB88320u&(uint)-(int)(crc&1));}
  return ~crc;
 }
 public static void Validate(){
  string sample=string.Concat(Enumerable.Range(0,900).Select(i=>$"INKOS navigation dispatch page={i%7} sequence={i} age_us={i*37}\n"));
  var firmware=FirmwareStyleArchive("inkos-000042.log",sample,1792268978);
  if(LogArchive.ToText("inkos-000042.tar.gz",firmware)!=sample)throw new Exception("Firmware-format tar.gz decode failed");
  var ms=new MemoryStream();
  using(var gz=new GZipStream(ms,CompressionLevel.Fastest,leaveOpen:true))using(var tw=new TarWriter(gz,TarEntryFormat.Ustar,leaveOpen:false)){
   var e=new UstarTarEntry(TarEntryType.RegularFile,"a.log"){DataStream=new MemoryStream(Encoding.UTF8.GetBytes("INKOS one\n"))};tw.WriteEntry(e);}
  if(LogArchive.ToText("x.tar.gz",ms.ToArray())!="INKOS one\n")throw new Exception(".NET tar.gz decode failed");
  if(LogArchive.ToText("current.log",Encoding.UTF8.GetBytes("INKOS plain\n"))!="INKOS plain\n")throw new Exception("Plain log decode failed");
  try{LogArchive.ToText("bad.tar.gz",[1,2,3]);throw new Exception("Corrupt archive accepted");}catch(InvalidDataException){}
  if(LogArchive.Filter("INKOS WiFi online\nINKOS display mode=full\nINKOS wifi retry","wifi")!="INKOS WiFi online\nINKOS wifi retry")throw new Exception("Log filter failed");
  var merged=LogArchive.MergeTail(["a","b","c"],["b","c","d","e"]);
  if(string.Join(",",merged)!="a,b,c,d,e")throw new Exception("Tail overlap merge failed");
  if(string.Join(",",LogArchive.MergeTail(["a"],["x","y"]))!="a,x,y")throw new Exception("Tail append failed");
  if(LogArchive.MergeTail(Enumerable.Range(0,10).Select(i=>i.ToString()).ToList(),["10","11"],keep:5).Count!=5)throw new Exception("Tail cap failed");
  if(new LogFileItem("inkos-000001.tar.gz",6864,0).SizeText!="6.7 KB"||new LogFileItem("current.log",10,0).ModifiedText!="—")throw new Exception("Log list formatting failed");
  // fs_info fixture captured from a 0.7.8 screen (no private data).
  const string fixture="{\"inkos\":true,\"ok\":true,\"littlefs\":{\"total_bytes\":4194304,\"used_bytes\":86016,\"free_bytes\":4108288,\"block_bytes\":4096,\"reserve_bytes\":524288},\"categories\":{\"logs\":{\"bytes\":12854,\"files\":4},\"calendars\":{\"bytes\":38457,\"files\":3},\"energy\":{\"bytes\":1676,\"files\":1},\"other\":{\"bytes\":0,\"files\":0}},\"dirs\":[],\"log\":{\"current_bytes\":4118,\"archive_bytes\":8736,\"archive_count\":3,\"oldest_archive_time\":1792268659,\"newest_archive_time\":1792269989,\"archive_budget_bytes\":1804288,\"archive_max_files\":300,\"rotate_bytes\":65536,\"retention_days\":35,\"clock_valid\":true},\"fonts\":{\"partition_bytes\":4194304,\"used_bytes\":4185866}}";
  var info=StorageInfo.Parse(JsonDocument.Parse(fixture).RootElement);
  if(info.Total!=4194304||info.Free!=4108288||info.ArchiveCount!=3||info.Budget!=1804288||info.Overhead!=86016-12854-38457-1676||info.FontsUsed!=4185866)throw new Exception("fs_info parse failed");
  if(LogStorage.Segments(info).Sum(x=>x.Bytes)!=info.Total)throw new Exception("Storage segments do not add up to the filesystem size");
  var typical=LogStorage.Estimate(info,35);// 22 min of archives: not enough history, typical rate
  if(typical.Measured||Math.Abs(typical.BytesPerDay-LogStorage.TypicalBytesPerDay)>1||typical.Warn)throw new Exception("Typical capacity estimate failed");
  if(!LogStorage.Estimate(info,180).Warn)throw new Exception("6-month warning missing");
  var history=new StorageInfo{Budget=1804288,MaxFiles=300,ArchiveCount=11,ArchiveBytes=11*7000,OldestArchive=1792000000,NewestArchive=1792000000+10*86400/5};// 11 archives over 2 days
  var est=LogStorage.Estimate(history,7);
  if(!est.Measured||Math.Abs(est.BytesPerDay-35000)>1||Math.Abs(est.DaysFit-Math.Min(1804288/8192.0,300)*7000/35000.0)>0.01)throw new Exception("Measured capacity estimate failed");
  if(LogStorage.RetentionDays.Length!=LogStorage.RetentionLabels.Length||string.Join(",",LogStorage.RetentionDays)!="7,21,35,90,180")throw new Exception("Retention choices failed");
  using var model=new MainViewModel();
  if(model.DeleteSelectedLogsCommand.CanExecute(null)||model.SetRetentionCommand.CanExecute(35)||model.StorageAvailable)throw new Exception("Disconnected storage/delete gating failed");
  if(model.RefreshLogsCommand.CanExecute(null)||model.OpenLogCommand.CanExecute(null)||model.ConfirmClearLogsCommand.CanExecute(null)||model.LogsAvailable)throw new Exception("Disconnected log command gating failed");
 }
 /// <summary>End-to-end against a real screen over USB (default COM9). Leaves logging enabled.</summary>
 public static async Task<int> DeviceSmokeAsync(string port){
  using var model=new MainViewModel{SelectedPort=port};
  var sw=Stopwatch.StartNew();await model.ConnectAsync();
  if(!model.LogsAvailable)throw new Exception("Firmware has no log commands");
  var device=model.Device;var report=new Dictionary<string,object?>{["port"]=port};
  var st=await device.StatusAsync();report["firmware"]=st.GetProperty("version").GetString();int page=st.GetProperty("page").GetInt32();
  // Disabled logging must be a no-op: navigate away and back, the ring must not grow.
  if(await device.SetLoggingAsync(false))throw new Exception("logging_set false not applied");
  await Task.Delay(500);
  long ringOff=(await device.LogListAsync()).GetProperty("ring_bytes").GetInt64();
  await device.NavigateAsync("next");await Task.Delay(3500);await device.NavigateAsync("prev");await Task.Delay(3500);
  var off=await device.LogListAsync();
  if(off.GetProperty("enabled").GetBoolean()||off.GetProperty("ring_bytes").GetInt64()!=ringOff)throw new Exception("Disabled logging still buffered lines");
  if((await device.StatusAsync()).GetProperty("logging_enabled").GetBoolean())throw new Exception("status logging_enabled not updated");
  report["disabled_ring_unchanged"]=true;
  if(!await device.SetLoggingAsync(true))throw new Exception("logging_set true not applied");
  await device.NavigateAsync("next");await Task.Delay(3500);await device.NavigateAsync("prev");await Task.Delay(3500);
  var tail=await device.LogTailAsync();
  if(!tail.Contains("INKOS logging enabled")||!tail.Contains("navigation"))throw new Exception("Tail does not show the new lines");
  report["tail_bytes"]=Encoding.UTF8.GetByteCount(tail);
  // Retention: change and restore (applied immediately; persisted in NVS).
  int originalRetention=model.LogRetentionDays;StorageInfo fs3;
  try{
  await model.SetRetentionAsync(7);
  if((await device.StatusAsync()).GetProperty("log_retention_days").GetInt32()!=7||model.Storage?.RetentionDays!=7)throw new Exception("Retention not applied");
  report["retention_set_7"]=true;
  // Create three archives with log_rotate; fs_info must count them.
  await model.RefreshLogsAsync();var fs0=model.Storage!;
  for(int i=0;i<3;i++){
   var rot=await device.SendAsync(new{cmd="log_rotate"},v=>v.TryGetProperty("ok",out _));
   if(!rot.GetProperty("ok").GetBoolean())throw new Exception("log_rotate refused");
   await Task.Delay(2500);
  }
  await model.RefreshLogsAsync();var fs1=model.Storage!;
  if(fs1.ArchiveCount!=fs0.ArchiveCount+3||fs1.ArchiveBytes<=fs0.ArchiveBytes)throw new Exception($"fs_info archive count {fs0.ArchiveCount}->{fs1.ArchiveCount}");
  report["fs_before"]=new{fs0.ArchiveCount,fs0.ArchiveBytes,fs0.Logs,fs0.Used,fs0.Free};
  report["fs_after_rotate"]=new{fs1.ArchiveCount,fs1.ArchiveBytes,fs1.Logs,fs1.Used,fs1.Free};
  var archive=model.LogFiles.LastOrDefault(f=>LogArchive.IsArchive(f.Name))??throw new Exception("No archive after rotation");
  model.SelectedLogs.Clear();model.SelectedLogs.Add(archive);
  var t0=sw.ElapsedMilliseconds;await model.OpenSelectedLogAsync();long readMs=sw.ElapsedMilliseconds-t0;
  var lines=LogArchive.Lines(model.LogText);
  if(lines.Count==0||lines.Any(l=>!l.StartsWith("INKOS ")))throw new Exception("Archive has torn or foreign lines");
  await model.SaveOpenedLogAsync(Path.GetFullPath("logs-smoke-"+archive.Name));
  report["archive"]=archive.Name;report["archive_bytes"]=archive.Size;report["archive_lines"]=lines.Count;report["archive_read_ms"]=readMs;
  model.LogFilter="log";if(LogArchive.Lines(model.LogText).Any(l=>!l.Contains("log",StringComparison.OrdinalIgnoreCase)))throw new Exception("Filter failed on device data");
  model.LogFilter="";
  // Path validation on the device: nothing outside /logs, nothing deleted.
  foreach(var bad in new[]{"../agenda.bin","/logs/../agenda.bin","/feed0.ics","logs/current.log","/logs/sub/x.log"}){
   try{await device.LogDeleteAsync([bad]);throw new Exception($"Device accepted {bad}");}catch(InvalidOperationException){}
  }
  report["delete_path_validation"]=true;
  // Delete selected: the two newest archives.
  var victims=model.LogFiles.Where(f=>LogArchive.IsArchive(f.Name)).TakeLast(2).ToArray();
  model.SelectedLogs.Clear();foreach(var v in victims)model.SelectedLogs.Add(v);
  if(!model.DeleteSelectedLogsCommand.CanExecute(null))throw new Exception("Delete selected not available");
  await model.DeleteSelectedLogsAsync();var fs2=model.Storage!;
  if(fs2.ArchiveCount!=fs1.ArchiveCount-2||fs2.ArchiveBytes!=fs1.ArchiveBytes-victims.Sum(v=>v.Size)||model.LogFiles.Any(f=>victims.Any(v=>v.Name==f.Name)))throw new Exception("Delete selected numbers wrong");
  report["fs_after_delete_selected"]=new{fs2.ArchiveCount,fs2.ArchiveBytes,deleted=victims.Select(v=>v.Name).ToArray()};
  // Delete current.log while the writer runs: removed now, re-created by the next flush.
  // Make sure it exists first (the writer flushes every 15 s).
  await device.NavigateAsync("next");await Task.Delay(3500);await device.NavigateAsync("prev");await Task.Delay(16000);
  var beforeCurrent=StorageInfo.Parse(await device.FsInfoAsync());
  if(beforeCurrent.CurrentBytes==0)throw new Exception("current.log was not re-created by the writer");
  var (gone,_)=await device.LogDeleteAsync(["/logs/current.log"]);
  var afterCurrent=StorageInfo.Parse(await device.FsInfoAsync());
  if(gone.Length!=1||afterCurrent.CurrentBytes>2048)throw new Exception("current.log delete failed");
  report["current_log_bytes_before_after"]=new[]{beforeCurrent.CurrentBytes,afterCurrent.CurrentBytes};
  await device.NavigateAsync("next");await Task.Delay(3500);await device.NavigateAsync("prev");await Task.Delay(3500);
  await device.SendAsync(new{cmd="log_rotate"},v=>v.TryGetProperty("ok",out _));await Task.Delay(2500);
  await model.RefreshLogsAsync();if(!model.LogFiles.Any())throw new Exception("Writer did not recover after deleting current.log");
  report["current_log_delete"]=true;
  // Clear all.
  await model.ClearLogsAsync();await Task.Delay(1500);await model.RefreshLogsAsync();fs3=model.Storage!;
  if(fs3.ArchiveCount!=0||model.LogFiles.Any(f=>LogArchive.IsArchive(f.Name)))throw new Exception("Clear all left archives");
  report["fs_after_clear"]=new{fs3.ArchiveCount,fs3.ArchiveBytes,fs3.CurrentBytes,fs3.Logs,fs3.Used,fs3.Free};
  }finally{await model.SetRetentionAsync(originalRetention);} // Restored even when a step fails.
  if((await device.StatusAsync()).GetProperty("log_retention_days").GetInt32()!=originalRetention)throw new Exception("Retention not restored");
  report["retention_restored"]=originalRetention;
  report["capacity"]=model.CapacityText;
  report["fonts"]=new{fs3.FontsUsed,fs3.FontsPartition};report["littlefs_total"]=fs3.Total;report["archive_budget"]=fs3.Budget;
  report["files"]=model.LogFiles.Select(f=>new{f.Name,f.Size,f.Mtime}).ToArray();
  report["summary"]=model.LogSummary;
  if((await device.StatusAsync()).GetProperty("page").GetInt32()!=page)throw new Exception("Page not restored");
  report["passed"]=true;report["elapsed_ms"]=sw.ElapsedMilliseconds;
  File.WriteAllText("studio-logs-smoke.json",JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));
  return 0;
 }
}
