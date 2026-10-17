using System.Net;
using System.Text;
namespace InkOS.Studio;
internal static class ICloudChecks {
 private sealed class Fixture(Func<HttpRequestMessage,int,HttpResponseMessage> reply):HttpMessageHandler {
  internal int Calls;
  protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage r,CancellationToken ct)=>Task.FromResult(reply(r,++Calls));
 }
 private static HttpResponseMessage Xml(string value)=>new((HttpStatusCode)207){Content=new StringContent(value,Encoding.UTF8,"application/xml")};
 private static string Multi(string prop,string value)=>$"<multistatus xmlns='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><response><propstat><prop>{prop.Replace("VALUE",value)}</prop><status>HTTP/1.1 200 OK</status></propstat></response></multistatus>";
 internal static async Task Validate(){
  string auth=Convert.ToBase64String(Encoding.UTF8.GetBytes("fixture@example.invalid:abcd-efgh-ijkl-mnop"));
  using var fixture=new Fixture((r,n)=>{
   if(r.Method.Method!="PROPFIND"||r.Headers.Authorization?.Parameter!=auth)throw new Exception("CalDAV verb/auth failed");
   if(n==1)return new(HttpStatusCode.MovedPermanently){Headers={Location=new Uri("https://p12-caldav.icloud.com/")}};
   if(n==2)return Xml(Multi("<current-user-principal><href>VALUE</href></current-user-principal>","/123/principal/"));
   if(n==3)return Xml(Multi("<c:calendar-home-set><href>VALUE</href></c:calendar-home-set>","https://p12-caldav.icloud.com/123/calendars/"));
   if(n!=4||!r.Headers.TryGetValues("Depth",out var depth)||depth.Single()!="1")throw new Exception("CalDAV depth failed");
   return Xml("<d:multistatus xmlns:d='DAV:' xmlns:c='urn:ietf:params:xml:ns:caldav'><d:response><d:href>/123/calendars/work/</d:href><d:propstat><d:prop><d:displayname>工作 &amp; Work</d:displayname><d:resourcetype><c:calendar/></d:resourcetype><c:supported-calendar-component-set><c:comp name='VEVENT'/></c:supported-calendar-component-set></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response><d:response><d:href>/123/calendars/todos/</d:href><d:propstat><d:prop><d:displayname>Tasks</d:displayname><d:resourcetype><c:calendar/></d:resourcetype><c:supported-calendar-component-set><c:comp name='VTODO'/></c:supported-calendar-component-set></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response></d:multistatus>");
  });
  using(var client=new ICloudCalendarClient(fixture)){
   var result=await client.DiscoverAsync("fixture@example.invalid","abcd-efgh-ijkl-mnop");
   if(result.Count!=1||result[0].Name!="工作 & Work"||result[0].Url!="https://p12-caldav.icloud.com/123/calendars/work/")throw new Exception("CalDAV namespace/calendar selection failed");
  }
  foreach(string bad in new[]{"http://caldav.icloud.com/","https://caldav.icloud.com.evil.invalid/","https://user@caldav.icloud.com/","https://p12-caldav.icloud.com:8443/"}){
   try{ICloudCalendarClient.ValidateUrl(bad);throw new Exception("Untrusted CalDAV URL accepted");}catch(InvalidOperationException){}
  }
  using var redirectFixture=new Fixture((r,n)=>n==1?new(HttpStatusCode.TemporaryRedirect){Headers={Location=new Uri("https://evil.invalid/")}}:throw new Exception("Credentials forwarded to untrusted host"));
  using(var client=new ICloudCalendarClient(redirectFixture))try{await client.DiscoverAsync("fixture@example.invalid","abcd-efgh-ijkl-mnop");throw new Exception("Unsafe redirect accepted");}catch(InvalidOperationException){}
  using var dtdFixture=new Fixture((r,n)=>Xml("<!DOCTYPE x [<!ENTITY x SYSTEM 'file:///private'>]><x>&x;</x>"));
  using(var client=new ICloudCalendarClient(dtdFixture))try{await client.DiscoverAsync("fixture@example.invalid","abcd-efgh-ijkl-mnop");throw new Exception("XML DTD accepted");}catch(InvalidOperationException){}
 }
}
