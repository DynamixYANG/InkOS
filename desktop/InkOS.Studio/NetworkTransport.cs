using System.Net;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
namespace InkOS.Studio;

internal sealed class RemoteCodec : IDisposable {
 private readonly byte[] key;
 public RemoteCodec(string hex) {
  if(hex.Length!=64||!hex.All(Uri.IsHexDigit))throw new InvalidOperationException("Enter the 64-character pairing key obtained over USB.");
  key=Convert.FromHexString(hex);
 }
 private static byte[] Aad(string challenge,string direction)=>Encoding.UTF8.GetBytes($"inkos-config-v1|{challenge}|{direction}");
 public JsonElement Encrypt(object command,string challenge) {
  byte[] plain=JsonSerializer.SerializeToUtf8Bytes(command),nonce=RandomNumberGenerator.GetBytes(12),tag=new byte[16],ciphertext=new byte[plain.Length];
  if(plain.Length>4095)throw new InvalidOperationException("Command exceeds the device limit.");
  try {
   using var aes=new AesGcm(key,16);aes.Encrypt(nonce,plain,ciphertext,tag,Aad(challenge,"request"));
   return JsonSerializer.SerializeToElement(new{challenge,nonce=Convert.ToBase64String(nonce),ciphertext=Convert.ToBase64String(ciphertext),tag=Convert.ToBase64String(tag)});
  } finally { CryptographicOperations.ZeroMemory(plain); }
 }
 public JsonElement Decrypt(JsonElement envelope,string challenge) {
  byte[] nonce=Convert.FromBase64String(envelope.GetProperty("nonce").GetString()!),tag=Convert.FromBase64String(envelope.GetProperty("tag").GetString()!),ciphertext=Convert.FromBase64String(envelope.GetProperty("ciphertext").GetString()!);
  if(nonce.Length!=12||tag.Length!=16||ciphertext.Length>16384)throw new CryptographicException("Invalid encrypted response.");
  byte[] plain=new byte[ciphertext.Length];
  try { using var aes=new AesGcm(key,16);aes.Decrypt(nonce,ciphertext,tag,plain,Aad(challenge,"response"));using var doc=JsonDocument.Parse(plain);return doc.RootElement.Clone(); }
  finally { CryptographicOperations.ZeroMemory(plain); }
 }
 public void Dispose()=>CryptographicOperations.ZeroMemory(key);
}

internal sealed class NetworkTransport : IDeviceTransport {
 private readonly HttpClient http;
 private readonly RemoteCodec codec;
 private bool disposed;
 public bool Connected=>!disposed;
 internal static Uri ParseAddress(string address) {
  string value=address.Trim();if(!value.Contains("://"))value="http://"+value;
  if(!Uri.TryCreate(value,UriKind.Absolute,out var uri)||uri.Scheme!="http"||!IPAddress.TryParse(uri.Host.Trim('[',']'),out _)||uri.AbsolutePath!="/"||uri.Query.Length!=0||uri.Fragment.Length!=0||uri.UserInfo.Length!=0)
   throw new InvalidOperationException("Enter the device IP address, for example 192.168.1.100.");
  return uri;
 }
 public NetworkTransport(string address,string key,HttpMessageHandler? handler=null) {
  var uri=ParseAddress(address);codec=new RemoteCodec(key.Trim());
  http=new HttpClient(handler??new HttpClientHandler{AllowAutoRedirect=false,UseProxy=false}){BaseAddress=uri,Timeout=TimeSpan.FromSeconds(20)};
 }
 public async Task<JsonElement> SendAsync(object command,Func<JsonElement,bool> match) {
  try {
   using var challengeResponse=await http.GetAsync("api/challenge");
   if(challengeResponse.StatusCode==HttpStatusCode.Forbidden)throw new InvalidOperationException("IP access is disabled. Pair the screen over USB first.");
   challengeResponse.EnsureSuccessStatusCode();
   using var info=await ReadJsonAsync(challengeResponse);
   if(info.RootElement.GetProperty("protocol").GetString()!="inkos-config-v1")throw new InvalidOperationException("Unsupported IP configuration protocol.");
   string challenge=info.RootElement.GetProperty("challenge").GetString()!;
   if(challenge.Length!=32||!challenge.All(Uri.IsHexDigit))throw new InvalidOperationException("Invalid device challenge.");
   using var payload=new StringContent(codec.Encrypt(command,challenge).GetRawText(),Encoding.UTF8,"application/json");
   using var response=await http.PostAsync("api/command",payload);
   if(response.StatusCode==HttpStatusCode.Unauthorized)throw new InvalidOperationException("Pairing key rejected or request expired. Check the key and reconnect.");
   response.EnsureSuccessStatusCode();
   using var envelope=await ReadJsonAsync(response);var result=codec.Decrypt(envelope.RootElement,challenge);
   if(result.ValueKind!=JsonValueKind.Object||!result.TryGetProperty("inkos",out var ink)||ink.ValueKind!=JsonValueKind.True||!match(result))throw new InvalidOperationException("Unexpected response from the screen.");
   return result;
  } catch(CryptographicException) { throw new InvalidOperationException("Encrypted response could not be verified. Check the pairing key."); }
  catch(HttpRequestException) { throw new InvalidOperationException("Cannot reach the screen. Check its IP address and network connection."); }
  catch(TaskCanceledException) { throw new TimeoutException("The screen did not respond over IP. Check its address or use USB."); }
 }
 private static async Task<JsonDocument> ReadJsonAsync(HttpResponseMessage response) {
  await response.Content.LoadIntoBufferAsync(65536);
  return JsonDocument.Parse(await response.Content.ReadAsByteArrayAsync());
 }
 public void Dispose(){if(disposed)return;disposed=true;http.Dispose();codec.Dispose();}
}
