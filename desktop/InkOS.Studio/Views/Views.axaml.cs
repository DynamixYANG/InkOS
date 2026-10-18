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
 public LogsView(){
  InitializeComponent();
  DataContextChanged+=(_,_)=>{if(DataContext is MainViewModel m){m.PropertyChanged+=(_,e)=>{if(e.PropertyName==nameof(MainViewModel.StorageSegments))DrawStorage(m);};DrawStorage(m);}};
 }
 /// <summary>Proportional bar + legend; star columns keep it resolution independent.</summary>
 private void DrawStorage(MainViewModel model){
  var bar=this.FindControl<Grid>("StorageBar")!;var legend=this.FindControl<WrapPanel>("StorageLegend")!;
  bar.ColumnDefinitions.Clear();bar.Children.Clear();legend.Children.Clear();
  var segments=model.StorageSegments;long total=Math.Max(1,segments.Sum(s=>s.Bytes));
  foreach(var seg in segments){
   if(seg.Bytes<=0)continue;
   double share=Math.Max(seg.Bytes/(double)total,0.004); // keep tiny segments visible
   bar.ColumnDefinitions.Add(new ColumnDefinition(share,Avalonia.Controls.GridUnitType.Star));
   var brush=Avalonia.Media.SolidColorBrush.Parse(seg.Color);
   var cell=new Border{Background=brush};if(seg.Label=="Reserve")cell.BorderBrush=Avalonia.Media.SolidColorBrush.Parse("#B8B8B8");if(seg.Label=="Reserve")cell.BorderThickness=new Avalonia.Thickness(1,0,0,0);
   Grid.SetColumn(cell,bar.ColumnDefinitions.Count-1);bar.Children.Add(cell);
   var key=new StackPanel{Orientation=Avalonia.Layout.Orientation.Horizontal,Spacing=6,Margin=new Avalonia.Thickness(0,0,18,4)};
   key.Children.Add(new Border{Width=10,Height=10,Background=brush,BorderBrush=Avalonia.Media.SolidColorBrush.Parse("#C8C8C8"),BorderThickness=new Avalonia.Thickness(1),VerticalAlignment=Avalonia.Layout.VerticalAlignment.Center});
   key.Children.Add(new TextBlock{Text=$"{seg.Label} {LogArchive.Size(seg.Bytes)}",FontSize=11});
   legend.Children.Add(key);
  }
 }
 private async void SaveLog(object? sender,Avalonia.Interactivity.RoutedEventArgs e){
  if(DataContext is not MainViewModel model||TopLevel.GetTopLevel(this) is not { } top)return;
  var file=await top.StorageProvider.SaveFilePickerAsync(new(){Title="Save InkOS log",SuggestedFileName=model.SuggestedLogFileName});
  if(file?.TryGetLocalPath() is {} path){try{await model.SaveOpenedLogAsync(path);}catch(Exception ex){model.Status=ex is InvalidOperationException or IOException or UnauthorizedAccessException?ex.Message:"Could not save the log file.";}}
 }
}
