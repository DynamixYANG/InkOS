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
  if(new DisplaySettings().page_switch_delay_ms!=500||!MainViewModel.SupportsFirmware("0.7.0")||!MainViewModel.SupportsFirmware("0.6.0")||MainViewModel.SupportsFirmware("0.8.0")||MainViewModel.SupportsFirmware("invalid"))throw new Exception("Navigation default / firmware compatibility failed");
  if(new DisplaySettings().encoder_lock_ms!=2000)throw new Exception("Encoder lock default failed");
  foreach(var valid in new[]{"0","2","10","0.125"}){model.EncoderLockText=valid;model.ValidateEncoderLock();if(model.Settings.encoder_lock_ms!=(int)(decimal.Parse(valid,System.Globalization.CultureInfo.InvariantCulture)*1000))throw new Exception("Encoder lock conversion failed");}
  foreach(var invalid in new[]{"-1","10.001","NaN","2.0001"}){model.EncoderLockText=invalid;try{model.ValidateEncoderLock();throw new Exception("Invalid encoder lock accepted");}catch(InvalidOperationException){}}
  model.EncoderLockText="2";
  model.PageSwitchDelayText="1.25";model.ValidatePageSwitchDelay();if(model.Settings.page_switch_delay_ms!=1250)throw new Exception("Navigation seconds conversion failed");
  foreach(var invalid in new[]{"0","5.001","NaN","0.5001"}){model.PageSwitchDelayText=invalid;try{model.ValidatePageSwitchDelay();throw new Exception("Invalid navigation delay accepted");}catch(InvalidOperationException){}}
  var energy=new EnergySettings();if(energy.vat_percent!=25.5||energy.margin_ct!=.45||energy.transmission_ct!=6||energy.fees_include_vat)throw new Exception("Electricity defaults failed");
  NetworkChecks.ValidateInputs();
  FirmwareChecks.Validate().GetAwaiter().GetResult();
  ICloudChecks.Validate().GetAwaiter().GetResult();
  LogChecks.Validate();
  Write("studio-selftest.json",new{passed=14,total=14,device_logs_archive_filter_tail=true,storage_capacity_estimate=true,firmware_package_and_upload=true,icloud_discovery_and_security=true,navigation_delay_validation=true,encoder_lock_validation=true,firmware_07_compatibility=true,framework="ShadUI 0.2.4 / Avalonia 12.1 / .NET 10"});return 0;
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
  for(int i=0;i<7;i++){
   window.SelectSection(i);Dispatcher.UIThread.RunJobs();
   using var bitmap=window.CaptureRenderedFrame()??throw new Exception("Headless renderer produced no frame");
   bitmap.Save($"studio-previews/{i+1}.png",new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
  }
  // Check the migration's highest-risk path: editing masked URLs must update the
  // same feed DTO that is saved over USB. This uses only a synthetic HTTPS URL.
  window.SelectSection(0);Dispatcher.UIThread.RunJobs();
  var masked=((Views.CalendarsView)window.FindControl<ContentControl>("PageHost")!.Content!).GetVisualDescendants().OfType<TextBox>().Where(t=>t.PasswordChar=='●'&&t.Name!="ICloudPasswordInput").ToArray();
  if(masked.Length!=3)throw new Exception("Three masked subscription inputs expected");
  masked[1].Text="https://example.com/calendar.ics";Dispatcher.UIThread.RunJobs();
  if(window.Model.Feeds[1].url!="https://example.com/calendar.ics")throw new Exception("Subscription TwoWay binding failed");
  var cloud=((Views.CalendarsView)window.FindControl<ContentControl>("PageHost")!.Content!).GetVisualDescendants().OfType<Views.ICloudView>().Single();
  cloud.FindControl<TextBox>("ICloudUsernameInput")!.Text="fixture@example.invalid";
  cloud.FindControl<TextBox>("ICloudPasswordInput")!.Text="abcd-efgh-ijkl-mnop";
  window.Model.ICloudCalendars=[new("工作","https://p12-caldav.icloud.com/123/calendars/work/"),new("Personal","https://p12-caldav.icloud.com/123/calendars/home/")];Dispatcher.UIThread.RunJobs();
  cloud.FindControl<ComboBox>("ICloudCalendarInput")!.SelectedIndex=1;Dispatcher.UIThread.RunJobs();
  if(window.Model.ICloudUsername!="fixture@example.invalid"||window.Model.ICloudPassword!="abcd-efgh-ijkl-mnop"||window.Model.SelectedICloudCalendar?.Name!="Personal")throw new Exception("iCloud account/calendar TwoWay binding failed");
  window.Model.ICloudPassword="";
  var buttonFont=new Avalonia.Media.Typeface(window.GetVisualDescendants().OfType<Button>().First().FontFamily).GlyphTypeface.FamilyName;
  if(buttonFont!="IBM Plex Mono")throw new Exception("Resolved control font is not IBM Plex Mono: "+buttonFont);
  window.SelectSection(3);Dispatcher.UIThread.RunJobs();var energyView=(Views.EnergyView)window.FindControl<ContentControl>("PageHost")!.Content!;
  energyView.FindControl<TextBox>("VatInput")!.Text="24.0";energyView.FindControl<TextBox>("MarginInput")!.Text="0.49";energyView.FindControl<TextBox>("TransmissionInput")!.Text="6.5";Dispatcher.UIThread.RunJobs();
  if(window.Model.EnergyVatText!="24.0"||window.Model.EnergyMarginText!="0.49"||window.Model.EnergyTransmissionText!="6.5")throw new Exception("Energy numeric input binding failed");
  energyView.FindControl<CheckBox>("FeesTaxInput")!.IsChecked=true;Dispatcher.UIThread.RunJobs();
  if(!window.Model.Energy.fees_include_vat||!energyView.FindControl<TextBlock>("GrossFeesFormula")!.IsVisible||energyView.FindControl<TextBlock>("NetFeesFormula")!.IsVisible)throw new Exception("Energy tax-mode/formula binding failed");
  energyView.FindControl<ComboBox>("EnergySourceInput")!.SelectedIndex=2;Dispatcher.UIThread.RunJobs();if(!window.Model.CustomEnergySource)throw new Exception("Energy source binding failed");
  window.SelectSection(5);Dispatcher.UIThread.RunJobs();var network=(Views.NetworkView)window.FindControl<ContentControl>("PageHost")!.Content!;
  network.FindControl<TextBox>("WifiNameInput")!.Text="Example Network";
  network.FindControl<ComboBox>("WifiModeInput")!.SelectedIndex=1;
  network.FindControl<TextBox>("WifiPasswordInput")!.Text="example-password";
  network.FindControl<TextBox>("PairingKeyInput")!.Text=new string('a',64);Dispatcher.UIThread.RunJobs();
  if(window.Model.WifiSsid!="Example Network"||window.Model.WifiPasswordMode!=1||window.Model.WifiPassword!="example-password"||window.Model.PairingKey!=new string('a',64))throw new Exception("Network TwoWay bindings failed");
  window.SelectSection(4);Dispatcher.UIThread.RunJobs();var deviceView=(Views.DeviceView)window.FindControl<ContentControl>("PageHost")!.Content!;
  deviceView.FindControl<TextBox>("PageSwitchDelayInput")!.Text="0.8";Dispatcher.UIThread.RunJobs();window.Model.ValidatePageSwitchDelay();
  deviceView.FindControl<TextBox>("EncoderLockInput")!.Text="2.5";Dispatcher.UIThread.RunJobs();window.Model.ValidateEncoderLock();
  if(window.Model.Settings.encoder_lock_ms!=2500)throw new Exception("Encoder lock TwoWay binding failed");
  if(window.Model.Settings.page_switch_delay_ms!=800)throw new Exception("Page selection delay TwoWay binding failed");
  window.FindControl<Button>("Nav2")!.RaiseEvent(new Avalonia.Interactivity.RoutedEventArgs(Button.ClickEvent));Dispatcher.UIThread.RunJobs();
  if(window.FindControl<ContentControl>("PageHost")!.Content is not Views.SpotifyView)throw new Exception("Sidebar click navigation failed");
  window.Width=1040;window.Height=720;Dispatcher.UIThread.RunJobs();
  using(var small=window.CaptureRenderedFrame()??throw new Exception("Minimum size render failed"))small.Save("studio-previews/minimum-size.png",new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
  window.SelectSection(5);window.Model.ConnectionMode=1;window.Model.NetworkAddress="192.168.1.100";window.Model.PairingKey="";Dispatcher.UIThread.RunJobs();
  using(var ipSmall=window.CaptureRenderedFrame()??throw new Exception("IP minimum size render failed"))ipSmall.Save("studio-previews/network-ip-minimum.png",new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
  window.Width=1160;window.Height=860;Dispatcher.UIThread.RunJobs();
  using(var ipView=window.CaptureRenderedFrame()??throw new Exception("IP render failed"))ipView.Save("studio-previews/network-ip.png",new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
  var pageScroll=window.FindControl<ScrollViewer>("PageScroll")!;
  pageScroll.Offset=new Avalonia.Vector(0,pageScroll.Extent.Height);Dispatcher.UIThread.RunJobs();
  using(var otaView=window.CaptureRenderedFrame()??throw new Exception("Firmware section render failed"))otaView.Save("studio-previews/network-ota.png",new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
  // Logs view with synthetic data (no device): filter binding and screenshot.
  window.Width=1160;window.Height=860;window.Model.ConnectionMode=0;window.Model.LoadLogPreview();window.SelectSection(6);pageScroll.Offset=default;Dispatcher.UIThread.RunJobs();
  pageScroll.Offset=default;Dispatcher.UIThread.RunJobs();pageScroll.Offset=default;Dispatcher.UIThread.RunJobs();
  using(var logsView=window.CaptureRenderedFrame()??throw new Exception("Logs render failed"))logsView.Save("studio-previews/logs.png",new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
  var logs=(Views.LogsView)window.FindControl<ContentControl>("PageHost")!.Content!;
  if(logs.FindControl<Grid>("StorageBar")!.ColumnDefinitions.Count<5||!logs.FindControl<Border>("CapacityWarningBox")!.IsVisible)throw new Exception("Storage bar / 6-month warning not rendered");
  var fileList=logs.FindControl<ListBox>("LogFileList")!;fileList.Selection.Clear();fileList.Selection.Select(0);fileList.Selection.Select(1);Dispatcher.UIThread.RunJobs();
  if(window.Model.SelectedLogs.Count!=2||window.Model.DeletePrompt!="Delete 2 selected files? This cannot be undone.")throw new Exception("Multi-select binding failed");
  if(logs.FindControl<ComboBox>("RetentionInput")!.SelectedIndex!=4)throw new Exception("Retention dropdown binding failed");
  window.Model.ConfirmDelete=true;
  pageScroll.Offset=new Avalonia.Vector(0,560);Dispatcher.UIThread.RunJobs();
  using(var filesView=window.CaptureRenderedFrame()??throw new Exception("Logs files render failed"))filesView.Save("studio-previews/logs-files.png",new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
  window.Model.ConfirmDelete=false;pageScroll.Offset=new Avalonia.Vector(0,pageScroll.Extent.Height);Dispatcher.UIThread.RunJobs();
  logs.FindControl<TextBox>("LogFilterInput")!.Text="navigation";Dispatcher.UIThread.RunJobs();
  if(window.Model.LogText.Split('\n').Any(l=>!l.Contains("navigation"))||!logs.FindControl<TextBox>("LogViewer")!.Text!.Contains("dispatch"))throw new Exception("Log filter binding failed");
  if(logs.FindControl<ListBox>("LogFileList")!.ItemCount!=3)throw new Exception("Log file list binding failed");
  using(var filtered=window.CaptureRenderedFrame()??throw new Exception("Logs filter render failed"))filtered.Save("studio-previews/logs-filtered.png",new Avalonia.Media.Imaging.PngBitmapEncoderOptions());
  window.Model.LogFilter="";
  Write("studio-ui-check.json",new{passed=true,rendered_pages=7,logs_view=true,logs_filter_binding=true,logs_storage_bar=true,logs_multiselect=true,logs_retention_dropdown=true,masked_url_twoway=true,network_inputs_twoway=true,page_delay_twoway=true,encoder_lock_twoway=true,ip_minimum_size=true,energy_input_tax_and_source_bindings=true,sidebar_click=true,minimum_size=new[]{1040,720},button_font=buttonFont,library="ShadUI 0.2.4 (pinned NuGet package)",interactive_window_opened=false,rendering="Avalonia Headless + Skia; real application visual tree"});
  window.Close();return 0;
 }
 private static void Write(string path,object value)=>File.WriteAllText(path,JsonSerializer.Serialize(value,new JsonSerializerOptions{WriteIndented=true}));
}
