import sys,struct,json,zipfile
from pathlib import Path
sys.path.insert(0,str(Path('work/research-python').resolve()))
import pefile,capstone
from capstone.x86_const import X86_OP_MEM
out=Path('outputs/SC6Rollback/work/wind-object-61168')
p=pefile.PE(r'E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\SoulcaliburVI.exe',fast_load=True)
base=p.OPTIONAL_HEADER.ImageBase
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
ranges=[(0x334960,0x334b40),(0x333e80,0x333fd0),(0x332d60,0x333130),(0x333130,0x333550),(0x333550,0x333c20),(0x333c20,0x333e80),(0x331070,0x331090),(0x333fd0,0x334400)]
for start,end in ranges:
 lines=[]
 for ins in md.disasm(p.get_data(start,end-start),base+start):lines.append(f'{ins.address-base:08x}: {ins.mnemonic} {ins.op_str}')
 (out/f'{start:08x}.asm.txt').write_text('\n'.join(lines))
def sections(data):
 at=8;s={}
 while at<len(data):
  n,size=struct.unpack_from('<QQ',data,at);at+=16;name=data[at:at+n].decode();at+=n;s[name]=data[at:at+size];at+=size
 return s
def objects(image):
 count=struct.unpack_from('<I',image,32896)[0]
 assert count<=64
 result=[]
 for i in range(count):
  off=i*512;address,vt,size=struct.unpack_from('<QQI',image,off)
  result.append({'address':hex(address),'vtable':hex(vt),'size':size,'payload':image[off+32:off+32+size]})
 return result
z=zipfile.ZipFile(r'C:\Users\Ben\Downloads\SC6Rollback-Report-steam-inputs-46908-6d76e736.zip')
root=Path(r'E:\SteamLibrary\steamapps\common\SoulcaliburVI\SoulcaliburVI\Binaries\Win64\Mods\SC6Rollback\captures')
results=[]
for name,pid,read in [('Ben',61168,lambda n:(root/n).read_bytes()),('Partner',46908,z.read)]:
 s=sections(read(f'steam-inputs-{pid}-retained-evidence.bin'));w=s['wind.lifecycle'];span=(len(w)-8)//2
 expected,actual=objects(w[:span]),objects(w[span:span*2]);r={'side':name,'expected_count':len(expected),'actual_count':len(actual),'objects':[],'snapshots':[]}
 for i,(a,b) in enumerate(zip(expected,actual)):
  diffs=[{'offset':hex(k),'expected':x,'actual':y} for k,(x,y) in enumerate(zip(a['payload'],b['payload'])) if x!=y and not 0x10<=k<0x20]
  r['objects'].append({'index':i,'expected_address':a['address'],'actual_address':b['address'],'type_rva':hex(int(a['vtable'],16)-struct.unpack('<Q',s['module'])[0]),'size':a['size'],'diffs':diffs,'expected_08':a['payload'][8:16].hex(),'actual_08':b['payload'][8:16].hex()})
 for j in range(4):
  ss=sections(read(f'steam-inputs-{pid}-cycle-1-owned-snapshot-{j}.bin'));oo=objects(ss['effects.image']);r['snapshots'].append([{'address':x['address'],'08':x['payload'][8:16].hex()} for x in oo])
 results.append(r)
(out/'comparison.json').write_text(json.dumps(results,indent=2))
for r in results:
 print(r['side'],r['expected_count'],r['actual_count'])
 for o in r['objects']:
  if o['diffs']:print('object',o['index'],'addresses',o['expected_address'],o['actual_address'],'type',o['type_rva'],'08',o['expected_08'],o['actual_08'],'diff count',len(o['diffs']))
 print('snapshot heads', [row[0] if row else None for row in r['snapshots']])
