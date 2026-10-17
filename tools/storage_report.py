"""Plot an InkOS storage_info snapshot. Input contains capacities, never credentials."""
import argparse,json
from pathlib import Path
def render(snapshot,output):
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 from matplotlib.font_manager import FontProperties
 root=Path(__file__).resolve().parents[1]
 face=FontProperties(fname=root/'assets/fonts/source-han-sans/SourceHanSansSC-Light.otf')
 plt.rcParams.update({'font.family':face.get_name(),'axes.unicode_minus':False})
 from matplotlib import font_manager
 font_manager.fontManager.addfont(root/'assets/fonts/source-han-sans/SourceHanSansSC-Light.otf')
 parts=snapshot['partitions'];ext=snapshot['external'];mib=1048576
 fig=plt.figure(figsize=(12,8.6),facecolor='#faf9f5')
 grid=fig.add_gridspec(2,2,height_ratios=[1.2,1],hspace=.25,wspace=.25)
 colors=['#40586c','#83949b','#d4b887','#d9d8d2']
 ax=fig.add_subplot(grid[0,0]);values=[p['bytes'] for p in parts]+[snapshot['boot_metadata_reserved']]
 wedges,_=ax.pie(values,startangle=90,counterclock=False,colors=colors,wedgeprops={'edgecolor':'#faf9f5','linewidth':2})
 ax.set_title('内部启动 Flash · 16 MiB',fontproperties=face,fontsize=18,pad=16)
 ax.legend(wedges,[f"{p['name']}  {p['bytes']/mib:.4g} MiB" for p in parts]+['启动 / NVS / OTA 记录及预留 · 128 KiB'],loc='lower center',bbox_to_anchor=(.5,-.13),frameon=False,prop=face,ncol=2)
 ax=fig.add_subplot(grid[0,1]);ax.pie([ext['used_bytes'],ext['free_bytes']],startangle=90,counterclock=False,colors=[colors[0],colors[3]],wedgeprops={'edgecolor':'#faf9f5','linewidth':2})
 ax.set_title('外部 U5 · 4 MiB LittleFS',fontproperties=face,fontsize=18,pad=16)
 ax.legend([f"已用 {ext['used_bytes']/1024:,.0f} KiB",f"可用 {ext['free_bytes']/mib:.3f} MiB"],loc='lower center',bbox_to_anchor=(.5,-.07),frameon=False,prop=face,ncol=2)
 ax=fig.add_subplot(grid[1,:]);ax.axis('off')
 rows=[]
 for p in parts:
  rows.append([p['name'],f"0x{p['offset']:06X}",f"{p['bytes']:,}",f"{p['used_bytes']:,}",f"{p['free_bytes']:,}"])
 rows.append(['外部 LittleFS','0x000000',f"{ext['filesystem_bytes']:,}",f"{ext['used_bytes']:,}",f"{ext['free_bytes']:,}"])
 table=ax.table(cellText=rows,colLabels=['区域','起始地址','分区容量 / B','已用 / B','剩余 / B'],cellLoc='right',colLoc='right',loc='upper center',colWidths=[.20,.17,.21,.21,.21])
 table.auto_set_font_size(False);table.set_fontsize(11);table.scale(1,2.15)
 for (row,col),cell in table.get_celld().items():
  cell.set_text_props(fontproperties=face,color='#20282b');cell.set_edgecolor('#deddd7');cell.set_linewidth(.5);cell.set_facecolor('#f0efe9' if row==0 else '#faf9f5')
 note='固件槽的“剩余”是该槽的升级容量，不是文件空间；两槽分别保留完整应用。\n字库无损压缩，剩余空间仅属于字库分区。LittleFS 已用包含文件系统元数据，随同步变化。'
 ax.text(0,.19,note,fontproperties=face,fontsize=11,color='#4d565b',va='top',linespacing=1.7)
 n=snapshot.get('nvs',{})
 ax.text(0,-.02,f"NVS 独立保留 24 KiB · 条目已用 {n.get('used_entries','—')} / 可用 {n.get('free_entries','—')} · 1 MiB = 1,048,576 B",fontproperties=face,fontsize=10,color='#4d565b')
 fig.subplots_adjust(left=.055,right=.945,top=.94,bottom=.08)
 output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
 fig.savefig(output,dpi=180,facecolor=fig.get_facecolor());plt.close(fig)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--snapshot',type=Path);p.add_argument('--ip');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.ip:
  import getpass
  from ota import Remote
  data=Remote(a.ip,getpass.getpass('Pairing key (hidden): ').strip()).call({'cmd':'storage_info'})
  snapshot=a.snapshot or a.output.with_suffix('.json');snapshot.parent.mkdir(parents=True,exist_ok=True);snapshot.write_text(json.dumps(data,indent=2)+'\n')
 elif a.snapshot:data=json.loads(a.snapshot.read_text())
 else:p.error('Provide --ip for a live device or --snapshot for a saved report')
 render(data,a.output)
if __name__=='__main__':main()
