using System.IO.Ports;
using System.Text;
using System.Text.Json;
namespace InkOS.Studio;
internal sealed class UsbTransport : IDeviceTransport {
 private readonly SerialPort port;
 public bool Connected=>port.IsOpen;
 public UsbTransport(string name) {
  port=new SerialPort(name,115200){Encoding=new UTF8Encoding(false),DtrEnable=true,RtsEnable=false,ReadTimeout=350,WriteTimeout=2000,NewLine="\n"};
  try { port.Open(); } catch { port.Dispose(); throw; }
 }
 public Task<JsonElement> SendAsync(object command,Func<JsonElement,bool> match)=>Task.Run(()=>{
  if(!Connected) throw new InvalidOperationException("USB is disconnected.");
  string payload=JsonSerializer.Serialize(command);
  if(Encoding.UTF8.GetByteCount(payload)>4095) throw new InvalidOperationException("Command exceeds the device limit.");
  port.DiscardInBuffer();port.WriteLine(payload);var stop=DateTime.UtcNow.AddSeconds(15);
  while(DateTime.UtcNow<stop) {
   string line;try{line=port.ReadLine();}catch(TimeoutException){continue;}
   if(!line.TrimStart().StartsWith('{'))continue;
   try{using var doc=JsonDocument.Parse(line);var v=doc.RootElement;if(v.ValueKind==JsonValueKind.Object&&v.TryGetProperty("inkos",out var ink)&&ink.ValueKind==JsonValueKind.True&&match(v))return v.Clone();}catch(JsonException){}
  }
  throw new TimeoutException("InkOS did not respond. Check the USB port and firmware.");
 });
 public void Dispose(){try{port.Close();}catch(IOException){}port.Dispose();}
}
