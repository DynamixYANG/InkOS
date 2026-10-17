using System.Text.Json.Serialization;
namespace InkOS.Studio;
public sealed class ClockSettings { public string label {get;set;}="";public string zone {get;set;}="UTC"; }
public sealed class DisplaySettings {
 public string location {get;set;}="Oulu";public string zone {get;set;}="Europe/Helsinki";
 public double latitude {get;set;}=65.0121;public double longitude {get;set;}=25.4651;
 public int weather_source {get;set;}=0;public bool animations {get;set;}=false;
 public int page_switch_delay_ms {get;set;}=500;
 public int encoder_lock_ms {get;set;}=2000;
 public string met_contact {get;set;}="";
 public List<ClockSettings> worlds {get;set;}=[new(){label="Beijing",zone="Asia/Shanghai"},new(){label="London",zone="Europe/London"}];
}
public sealed class CalendarFeed {public string name {get;set;}="";public string provider {get;set;}="";public string url {get;set;}="";public bool enabled {get;set;}=false;}
public sealed class EnergySettings:System.ComponentModel.INotifyPropertyChanged {
 public event System.ComponentModel.PropertyChangedEventHandler? PropertyChanged;
 public bool enabled{get;set;}=true;public int provider{get;set;}=0;
 public double vat_percent{get;set;}=25.5;public double margin_ct{get;set;}=.45;public double transmission_ct{get;set;}=6;
 private bool feesTaxed;public bool fees_include_vat{get=>feesTaxed;set{feesTaxed=value;PropertyChanged?.Invoke(this,new(nameof(fees_include_vat)));}}
 public string endpoint{get;set;}="https://sahkotin.fi/prices";public string api_header{get;set;}="Authorization";
 public bool has_key{get;set;}=false;
}
public sealed record Place(string Name,double Latitude,double Longitude,string Zone) {public override string ToString()=>Name;}
public static class Catalog {
 public static string[] Zones {get;}=["UTC","Europe/Helsinki","Europe/London","Europe/Stockholm","Europe/Oslo","Europe/Berlin","Europe/Paris","Europe/Tallinn","Europe/Athens","Europe/Zurich","Europe/Madrid","Europe/Rome","Europe/Moscow","Asia/Shanghai","Asia/Hong_Kong","Asia/Singapore","Asia/Tokyo","Asia/Seoul","Asia/Kolkata","Asia/Dubai","America/New_York","America/Chicago","America/Denver","America/Los_Angeles","America/Phoenix","Australia/Sydney","Australia/Brisbane","Pacific/Honolulu"];
 public static Place[] Places {get;}=[new("Oulu",65.0121,25.4651,"Europe/Helsinki"),new("Helsinki",60.1699,24.9384,"Europe/Helsinki"),new("London",51.5074,-0.1278,"Europe/London"),new("Beijing",39.9042,116.4074,"Asia/Shanghai"),new("Stockholm",59.3293,18.0686,"Europe/Stockholm"),new("Oslo",59.9139,10.7522,"Europe/Oslo"),new("Berlin",52.5200,13.4050,"Europe/Berlin"),new("New York",40.7128,-74.0060,"America/New_York"),new("Tokyo",35.6762,139.6503,"Asia/Tokyo"),new("Sydney",-33.8688,151.2093,"Australia/Sydney")];
}
