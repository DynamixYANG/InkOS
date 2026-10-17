using Avalonia.Platform.Storage;
using Avalonia.Controls;
namespace InkOS.Studio.Views;
public partial class CalendarsView:UserControl {public CalendarsView()=>InitializeComponent();}
public partial class ICloudView:UserControl {public ICloudView()=>InitializeComponent();}
public partial class SpotifyView:UserControl {public SpotifyView()=>InitializeComponent();}
public partial class NetworkView:UserControl {
 public NetworkView()=>InitializeComponent();
 private async void ChooseFirmware(object? sender,Avalonia.Interactivity.RoutedEventArgs e){
  if(DataContext is not MainViewModel model||TopLevel.GetTopLevel(this) is not { } top)return;
  var files=await top.StorageProvider.OpenFilePickerAsync(new(){Title="InkOS OTA package",AllowMultiple=false,FileTypeFilter=[new("InkOS OTA ZIP"){Patterns=["*.zip"]}]});
  if(files.Count==1&&files[0].TryGetLocalPath() is {} path)model.ChooseFirmware(path);
 }
}
public partial class DeviceView:UserControl {public DeviceView()=>InitializeComponent();}
public partial class EnergyView:UserControl {public EnergyView()=>InitializeComponent();}
public partial class PlaceView:UserControl {
 public PlaceView()=>InitializeComponent();
 private void PlaceChanged(object? sender,SelectionChangedEventArgs e){if(DataContext is MainViewModel model&&sender is ComboBox{SelectedItem:Place place})model.ApplyPlace(place);}
}
public partial class LogsView:UserControl {
 public LogsView()=>InitializeComponent();
 private async void SaveLog(object? sender,Avalonia.Interactivity.RoutedEventArgs e){
  if(DataContext is not MainViewModel model||TopLevel.GetTopLevel(this) is not { } top)return;
  var file=await top.StorageProvider.SaveFilePickerAsync(new(){Title="Save InkOS log",SuggestedFileName=model.SuggestedLogFileName});
  if(file?.TryGetLocalPath() is {} path){try{await model.SaveOpenedLogAsync(path);}catch(Exception ex){model.Status=ex is InvalidOperationException or IOException or UnauthorizedAccessException?ex.Message:"Could not save the log file.";}}
 }
}
