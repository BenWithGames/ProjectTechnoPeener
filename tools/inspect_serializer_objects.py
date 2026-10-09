"""Read-only serializer object inventory. Never calls game functions or writes memory."""
import argparse, ctypes as C, hashlib, json, struct
from ctypes import wintypes as W
from pathlib import Path
from observe_training import EXPECTED_SHA256


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid',type=int,required=True)
    ap.add_argument('--training-confirmed',action='store_true',required=True)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args()
    k=C.WinDLL('kernel32',use_last_error=True); p=C.WinDLL('psapi',use_last_error=True)
    k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD]; k.OpenProcess.restype=W.HANDLE
    k.CloseHandle.argtypes=[W.HANDLE]
    k.ReadProcessMemory.argtypes=[W.HANDLE,C.c_void_p,C.c_void_p,C.c_size_t,C.POINTER(C.c_size_t)]
    k.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)]
    p.EnumProcessModulesEx.argtypes=[W.HANDLE,C.POINTER(W.HMODULE),W.DWORD,C.POINTER(W.DWORD),W.DWORD]
    h=k.OpenProcess(0x410,False,a.pid)
    if not h: raise C.WinError(C.get_last_error())
    try:
        name=C.create_unicode_buffer(32768); n=W.DWORD(len(name))
        if not k.QueryFullProcessImageNameW(h,0,name,C.byref(n)): raise C.WinError(C.get_last_error())
        exe=Path(name.value)
        if exe.name.lower()!='soulcaliburvi.exe' or hashlib.sha256(exe.read_bytes()).hexdigest().upper()!=EXPECTED_SHA256:
            raise RuntimeError('Unsupported executable')
        mods=(W.HMODULE*1024)(); needed=W.DWORD()
        if not p.EnumProcessModulesEx(h,mods,C.sizeof(mods),C.byref(needed),3) or not needed.value:
            raise RuntimeError('Cannot enumerate modules')
        base=mods[0]
        def read(addr,size):
            b=C.create_string_buffer(size); got=C.c_size_t()
            if not k.ReadProcessMemory(h,addr,b,size,C.byref(got)) or got.value!=size:
                raise RuntimeError(f'Read failed at {addr:#x}')
            return b.raw
        def u64(addr): return struct.unpack('<Q',read(addr,8))[0]
        def describe(addr,slots):
            v=u64(addr)
            return {'object':hex(addr),'vtable':hex(v),'vtable_relative_to_exe':hex(v-base),
                    'methods_relative_to_exe':{hex(s):hex(u64(v+s)-base) for s in slots}}
        report={'schema':1,'pid':a.pid,'module_base':hex(base),'read_only':True,
                'atomic':False,'complete_snapshot':False,'players':[],'errors':[]}
        report['event_interface']=describe(u64(base+0x470D188),(0xC0,))
        report['outer_before']=int.from_bytes(read(base+0x470D0C4,4),'little')
        before=[u64(base+x) for x in (0x470DE90,0x470DE98,0x470F1A0,0x470F1A8)]
        for i,player in enumerate(before[:2]):
            entry={'pointer':hex(player),'recorder_pointer':hex(before[i+2]),'matches_recorder':player==before[i+2],'lists':[]}
            report['players'].append(entry)
            try:
                entry['buffers']=[describe(player+off,(0x28,)) for off in (0x35A0,0x27760)]
                for offset in (0x400,0x420,0x440):
                    head=u64(player+0x44078+offset); node=head; seen=set(); nodes=[]
                    entry['lists'].append({'head_offset':hex(offset),'head':hex(head),'nodes':nodes})
                    while node:
                        if node in seen or len(seen)>=64: raise RuntimeError('Cycle or 64-node limit')
                        seen.add(node); nodes.append(describe(node,(0x30,0x38))); node=u64(node+0x18)
            except RuntimeError as e: report['errors'].append(str(e))
        try:
            manager=u64(base+0x470DF28); report['manager']=describe(manager,(0,8))
            report['manager']['active_field']=hex(u64(manager+8))
            table=u64(manager+0x90); report['manager']['objects']=[]
            if table:
                for i in range(16):
                    obj=u64(table+0x270+8*i)
                    if obj: report['manager']['objects'].append({'index':i,**describe(obj,(0xC8,0xD0,0xD8,0xE0,0x100,0x108))})
            aux=u64(manager+0xA0)
            report['manager']['auxiliary']=hex(aux)
            report['manager']['auxiliary_objects']=[]
            if aux:
                for i in range(17):
                    obj=u64(aux+0x10+i*8)
                    if obj: report['manager']['auxiliary_objects'].append({'index':i,**describe(obj,(0x20,0x28))})
        except RuntimeError as e: report['errors'].append(str(e))
        after=[u64(base+x) for x in (0x470DE90,0x470DE98,0x470F1A0,0x470F1A8)]
        report['pointers_stable']=before==after
        report['outer_after']=int.from_bytes(read(base+0x470D0C4,4),'little')
        a.output.parent.mkdir(parents=True,exist_ok=True)
        with a.output.open('x') as f: json.dump(report,f,indent=2)
        print(json.dumps({'output':str(a.output),'errors':report['errors'],'pointers_stable':report['pointers_stable'],'matches_recorder':[p['matches_recorder'] for p in report['players']],'auxiliary_objects':report.get('manager',{}).get('auxiliary_objects',[])},indent=2))
    finally: k.CloseHandle(h)

if __name__=='__main__': main()



