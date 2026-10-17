using Avalonia;
using Avalonia.Headless;
namespace InkOS.Studio;
internal static class Program {
 [STAThread] public static int Main(string[] args){
  try {
   if(args.Contains("--self-test"))return StudioChecks.SelfTest();
   if(args.Contains("--network-smoke"))return NetworkChecks.DeviceSmokeAsync().GetAwaiter().GetResult();
   if(args.Contains("--logs-smoke")){int at=Array.IndexOf(args,"--port");return LogChecks.DeviceSmokeAsync(at>=0&&at+1<args.Length?args[at+1]:"COM9").GetAwaiter().GetResult();}
   if(args.Contains("--device-smoke"))return StudioChecks.DeviceSmokeAsync().GetAwaiter().GetResult();
   if(args.Contains("--render-preview")){
    AppBuilder.Configure<App>().UseSkia().UseHeadless(new AvaloniaHeadlessPlatformOptions{UseHeadlessDrawing=false}).SetupWithoutStarting();
    return StudioChecks.RenderPreviews();
   }
   return BuildApp().StartWithClassicDesktopLifetime(args);
  }catch(Exception ex){File.WriteAllText("studio-diagnostic.txt",ex.GetType().Name+": "+ex.Message);return 1;}
 }
 public static AppBuilder BuildApp()=>AppBuilder.Configure<App>().UsePlatformDetect().LogToTrace();
}
