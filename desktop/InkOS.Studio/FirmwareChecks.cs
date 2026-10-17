using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
namespace InkOS.Studio;
internal static class FirmwareChecks {
 internal static async Task Validate(){
  string folder=Path.Combine(Path.GetTempPath(),"inkos-ota-check-"+Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);
  try{
   byte[] b=new byte[6000];b[0]=0xe9;b[12]=9;BitConverter.GetBytes(0xabcd5432u).CopyTo(b,32);
   var m=new{format=1,board=FirmwarePackage.Board,layout=FirmwarePackage.Layout,font_sha256=new string('a',64),size=b.Length,sha256=Convert.ToHexStringLower(SHA256.HashData(b)),version="test"};
   string file=Path.Combine(folder,"test.zip");
   void Write(bool corrupt){using var z=ZipFile.Open(file,ZipArchiveMode.Create);using(var text=new StreamWriter(z.CreateEntry("manifest.json").Open()))text.Write(JsonSerializer.Serialize(m));using var data=z.CreateEntry("firmware.bin").Open();byte[] image=(byte[])b.Clone();if(corrupt)image[^1]=1;data.Write(image);}
   Write(false);var p=FirmwarePackage.Load(file);if(!p.Image.SequenceEqual(b))throw new Exception("OTA package image changed");
   var fake=new Fake(b,m.font_sha256);using var device=new DeviceClient(fake);var result=await FirmwareUpdater.Install(device,p,(_,_)=>{});
   if(result.GetProperty("slot").GetString()!="ota_1"||fake.Bytes!=6000||!fake.Finished)throw new Exception("OTA upload/reboot verification failed");
   File.Delete(file);Write(true);try{FirmwarePackage.Load(file);throw new Exception("Corrupt OTA image accepted");}catch(InvalidOperationException){}
  }finally{Directory.Delete(folder,true);}
 }
 private sealed class Fake(byte[] image,string font):IDeviceTransport {
  public bool Connected=>true;public int Bytes;public bool Finished;
  public Task<JsonElement> SendAsync(object payload,Func<JsonElement,bool> match){
   var j=JsonSerializer.SerializeToElement(payload);string cmd=j.GetProperty("cmd").GetString()!;object reply;
   if(cmd=="ota_info")reply=new{inkos=true,ok=true,board=FirmwarePackage.Board,layout=FirmwarePackage.Layout,font_sha256=font,slot=Finished?"ota_1":"ota_0",boot_id=Finished?2:1,healthy=true};
   else if(cmd=="ota_begin")reply=new{inkos=true,ok=true,session="test",slot="ota_1"};
   else if(cmd=="ota_chunk"){
    if(j.GetProperty("offset").GetInt32()!=Bytes)throw new Exception("OTA chunk order failed");byte[] data=Convert.FromBase64String(j.GetProperty("data").GetString()!);if(!data.SequenceEqual(image.AsSpan(Bytes,data.Length).ToArray()))throw new Exception("OTA bytes changed");Bytes+=data.Length;reply=new{inkos=true,ok=true,received=Bytes};
   }else if(cmd=="ota_finish"){if(Bytes!=image.Length)throw new Exception("Premature OTA finish");Finished=true;reply=new{inkos=true,ok=true};}
   else throw new Exception("Unexpected OTA command");
   var result=JsonSerializer.SerializeToElement(reply);if(!match(result))throw new Exception("OTA response predicate failed");return Task.FromResult(result);
  }
  public void Dispose(){}
 }
}
