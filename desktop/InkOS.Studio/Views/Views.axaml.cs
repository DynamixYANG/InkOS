using Avalonia.Controls;
namespace InkOS.Studio.Views;
public partial class CalendarsView:UserControl {public CalendarsView()=>InitializeComponent();}
public partial class SpotifyView:UserControl {public SpotifyView()=>InitializeComponent();}
public partial class DeviceView:UserControl {public DeviceView()=>InitializeComponent();}
public partial class EnergyView:UserControl {public EnergyView()=>InitializeComponent();}
public partial class PlaceView:UserControl {
 public PlaceView()=>InitializeComponent();
 private void PlaceChanged(object? sender,SelectionChangedEventArgs e){if(DataContext is MainViewModel model&&sender is ComboBox{SelectedItem:Place place})model.ApplyPlace(place);}
}
