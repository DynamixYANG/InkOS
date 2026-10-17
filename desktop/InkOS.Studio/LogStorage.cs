using System.Text.Json;
namespace InkOS.Studio;
/// <summary>Parsed fs_info reply (firmware 0.7.8+).</summary>
public sealed class StorageInfo {
 public long Total,Used,Free,Reserve,Block;
 public long Logs,LogFiles,Calendars,Energy,Other;
 public long CurrentBytes,ArchiveBytes,ArchiveCount,Budget,MaxFiles,RotateBytes;
 public long OldestArchive,NewestArchive,FontsPartition,FontsUsed;
 public int RetentionDays;public bool ClockValid;
 /// <summary>Block rounding and LittleFS metadata: used minus the file bytes.</summary>
 public long Overhead=>Math.Max(0,Used-Logs-Calendars-Energy-Other);
 public static StorageInfo Parse(JsonElement v){
  long L(JsonElement o,string k)=>o.TryGetProperty(k,out var x)&&x.ValueKind==JsonValueKind.Number?x.GetInt64():0;
  var fs=v.GetProperty("littlefs");var c=v.GetProperty("categories");var log=v.GetProperty("log");var f=v.GetProperty("fonts");
  return new(){Total=L(fs,"total_bytes"),Used=L(fs,"used_bytes"),Free=L(fs,"free_bytes"),Reserve=L(fs,"reserve_bytes"),Block=L(fs,"block_bytes"),
   Logs=L(c.GetProperty("logs"),"bytes"),LogFiles=L(c.GetProperty("logs"),"files"),Calendars=L(c.GetProperty("calendars"),"bytes"),Energy=L(c.GetProperty("energy"),"bytes"),Other=L(c.GetProperty("other"),"bytes"),
   CurrentBytes=L(log,"current_bytes"),ArchiveBytes=L(log,"archive_bytes"),ArchiveCount=L(log,"archive_count"),Budget=L(log,"archive_budget_bytes"),MaxFiles=L(log,"archive_max_files"),RotateBytes=L(log,"rotate_bytes"),
   OldestArchive=L(log,"oldest_archive_time"),NewestArchive=L(log,"newest_archive_time"),RetentionDays=(int)L(log,"retention_days"),
   ClockValid=log.TryGetProperty("clock_valid",out var cv)&&cv.ValueKind==JsonValueKind.True,
   FontsPartition=L(f,"partition_bytes"),FontsUsed=L(f,"used_bytes")};
 }
}
public sealed record StorageSegment(string Label,long Bytes,string Color);
public sealed record CapacityEstimate(double BytesPerDay,bool Measured,double DaysFit,bool Warn,string Text);
public static class LogStorage {
 public static readonly int[] RetentionDays=[7,21,35,90,180];
 public static readonly string[] RetentionLabels=["1 week","3 weeks","5 weeks","3 months","6 months"];
 /// <summary>Measured on 0.7.7/0.7.8 hardware: ~350 KB/day raw when idle (minute clock
 /// refreshes, weather, calendar), rotated archives compress to ~10 %.</summary>
 public const double TypicalBytesPerDay=36*1024, TypicalArchiveBytes=6.8*1024;
 public const double MinMeasuredSpanSeconds=12*3600;
 public static StorageSegment[] Segments(StorageInfo s){
  long freeAbove=Math.Max(0,s.Free-s.Reserve),reserve=Math.Min(s.Free,s.Reserve);
  return [new("Logs",s.Logs,"#202020"),new("Calendars",s.Calendars,"#5E5E5E"),new("Energy",s.Energy,"#8C8C8C"),
   new("Other + FS",s.Other+s.Overhead,"#BDBDBD"),new("Free",freeAbove,"#F1F1F0"),new("Reserve",reserve,"#E4E4E2")];
 }
 /// <summary>Days of logs the byte budget and file cap allow at the archive rate. Uses the
 /// device's own archives when they span at least 12 h, else the typical idle rate.</summary>
 public static CapacityEstimate Estimate(StorageInfo s,int retentionDays){
  bool measured=s.ArchiveCount>=2&&s.OldestArchive>0&&s.NewestArchive-s.OldestArchive>=MinMeasuredSpanSeconds;
  // Forced or very recent rotations are not representative: average archive size only
  // from a measured history, else the typical 64 KB-rotation archive.
  double avg=measured?(double)s.ArchiveBytes/s.ArchiveCount:TypicalArchiveBytes;
  // Oldest archive's contents predate its timestamp: rate over the remaining archives.
  double rate=measured?(s.ArchiveBytes-avg)/((s.NewestArchive-s.OldestArchive)/86400.0):TypicalBytesPerDay;
  if(rate<=0)rate=TypicalBytesPerDay;
  // The device budgets whole LittleFS blocks per archive.
  double block=s.Block>0?s.Block:4096,cost=Math.Ceiling(avg/block)*block,days=Math.Min(s.Budget/cost,s.MaxFiles)*avg/rate;
  bool warn=days<retentionDays;
  string text=$"Archive budget {LogArchive.Size(s.Budget)} or {s.MaxFiles} files · {(measured?"measured":"typical")} {LogArchive.Size((long)rate)}/day compressed · ≈ {days:0} days fit";
  return new(rate,measured,days,warn,text);
 }
}
