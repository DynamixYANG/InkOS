using System.Text.Json;
namespace InkOS.Studio;
public interface IDeviceTransport : IDisposable {
 bool Connected { get; }
 Task<JsonElement> SendAsync(object command, Func<JsonElement,bool> match);
}
public sealed class DeviceClient:IDisposable {
 private IDeviceTransport? transport; private readonly SemaphoreSlim gate=new(1,1);
 public DeviceClient(){}
 internal DeviceClient(IDeviceTransport supplied){transport=supplied;}
 public bool Connected=>transport?.Connected==true;
 public void Open(string port){Dispose();transport=new UsbTransport(port);}
 public void OpenNetwork(string address,string key){Dispose();transport=new NetworkTransport(address,key);}
 public async Task<JsonElement> SendAsync(object command,Func<JsonElement,bool> match){
  await gate.WaitAsync();try{return await (transport??throw new InvalidOperationException("Connect the screen first.")).SendAsync(command,match);}finally{gate.Release();}
 }
 public Task<JsonElement> StatusAsync()=>SendAsync(new{cmd="status"},v=>v.TryGetProperty("wifi",out _));
 public Task<JsonElement> ReadSettingsAsync()=>SendAsync(new{cmd="settings_get"},v=>v.TryGetProperty("settings",out _));
 public async Task SaveAsync(object payload){var v=await SendAsync(payload,x=>x.TryGetProperty("saved",out _));if(!v.GetProperty("saved").GetBoolean())throw new InvalidOperationException(v.TryGetProperty("message",out var m)?m.GetString():"The screen could not save the configuration.");}
 public Task<JsonElement> NavigateAsync(string action)=>SendAsync(new{cmd="navigate",action},v=>v.TryGetProperty("ok",out _));
 public Task<JsonElement> LogListAsync()=>SendAsync(new{cmd="log_list"},v=>v.TryGetProperty("files",out _));
 public async Task<string> LogTailAsync(int bytes=3072){
  var v=await SendAsync(new{cmd="log_tail",bytes},x=>x.TryGetProperty("data",out _)&&!x.TryGetProperty("name",out _));
  return System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(v.GetProperty("data").GetString()??""));
 }
 /// <summary>Downloads a whole log file in base64 chunks (log_read offset/length).</summary>
 public async Task<byte[]> LogReadFileAsync(string name,Action<long,long>? progress=null){
  using var buffer=new MemoryStream();
  for(long offset=0;;){
   long at=offset;
   var v=await SendAsync(new{cmd="log_read",name,offset=at,length=2048},x=>x.TryGetProperty("ok",out _)&&(!x.GetProperty("ok").GetBoolean()||x.TryGetProperty("name",out var n)&&n.GetString()==name&&x.GetProperty("offset").GetInt64()==at));
   if(!v.GetProperty("ok").GetBoolean())throw new InvalidOperationException(v.TryGetProperty("message",out var m)?m.GetString():"The screen could not read the log file.");
   var chunk=Convert.FromBase64String(v.GetProperty("data").GetString()??"");
   buffer.Write(chunk);offset+=chunk.Length;long size=v.GetProperty("size").GetInt64();progress?.Invoke(offset,size);
   if(v.GetProperty("eof").GetBoolean()||chunk.Length==0)break;
   if(offset>16*1024*1024)throw new InvalidOperationException("Log file is unexpectedly large.");
  }
  return buffer.ToArray();
 }
 public async Task LogClearAsync(){
  var v=await SendAsync(new{cmd="log_clear",files=true},x=>x.TryGetProperty("ok",out _)&&!x.TryGetProperty("files",out _)&&!x.TryGetProperty("data",out _));
  if(!v.GetProperty("ok").GetBoolean())throw new InvalidOperationException(v.TryGetProperty("message",out var m)?m.GetString():"The screen could not clear its logs.");
 }
 public async Task<bool> SetLoggingAsync(bool enabled){
  var v=await SendAsync(new{cmd="logging_set",enabled},x=>x.TryGetProperty("logging_enabled",out _)||x.TryGetProperty("saved",out _));
  if(v.TryGetProperty("saved",out var s)&&!s.GetBoolean())throw new InvalidOperationException(v.TryGetProperty("message",out var m)?m.GetString():"The screen could not change logging.");
  return v.GetProperty("logging_enabled").GetBoolean();
 }
 public void Dispose(){transport?.Dispose();transport=null;}
}
