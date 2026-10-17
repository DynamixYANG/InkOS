using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Xml;
using System.Xml.Linq;
namespace InkOS.Studio;
public sealed record ICloudCalendar(string Name,string Url) { public override string ToString()=>Name; }
/// <summary>Read-only CalDAV discovery. Credentials stay in memory and are never logged.</summary>
internal sealed class ICloudCalendarClient:IDisposable {
 private static readonly XNamespace Dav="DAV:",Cal="urn:ietf:params:xml:ns:caldav";
 private readonly HttpClient client;
 internal ICloudCalendarClient(HttpMessageHandler? handler=null){client=new(handler??new HttpClientHandler{AllowAutoRedirect=false}){Timeout=TimeSpan.FromSeconds(20)};}
 internal static Uri ValidateUrl(string value){
  if(!Uri.TryCreate(value,UriKind.Absolute,out var u)||u.Scheme!="https"||u.Port!=443||u.UserInfo.Length!=0||u.Query.Length!=0||u.Fragment.Length!=0||value.Contains('\\')||value.Any(char.IsControl))throw new InvalidOperationException("Invalid iCloud calendar address.");
  if(u.Host!="caldav.icloud.com"&&!System.Text.RegularExpressions.Regex.IsMatch(u.Host,@"^p[0-9]{1,4}-caldav\.icloud\.com$"))throw new InvalidOperationException("Only Apple's iCloud CalDAV servers are allowed.");
  return u;
 }
 internal static void ValidateAccount(string username,string password){
  if(string.IsNullOrWhiteSpace(username)||username.Length>254||username.Contains(':')||username.Any(char.IsControl)||password.Length<8||password.Length>128||password.Any(char.IsControl))throw new InvalidOperationException("Enter your Apple Account and an app-specific password.");
 }
 private static XElement? SuccessfulProperty(XElement response,XName property)=>response.Elements(Dav+"propstat").Where(p=>p.Element(Dav+"status")?.Value.Split(' ',StringSplitOptions.RemoveEmptyEntries).ElementAtOrDefault(1)=="200").Select(p=>p.Element(Dav+"prop")?.Element(property)).FirstOrDefault(p=>p!=null);
 private async Task<(XDocument Document,Uri Address)> PropfindAsync(Uri address,string authorization,int depth,string properties,CancellationToken ct){
  string body=$"<?xml version=\"1.0\" encoding=\"UTF-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop>{properties}</d:prop></d:propfind>";
  for(int redirects=0;redirects<4;redirects++){
   ValidateUrl(address.AbsoluteUri);
   using var request=new HttpRequestMessage(new HttpMethod("PROPFIND"),address){Content=new StringContent(body,Encoding.UTF8,"application/xml")};
   request.Headers.Authorization=new AuthenticationHeaderValue("Basic",authorization);request.Headers.Add("Depth",depth.ToString());
   request.Headers.UserAgent.ParseAdd("InkOS-Studio/0.7.0");
   using var response=await client.SendAsync(request,HttpCompletionOption.ResponseHeadersRead,ct);
   int code=(int)response.StatusCode;
   if(code is 301 or 302 or 303 or 307 or 308){address=ValidateUrl(new Uri(address,response.Headers.Location??throw new InvalidOperationException("Missing iCloud redirect.")).AbsoluteUri);continue;}
   if(response.StatusCode is HttpStatusCode.Unauthorized or HttpStatusCode.Forbidden)throw new InvalidOperationException("iCloud rejected the account or app-specific password. Generate a fresh password on account.apple.com.");
   if(code!=207)throw new InvalidOperationException($"iCloud calendar discovery returned HTTP {code}.");
   if(response.Content.Headers.ContentLength>262144)throw new InvalidOperationException("iCloud calendar list exceeds the size limit.");
   await using var input=await response.Content.ReadAsStreamAsync(ct);using var limited=new MemoryStream();var buffer=new byte[8192];
   int read;while((read=await input.ReadAsync(buffer,ct))!=0){if(limited.Length+read>262144)throw new InvalidOperationException("iCloud calendar list exceeds the size limit.");limited.Write(buffer,0,read);}
   limited.Position=0;using var xml=XmlReader.Create(limited,new XmlReaderSettings{DtdProcessing=DtdProcessing.Prohibit,XmlResolver=null,MaxCharactersInDocument=262144,Async=true});
   var document=await XDocument.LoadAsync(xml,LoadOptions.None,ct);
   if(document.Root?.Name!=Dav+"multistatus")throw new InvalidOperationException("Invalid iCloud calendar response.");
   return(document,address);
  }
  throw new InvalidOperationException("Too many iCloud redirects.");
 }
 public async Task<IReadOnlyList<ICloudCalendar>> DiscoverAsync(string username,string password,CancellationToken ct=default){
  ValidateAccount(username,password);
  string authorization=Convert.ToBase64String(Encoding.UTF8.GetBytes(username+":"+password));
  try{
   var principal=await PropfindAsync(new Uri("https://caldav.icloud.com/"),authorization,0,"<d:current-user-principal/>",ct);
   string? principalHref=principal.Document.Root!.Elements(Dav+"response").Select(r=>SuccessfulProperty(r,Dav+"current-user-principal")?.Element(Dav+"href")?.Value).FirstOrDefault(h=>!string.IsNullOrWhiteSpace(h));
   if(principalHref==null)throw new InvalidOperationException("iCloud did not return a calendar principal.");
   var home=await PropfindAsync(ValidateUrl(new Uri(principal.Address,principalHref).AbsoluteUri),authorization,0,"<c:calendar-home-set/>",ct);
   string? homeHref=home.Document.Root!.Elements(Dav+"response").Select(r=>SuccessfulProperty(r,Cal+"calendar-home-set")?.Element(Dav+"href")?.Value).FirstOrDefault(h=>!string.IsNullOrWhiteSpace(h));
   if(homeHref==null)throw new InvalidOperationException("iCloud did not return a calendar home.");
   var listing=await PropfindAsync(ValidateUrl(new Uri(home.Address,homeHref).AbsoluteUri),authorization,1,"<d:displayname/><d:resourcetype/><c:supported-calendar-component-set/>",ct);
   var calendars=new List<ICloudCalendar>();
   foreach(var r in listing.Document.Root!.Elements(Dav+"response")){
    if(SuccessfulProperty(r,Dav+"resourcetype")?.Element(Cal+"calendar")==null)continue;
    var components=SuccessfulProperty(r,Cal+"supported-calendar-component-set");
    if(components!=null&&!components.Elements(Cal+"comp").Any(c=>(string?)c.Attribute("name")=="VEVENT"))continue;
    string? href=r.Element(Dav+"href")?.Value;if(string.IsNullOrWhiteSpace(href))continue;
    var address=ValidateUrl(new Uri(listing.Address,href).AbsoluteUri);
    string name=SuccessfulProperty(r,Dav+"displayname")?.Value.Trim()??"";
    if(name.Length==0)name="Untitled calendar";
    calendars.Add(new(name,address.AbsoluteUri));if(calendars.Count>128)throw new InvalidOperationException("Too many iCloud calendars.");
   }
   if(calendars.Count==0)throw new InvalidOperationException("No event calendars found. Check that iCloud Calendar is enabled for this account.");
   return calendars.DistinctBy(c=>c.Url).OrderBy(c=>c.Name,StringComparer.CurrentCultureIgnoreCase).ToArray();
  }catch(HttpRequestException){throw new InvalidOperationException("Could not reach iCloud. Check the internet connection.");}
   catch(XmlException){throw new InvalidOperationException("Invalid XML in iCloud's calendar response.");}
 }
 public void Dispose()=>client.Dispose();
}
