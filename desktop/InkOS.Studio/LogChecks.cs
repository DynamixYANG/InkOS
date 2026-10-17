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
  using var model=new MainViewModel();
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
  // Force a rotation and read the archive back through the Studio code path.
  var rot=await device.SendAsync(new{cmd="log_rotate"},v=>v.TryGetProperty("ok",out _));
  if(!rot.GetProperty("ok").GetBoolean())throw new Exception("log_rotate refused");
  await Task.Delay(2500);
  await model.RefreshLogsAsync();
  var archive=model.LogFiles.LastOrDefault(f=>LogArchive.IsArchive(f.Name))??throw new Exception("No archive after rotation");
  model.SelectedLog=archive;
  var t0=sw.ElapsedMilliseconds;await model.OpenSelectedLogAsync();long readMs=sw.ElapsedMilliseconds-t0;
  var lines=LogArchive.Lines(model.LogText);
  if(lines.Count==0||lines.Any(l=>!l.StartsWith("INKOS ")))throw new Exception("Archive has torn or foreign lines");
  if(!model.LogText.Contains("INKOS logging enabled"))throw new Exception("Archive misses the enable marker");
  await model.SaveOpenedLogAsync(Path.GetFullPath("logs-smoke-"+archive.Name));
  model.LogFilter="navigation";if(LogArchive.Lines(model.LogText).Any(l=>!l.Contains("navigation",StringComparison.OrdinalIgnoreCase)))throw new Exception("Filter failed on device data");
  model.LogFilter="";
  report["archive"]=archive.Name;report["archive_bytes"]=archive.Size;report["archive_lines"]=lines.Count;report["archive_read_ms"]=readMs;
  report["files"]=model.LogFiles.Select(f=>new{f.Name,f.Size,f.Mtime}).ToArray();
  report["summary"]=model.LogSummary;
  if((await device.StatusAsync()).GetProperty("page").GetInt32()!=page)throw new Exception("Page not restored");
  report["passed"]=true;report["elapsed_ms"]=sw.ElapsedMilliseconds;
  File.WriteAllText("studio-logs-smoke.json",JsonSerializer.Serialize(report,new JsonSerializerOptions{WriteIndented=true}));
  return 0;
 }
}
