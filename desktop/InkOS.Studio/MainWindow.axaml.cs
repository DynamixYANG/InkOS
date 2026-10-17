using Avalonia.Controls;
using Avalonia.Interactivity;
using InkOS.Studio.Views;
namespace InkOS.Studio;
public partial class MainWindow:ShadUI.Window {
 public MainViewModel Model{get;}=new();
 private readonly UserControl[] pages=[new CalendarsView(),new PlaceView(),new SpotifyView(),new EnergyView(),new DeviceView(),new NetworkView()];
 public MainWindow(){InitializeComponent();DataContext=Model;SelectSection(0);Closed+=(_,_)=>Model.Dispose();}
 private void SectionClick(object? sender,RoutedEventArgs e){if(sender is Button{Tag:string tag})SelectSection(int.Parse(tag));}
 public void SelectSection(int index){PageHost.Content=pages[index];for(int i=0;i<pages.Length;i++)this.FindControl<Button>($"Nav{i}")!.Classes.Set("active",i==index);}
}
