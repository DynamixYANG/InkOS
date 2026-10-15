using System.Globalization;
namespace InkOS.Studio;
public sealed partial class MainViewModel {
 private EnergySettings energy=new();
 public EnergySettings Energy{get=>energy;set{energy=value;EnergyVatText=value.vat_percent.ToString(CultureInfo.InvariantCulture);EnergyMarginText=value.margin_ct.ToString(CultureInfo.InvariantCulture);EnergyTransmissionText=value.transmission_ct.ToString(CultureInfo.InvariantCulture);EnergyKeyText="";ClearEnergyKey=false;Notify();Notify(nameof(EnergyVatText));Notify(nameof(EnergyMarginText));Notify(nameof(EnergyTransmissionText));Notify(nameof(EnergyProviderIndex));Notify(nameof(CustomEnergySource));Notify(nameof(EnergyKeyStatus));Notify(nameof(EnergyKeyText));Notify(nameof(ClearEnergyKey));}}
 public string EnergyVatText{get;set;}="25.5";public string EnergyMarginText{get;set;}="0.45";public string EnergyTransmissionText{get;set;}="6";
 public string EnergyKeyText{get;set;}="";public bool ClearEnergyKey{get;set;}=false;
 public string EnergyKeyStatus=>Energy.has_key?"A key is saved on the screen. Leave blank to keep it.":"No API key saved. Preset Finnish sources do not need one.";
 public bool CustomEnergySource=>Energy.provider==2;
 public int EnergyProviderIndex{get=>Energy.provider;set{Energy.provider=value;if(value==0)Energy.endpoint="https://sahkotin.fi/prices";else if(value==1)Energy.endpoint="https://api.porssisahko.net/v2/latest-prices.json";Notify();Notify(nameof(Energy));Notify(nameof(CustomEnergySource));}}
 public string[] EnergyHeaders=>["Authorization","X-API-Key"];
 private static double EnergyNumber(string input,string name,double min,double max){if(!double.TryParse(input,NumberStyles.Float,CultureInfo.InvariantCulture,out var value)||!double.IsFinite(value)||value<min||value>max)throw new InvalidOperationException($"Check {name}: expected {min} to {max}, using a decimal point.");return value;}
 private void ValidateEnergy(){
  Energy.vat_percent=EnergyNumber(EnergyVatText,"VAT percent",0,100);Energy.margin_ct=EnergyNumber(EnergyMarginText,"margin ct/kWh",-100,1000);Energy.transmission_ct=EnergyNumber(EnergyTransmissionText,"transmission ct/kWh",0,1000);
  if(Energy.provider<0||Energy.provider>2||Energy.endpoint.Length>512||!Uri.TryCreate(Energy.endpoint,UriKind.Absolute,out var url)||url.Scheme!="https"||Energy.endpoint.Contains('\r')||Energy.endpoint.Contains('\n'))throw new InvalidOperationException("Use a valid HTTPS energy endpoint.");
  if(!EnergyHeaders.Contains(Energy.api_header)||EnergyKeyText.Length>256||EnergyKeyText.Contains('\r')||EnergyKeyText.Contains('\n'))throw new InvalidOperationException("Check the energy API header and key.");
 }
 private async Task SaveEnergyAsync(){
  int mode=ClearEnergyKey?2:string.IsNullOrWhiteSpace(EnergyKeyText)?0:1;
  await Device.SaveAsync(new{cmd="energy_set",energy=new{Energy.enabled,Energy.provider,Energy.vat_percent,Energy.margin_ct,Energy.transmission_ct,Energy.fees_include_vat,Energy.endpoint,Energy.api_header,api_key=mode==1?EnergyKeyText:"",key_mode=mode}});
  if(mode==1)Energy.has_key=true;else if(mode==2)Energy.has_key=false;
  EnergyKeyText="";ClearEnergyKey=false;Notify(nameof(EnergyKeyText));Notify(nameof(ClearEnergyKey));Notify(nameof(EnergyKeyStatus));
 }
}
