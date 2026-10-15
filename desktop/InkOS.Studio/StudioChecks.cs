using System.Text.Json;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Threading;
using Avalonia.VisualTree;
namespace InkOS.Studio;
/// <summary>Deterministic CLI checks, including real ShadUI visual-tree rendering.</summary>
internal static class StudioChecks {
 public static int SelfTest(){
  if(SpotifyAuthorization.Challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk")!="E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM")throw new Exception("PKCE RFC fixture failed");
  if(Catalog.Zones.Distinct().Count()!=28||Catalog.Zones.Length!=28)throw new Exception("Zone catalog failed");
  if(new DisplaySettings().worlds.Count!=2)throw new Exception("Default clocks failed");
  using var model=new MainViewModel();model.ApplyPlace(Catalog.Places.Single(p=>p.Name=="London"));
  if(model.Settings.zone!="Europe/London"||model.LatitudeText!="51.5074"||model.LongitudeText!="-0.1278")throw new Exception("Place/coordinate binding failed");
  if(model.SaveCommand.CanExecute(null)||model.AuthorizeCommand.CanExecute(null)||model.NavigateCommand.CanExecute("next"))throw new Exception("Disconnected command gating failed");
  if(new DisplaySettings().animations)throw new Exception("Quality-first default failed");
  var energy=new EnergySettings();if(energy.vat_percent!=25.5||energy.margin_ct!=.45||energy.transmission_ct!=6||energy.fees_include_vat)throw new Exception("Electricity defaults failed");
  Write("studio-selftest.json",new{passed=7,total=7,framework="ShadUI 0.2.4 / Avalonia 12.1 / .NET 10"});return 0;
 }
 public static async Task<int> DeviceSmokeAsync(){
  using var model=new MainViewModel();await model.ConnectAsync();var device=model.Device;var st=await device.StatusAsync();
  if(!model.Connected||!model.SaveCommand.CanExecute(null))throw new Exception("Connected view model command gating failed");
  // Slots have fixed providers in Studio. Older helpers could leave this display
  // metadata blank; Save normalizes it without changing URLs or enable flags.
  for(int i=0;i<3;i++)model.Feeds[i].provider=new[]{"Apple","Google","Outlook"}[i];
  string before=JsonSerializer.Serialize(new{settings=model.Settings,feeds=model.Feeds,energy=model.Energy});
  await model.SaveAsync();await model.ReloadAsync();
  if(before!=JsonSerializer.Serialize(new{settings=model.Settings,feeds=model.Feeds,energy=model.Energy}))throw new Exception("View model settings round trip failed");
  await device.NavigateAsync("home");await device.NavigateAsync("prev");if((await device.StatusAsync()).GetProperty("page").GetInt32()!=6)throw new Exception("Previous-page wrap failed");
  await device.NavigateAsync("next");if((await device.StatusAsync()).GetProperty("page").GetInt32()!=0)throw new Exception("Next-page wrap failed");
  Write("studio-device-smoke.json",new{passed=true,firmware=st.GetProperty("version").GetString(),settings_read=true,settings_written=true,feeds_written=true,energy_written=true,navigation=true,viewmodel_roundtrip=true,command_gating=true,client="ShadUI / Avalonia / .NET 10"});return 0;
 }
 public static int RenderPreviews(){
  Directory.CreateDirectory("studio-previews");var window=new MainWindow();window.Show();
  for(int i=0;i<5;i++){
   window.SelectSection(i);Dispatcher.UIThread.RunJobs();
   using var bitmap=window.CaptureRenderedFrame()??throw new Exception("Headless renderer produced no frame");
   bitmap.Save($"studio-previews/{i+1}.png",new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
  }
  // Check the migration's highest-risk path: editing masked URLs must update the
  // same feed DTO that is saved over USB. This uses only a synthetic HTTPS URL.
  window.SelectSection(0);Dispatcher.UIThread.RunJobs();
  var masked=window.GetVisualDescendants().OfType<TextBox>().Where(t=>t.PasswordChar=='●').ToArray();
  if(masked.Length!=3)throw new Exception("Three masked subscription inputs expected");
  masked[1].Text="https://example.com/calendar.ics";Dispatcher.UIThread.RunJobs();
  if(window.Model.Feeds[1].url!="https://example.com/calendar.ics")throw new Exception("Subscription TwoWay binding failed");
  var buttonFont=new Avalonia.Media.Typeface(window.GetVisualDescendants().OfType<Button>().First().FontFamily).GlyphTypeface.FamilyName;
  if(buttonFont!="IBM Plex Mono")throw new Exception("Resolved control font is not IBM Plex Mono: "+buttonFont);
  window.SelectSection(3);Dispatcher.UIThread.RunJobs();var energyView=(Views.EnergyView)window.FindControl<ContentControl>("PageHost")!.Content!;
  energyView.FindControl<TextBox>("VatInput")!.Text="24.0";energyView.FindControl<TextBox>("MarginInput")!.Text="0.49";energyView.FindControl<TextBox>("TransmissionInput")!.Text="6.5";Dispatcher.UIThread.RunJobs();
  if(window.Model.EnergyVatText!="24.0"||window.Model.EnergyMarginText!="0.49"||window.Model.EnergyTransmissionText!="6.5")throw new Exception("Energy numeric input binding failed");
  energyView.FindControl<CheckBox>("FeesTaxInput")!.IsChecked=true;Dispatcher.UIThread.RunJobs();
  if(!window.Model.Energy.fees_include_vat||!energyView.FindControl<TextBlock>("GrossFeesFormula")!.IsVisible||energyView.FindControl<TextBlock>("NetFeesFormula")!.IsVisible)throw new Exception("Energy tax-mode/formula binding failed");
  energyView.FindControl<ComboBox>("EnergySourceInput")!.SelectedIndex=2;Dispatcher.UIThread.RunJobs();if(!window.Model.CustomEnergySource)throw new Exception("Energy source binding failed");
  window.FindControl<Button>("Nav2")!.RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(Button.ClickEvent));Dispatcher.UIThread.RunJobs();
  if(window.FindControl<ContentControl>("PageHost")!.Content is not Views.SpotifyView)throw new Exception("Sidebar click navigation failed");
  window.Width=1040;window.Height=720;Dispatcher.UIThread.RunJobs();
  using(var small=window.CaptureRenderedFrame()??throw new Exception("Minimum size render failed"))small.Save("studio-previews/minimum-size.png",new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
  Write("studio-ui-check.json",new{passed=true,rendered_pages=5,masked_url_twoway=true,energy_input_tax_and_source_bindings=true,sidebar_click=true,minimum_size=new[]{1040,720},button_font=buttonFont,library="ShadUI 0.2.4 (pinned NuGet package)",interactive_window_opened=false,rendering="Avalonia Headless + Skia; real application visual tree"});
  window.Close();return 0;
 }
 private static void Write(string path,object value)=>File.WriteAllText(path,JsonSerializer.Serialize(value,new JsonSerializerOptions{WriteIndented=true}));
}
