using System.Formats.Tar;
using System.IO.Compression;
using System.Text;
namespace InkOS.Studio;
/// <summary>Device log files: plain current.log or rotated ustar+gzip archives.</summary>
public static class LogArchive {
 public const int MaxViewerLines=20000;
 public static bool IsArchive(string name)=>name.EndsWith(".tar.gz",StringComparison.OrdinalIgnoreCase);
 /// <summary>Decompresses a .tar.gz (System.Formats.Tar + GZipStream) or decodes a text log.</summary>
 public static string ToText(string name,byte[] data){
  if(!IsArchive(name))return Encoding.UTF8.GetString(data);
  using var gzip=new GZipStream(new MemoryStream(data),CompressionMode.Decompress);
  using var tar=new TarReader(gzip);
  var text=new StringBuilder();int entries=0;
  for(TarEntry? entry;(entry=tar.GetNextEntry())!=null;){
   if(entry.EntryType is not (TarEntryType.RegularFile or TarEntryType.V7RegularFile)||entry.DataStream==null)continue;
   using var reader=new StreamReader(entry.DataStream,new UTF8Encoding(false));
   if(entries++>0)text.Append('\n');
   text.Append(reader.ReadToEnd());
  }
  if(entries==0)throw new InvalidDataException("The archive contains no log file.");
  return text.ToString();
 }
 /// <summary>Case-insensitive line filter; empty filter keeps everything.</summary>
 public static string Filter(string text,string? filter){
  var lines=text.Split('\n');
  IEnumerable<string> kept=string.IsNullOrWhiteSpace(filter)?lines:lines.Where(l=>l.Contains(filter.Trim(),StringComparison.OrdinalIgnoreCase));
  var list=kept.ToList();
  if(list.Count>MaxViewerLines)list=list.Skip(list.Count-MaxViewerLines).ToList();
  return string.Join('\n',list);
 }
 /// <summary>Appends a fresh ring tail to earlier tail lines without duplicating the overlap.</summary>
 public static List<string> MergeTail(List<string> previous,IReadOnlyList<string> fresh,int keep=2000){
  int best=0;
  for(int k=Math.Min(previous.Count,fresh.Count);k>0;k--){
   bool same=true;
   for(int i=0;i<k&&same;i++)same=previous[previous.Count-k+i]==fresh[i];
   if(same){best=k;break;}
  }
  var merged=new List<string>(previous);merged.AddRange(fresh.Skip(best));
  return merged.Count>keep?merged.GetRange(merged.Count-keep,keep):merged;
 }
 public static List<string> Lines(string text)=>text.Split('\n',StringSplitOptions.RemoveEmptyEntries).ToList();
 public static string Size(long bytes)=>bytes<1024?$"{bytes} B":bytes<1024*1024?$"{bytes/1024.0:0.0} KB":$"{bytes/1048576.0:0.00} MB";
}
public sealed record LogFileItem(string Name,long Size,long Mtime){
 public string SizeText=>LogArchive.Size(Size);
 public string ModifiedText=>Mtime>1_600_000_000?DateTimeOffset.FromUnixTimeSeconds(Mtime).ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss"):"—";
 public string Kind=>LogArchive.IsArchive(Name)?"archive":"active";
}
