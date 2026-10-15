using System.IO.Ports;
using System.Text;
using System.Text.Json;
namespace InkOS.Studio;
public sealed class DeviceClient:IDisposable {
 private SerialPort? port;private readonly SemaphoreSlim gate=new(1,1);
 public bool Connected=>port?.IsOpen==true;
 public void Open(string name){DisposePort();port=new SerialPort(name,115200){Encoding=new UTF8Encoding(false),DtrEnable=true,RtsEnable=false,ReadTimeout=350,WriteTimeout=2000,NewLine="\n"};port.Open();}
 public async Task<JsonElement> SendAsync(object command,Func<JsonElement,bool> match) {
  await gate.WaitAsync();try{return await Task.Run(()=>{
   var p=port??throw new InvalidOperationException("Connect the screen first.");if(!p.IsOpen)throw new InvalidOperationException("USB is disconnected.");
   p.DiscardInBuffer();p.WriteLine(JsonSerializer.Serialize(command));var stop=DateTime.UtcNow.AddSeconds(15);
   while(DateTime.UtcNow<stop){string line;try{line=p.ReadLine();}catch(TimeoutException){continue;}
    if(!line.TrimStart().StartsWith('{'))continue;
    try{using var doc=JsonDocument.Parse(line);var v=doc.RootElement;if(v.TryGetProperty("inkos",out var ink)&&ink.ValueKind==JsonValueKind.True&&match(v))return v.Clone();}catch(JsonException){}
   }throw new TimeoutException("InkOS did not respond. Check the port and firmware.");
  });}finally{gate.Release();}
 }
 public Task<JsonElement> StatusAsync()=>SendAsync(new{cmd="status"},v=>v.TryGetProperty("wifi",out _));
 public Task<JsonElement> ReadSettingsAsync()=>SendAsync(new{cmd="settings_get"},v=>v.TryGetProperty("settings",out _));
 public async Task SaveAsync(object payload){var v=await SendAsync(payload,x=>x.TryGetProperty("saved",out _));if(!v.GetProperty("saved").GetBoolean())throw new InvalidOperationException(v.TryGetProperty("message",out var m)?m.GetString():"The screen could not save the configuration.");}
 public Task<JsonElement> NavigateAsync(string action)=>SendAsync(new{cmd="navigate",action},v=>v.TryGetProperty("ok",out _));
 private void DisposePort(){if(port!=null){try{port.Close();}catch(IOException){}port.Dispose();port=null;}}
 public void Dispose()=>DisposePort();
}
