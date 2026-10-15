using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
namespace InkOS.Studio;
public static class SpotifyAuthorization {
 public const string RedirectUri="http://127.0.0.1:8768/callback";
 public static string Base64Url(byte[] b)=>Convert.ToBase64String(b).TrimEnd('=').Replace('+','-').Replace('/','_');
 public static string Challenge(string verifier)=>Base64Url(SHA256.HashData(Encoding.ASCII.GetBytes(verifier)));
 public static async Task<string> AuthorizeAsync(string clientId,CancellationToken ct) {
  if(!Regex.IsMatch(clientId,"^[a-fA-F0-9]{32}$"))throw new InvalidOperationException("Enter your 32-character Spotify Client ID.");
  string state=Base64Url(RandomNumberGenerator.GetBytes(32)),verifier=Base64Url(RandomNumberGenerator.GetBytes(64));
  var listener=new TcpListener(IPAddress.Loopback,8768);listener.Start();
  try {
   string url="https://accounts.spotify.com/authorize?"+string.Join('&',new Dictionary<string,string>{{"client_id",clientId},{"response_type","code"},{"redirect_uri",RedirectUri},{"state",state},{"code_challenge",Challenge(verifier)},{"code_challenge_method","S256"},{"scope","user-read-currently-playing user-read-playback-state"}}.Select(x=>Uri.EscapeDataString(x.Key)+"="+Uri.EscapeDataString(x.Value)));
   Process.Start(new ProcessStartInfo(url){UseShellExecute=true});string? code=null;
   while(code==null) {
    using var client=await listener.AcceptTcpClientAsync(ct);using var stream=client.GetStream();using var reader=new StreamReader(stream,Encoding.ASCII,false,4096,true);
    using var timeout=CancellationTokenSource.CreateLinkedTokenSource(ct);timeout.CancelAfter(TimeSpan.FromSeconds(5));
    string? first=await reader.ReadLineAsync(timeout.Token);if(first==null||first.Length>4096)continue;
    var parts=first.Split(' ');if(parts.Length<2||parts[0]!="GET"||!parts[1].StartsWith("/callback?"))continue;
    var uri=new Uri(RedirectUri+parts[1]["/callback".Length..]);var query=uri.Query.TrimStart('?').Split('&').Select(x=>x.Split('=',2)).Where(x=>x.Length==2).ToDictionary(x=>Uri.UnescapeDataString(x[0]),x=>Uri.UnescapeDataString(x[1].Replace('+',' ')));
    bool valid=query.TryGetValue("state",out var actual)&&CryptographicOperations.FixedTimeEquals(Encoding.ASCII.GetBytes(actual),Encoding.ASCII.GetBytes(state));
    string message=valid?"Authorization received. Return to InkOS Studio.":"Authorization state did not match. Return to InkOS Studio and try again.";
    byte[] body=Encoding.UTF8.GetBytes("<!doctype html><html lang=en><meta charset=utf-8><title>InkOS Studio</title><body style='font:18px monospace;margin:60px'><h1>InkOS / Spotify</h1><p>"+message+"</p></body></html>");
    byte[] headers=Encoding.ASCII.GetBytes($"HTTP/1.1 {(valid?200:400)} OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {body.Length}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");await stream.WriteAsync(headers,ct);await stream.WriteAsync(body,ct);
    if(!valid)continue;if(query.ContainsKey("error"))throw new InvalidOperationException("Spotify authorization was declined.");query.TryGetValue("code",out code);
   }
   using var http=new HttpClient{Timeout=TimeSpan.FromSeconds(20)};
   using var response=await http.PostAsync("https://accounts.spotify.com/api/token",new FormUrlEncodedContent(new Dictionary<string,string>{{"client_id",clientId},{"grant_type","authorization_code"},{"code",code},{"redirect_uri",RedirectUri},{"code_verifier",verifier}}),ct);
   if(!response.IsSuccessStatusCode)throw new InvalidOperationException($"Spotify token exchange returned HTTP {(int)response.StatusCode}. Check the app and Redirect URI.");
   using var doc=JsonDocument.Parse(await response.Content.ReadAsStringAsync(ct));return doc.RootElement.GetProperty("refresh_token").GetString()??throw new InvalidOperationException("Spotify did not issue a refresh token.");
  }finally{listener.Stop();}
 }
}
