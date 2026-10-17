using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
namespace InkOS.Studio;
internal sealed record FirmwarePackage(JsonElement Manifest,byte[] Image) {
 internal const string Board="xiao-esp32s3-plus-16m",Layout="inkos-ab594-extfs-v2";
 public static FirmwarePackage Load(string path){
  using var zip=ZipFile.OpenRead(path);
  if(zip.Entries.Count!=2||zip.Entries.Count(x=>x.FullName=="manifest.json")!=1||zip.Entries.Count(x=>x.FullName=="firmware.bin")!=1)throw new InvalidOperationException("Choose an InkOS app-only OTA ZIP.");
  var manifest=zip.GetEntry("manifest.json")!;var image=zip.GetEntry("firmware.bin")!;
  if(manifest.Length>4096||image.Length<288||image.Length>0x5f0000)throw new InvalidOperationException("Invalid firmware package size.");
  using var input=manifest.Open();using var json=JsonDocument.Parse(input);var m=json.RootElement.Clone();
  using var data=image.Open();using var buffer=new MemoryStream();data.CopyTo(buffer);byte[] bytes=buffer.ToArray();
  if(m.GetProperty("format").GetInt32()!=1||m.GetProperty("board").GetString()!=Board||m.GetProperty("layout").GetString()!=Layout||m.GetProperty("size").GetInt32()!=bytes.Length||m.GetProperty("sha256").GetString()!=Convert.ToHexStringLower(SHA256.HashData(bytes)))throw new InvalidOperationException("Firmware board, layout, size or SHA-256 does not match.");
  if(bytes[0]!=0xe9||bytes[12]!=9||bytes[13]!=0||BitConverter.ToUInt32(bytes,32)!=0xabcd5432)throw new InvalidOperationException("Not an ESP32-S3 application image.");
  return new(m,bytes);
 }
 public string Version=>Manifest.GetProperty("version").GetString()!;
}
internal sealed class FirmwareRejectedException(string? message):InvalidOperationException(message);
internal static class FirmwareUpdater {
 internal static async Task<JsonElement> Call(DeviceClient device,object payload){var r=await device.SendAsync(payload,v=>v.TryGetProperty("ok",out _));if(!r.GetProperty("ok").GetBoolean())throw new FirmwareRejectedException(r.TryGetProperty("message",out var m)?m.GetString():"OTA command rejected.");return r;}
 internal static async Task<JsonElement> Install(DeviceClient device,FirmwarePackage p,Action<int,string> progress){
  var old=await Call(device,new{cmd="ota_info"});
  foreach(string k in new[]{"board","layout","font_sha256"})if(old.GetProperty(k).GetString()!=p.Manifest.GetProperty(k).GetString())throw new InvalidOperationException("Firmware requires a different board, partition layout or font archive. Use USB maintenance.");
  if(!old.GetProperty("healthy").GetBoolean())throw new InvalidOperationException("Wait for device boot health confirmation, then retry.");
  var start=await Call(device,new{cmd="ota_begin",board=FirmwarePackage.Board,layout=FirmwarePackage.Layout,font_sha256=p.Manifest.GetProperty("font_sha256").GetString(),size=p.Image.Length,sha256=p.Manifest.GetProperty("sha256").GetString(),header=Convert.ToBase64String(p.Image,0,288)});
  string session=start.GetProperty("session").GetString()!,slot=start.GetProperty("slot").GetString()!;
  try {
   for(int offset=0;offset<p.Image.Length;offset+=2560){
    int n=Math.Min(2560,p.Image.Length-offset);var payload=new{cmd="ota_chunk",session,offset,data=Convert.ToBase64String(p.Image,offset,n)};
    JsonElement response=default;for(int attempt=0;;attempt++){try{response=await Call(device,payload);break;}catch when(attempt<2){await Task.Delay(300);}}
    if(response.GetProperty("received").GetInt32()!=offset+n)throw new InvalidOperationException("Device OTA byte count mismatch.");
    progress(100*(offset+n)/p.Image.Length,$"Uploading / {(offset+n)/1024} of {p.Image.Length/1024} KiB");
   }
  } catch {
   try{await Call(device,new{cmd="ota_abort",session});}catch{}
   throw;
  }
  // A lost finish response may mean that the verified image is already rebooting.
  try{await Call(device,new{cmd="ota_finish",session});}catch(InvalidOperationException ex) when(ex is not FirmwareRejectedException){}catch(TimeoutException){}
  progress(100,"Restarting / checking boot health...");
  var until=DateTime.UtcNow.AddSeconds(150);
  while(DateTime.UtcNow<until){
   await Task.Delay(2000);JsonElement state;
   try{state=await Call(device,new{cmd="ota_info"});}catch{continue;}
   if(state.GetProperty("boot_id").GetUInt32()==old.GetProperty("boot_id").GetUInt32())continue;
   if(state.GetProperty("slot").GetString()!=slot)throw new InvalidOperationException("New firmware failed its startup checks. The screen rolled back to the previous slot.");
   if(state.GetProperty("healthy").GetBoolean()){progress(100,"Updated / healthy / "+slot);return state;}
  }
  throw new TimeoutException("Boot confirmation was not received. Inspect the screen before retrying.");
 }
}
