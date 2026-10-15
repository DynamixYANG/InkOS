using System.Windows.Input;
namespace InkOS.Studio;
/// <summary>UI-neutral commands; device operations remain serialized in DeviceClient.</summary>
public sealed class StudioCommand(Func<object?,Task> execute,Func<bool>? canExecute=null):ICommand {
 public event EventHandler? CanExecuteChanged;
 public bool CanExecute(object? parameter)=>canExecute?.Invoke()??true;
 public async void Execute(object? parameter){if(CanExecute(parameter))await execute(parameter);}
 public void Refresh()=>CanExecuteChanged?.Invoke(this,EventArgs.Empty);
}
