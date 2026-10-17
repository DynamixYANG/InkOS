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
 public void Dispose(){transport?.Dispose();transport=null;}
}
