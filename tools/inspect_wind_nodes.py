"""Bounded, asynchronous read-only wind-node inventory; never a restore authority."""
import argparse,ctypes as C,hashlib,json,struct,time
from ctypes import wintypes as W
from pathlib import Path
EXPECTED='F8904E4B04BCA3B47BC52A683F6190365D2EB89EE8F44F8072759E9C5E04A553'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pid',required=True,type=int);p.add_argument('--output',required=True,type=Path);p.add_argument('--seconds',type=int,default=10);a=p.parse_args()
 if not 1<=a.seconds<=180: p.error('seconds must be 1..180')
 if C.sizeof(C.c_void_p)!=8:raise RuntimeError('64-bit Python required')
 k=C.WinDLL('kernel32',use_last_error=True);ps=C.WinDLL('psapi',use_last_error=True)
 k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE;k.CloseHandle.argtypes=[W.HANDLE]
 k.ReadProcessMemory.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
 k.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)]
 ps.EnumProcessModulesEx.argtypes=[W.HANDLE,C.POINTER(W.HMODULE),W.DWORD,C.POINTER(W.DWORD),W.DWORD]
 h=k.OpenProcess(0x410,False,a.pid)
 if not h:raise C.WinError(C.get_last_error())
 try:
  name=C.create_unicode_buffer(32768);n=W.DWORD(len(name))
  if not k.QueryFullProcessImageNameW(h,0,name,C.byref(n)):raise C.WinError(C.get_last_error())
  exe=Path(name.value)
  if exe.name.lower()!='soulcaliburvi.exe' or hashlib.sha256(exe.read_bytes()).hexdigest().upper()!=EXPECTED:raise RuntimeError('Unsupported executable')
  mods=(W.HMODULE*1024)();needed=W.DWORD()
  if not ps.EnumProcessModulesEx(h,mods,C.sizeof(mods),C.byref(needed),3) or not needed.value:raise RuntimeError('Module enumeration failed')
  base=mods[0]
  def read(addr,size):
   if not 0x10000<=addr<=0x7fffffffffff-size:raise ValueError('Invalid address')
   b=C.create_string_buffer(size);got=C.c_size_t()
   if not k.ReadProcessMemory(h,addr,b,size,C.byref(got)) or got.value!=size:raise RuntimeError(f'Read failed address={addr:#x} size={size} winerror={C.get_last_error()} bytes={got.value}')
   return b.raw
  def ptr(addr):return struct.unpack('<Q',read(addr,8))[0]
  samples=[]
  for tick in range(a.seconds*4):
   sample={'tick':tick,'nodes':[],'errors':[]}
   for side in range(2):
    try:
     player=ptr(base+0x470de90+side*8)
     if player<0x10000:raise ValueError('Player unavailable; no active object at verified slot')
     root=player+0x29120;heads=read(root+0x10,24);seen=set();total=0
     for li,off in enumerate((0,16)):
      group=struct.unpack_from('<Q',heads,off)[0];groups=set()
      while group:
       if group in groups or len(groups)>=64:raise ValueError('Group cycle/limit')
       groups.add(group);g=read(group,0x58);node=struct.unpack_from('<Q',g,0x48)[0]
       while node:
        if node in seen or total>=512:raise ValueError('Node cycle/limit')
        seen.add(node);total+=1;b=read(node,0x30);vt=struct.unpack_from('<Q',b)[0]-base;target,parent=struct.unpack_from('<hh',b,0x20)
        sample['nodes'].append(dict(side=side,list=li,address=node,vtable_rva=hex(vt),weapon=vt==0x3e88d78,target=target,parent=parent,matrix_index=target+2))
        if read(node,0x30)!=b:raise ValueError('Node changed during read')
        node=struct.unpack_from('<Q',b,0x28)[0]
       if read(group,0x58)!=g:raise ValueError('Group changed during read')
       group=struct.unpack_from('<Q',g,0x40)[0]
     if ptr(base+0x470de90+side*8)!=player or read(root+0x10,24)!=heads:raise ValueError('Root changed during read')
    except (ValueError,RuntimeError) as e:sample['errors'].append(dict(side=side,error=str(e)))
   samples.append(sample);time.sleep(.25)
  a.output.parent.mkdir(parents=True,exist_ok=True)
  clean=[s for s in samples if not s['errors']]
  summary=dict(samples=len(samples),clean_samples=len(clean),samples_with_errors=len(samples)-len(clean),weapon_observations=sum(n['weapon'] for s in clean for n in s['nodes']),weapon_matrix_indices=sorted({n['matrix_index'] for s in clean for n in s['nodes'] if n['weapon']}))
  a.output.write_text(json.dumps(dict(schema=2,pid=a.pid,module_base=base,read_only=True,coherent_snapshot=False,summary=summary,samples=samples),indent=2))
  print('Saved',a.output,'weapon observations',sum(n['weapon'] for s in samples for n in s['nodes']),'samples with errors',sum(bool(s['errors']) for s in samples))
 finally:k.CloseHandle(h)
if __name__=='__main__':main()



