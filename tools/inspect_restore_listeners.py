"""Read-only restore callback and component inventory; never invokes game methods."""
import argparse, ctypes as C, hashlib, json, struct
from ctypes import wintypes as W
from pathlib import Path
from observe_training import EXPECTED_SHA256


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid',type=int,required=True)
    ap.add_argument('--training-confirmed',action='store_true',required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--components',action='store_true',help='Resolve weak targets and inspect component dispatches without invoking them')
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
        def weak_target(addr):
            index,serial=struct.unpack('<ii',read(addr,8))
            count=struct.unpack('<i',read(base+0x42A115C,4))[0]
            if serial==0 or index<0 or index>=count:return None
            storage=u64(base+0x42A1150)
            item=storage+index*24
            raw=read(item,24)
            pointer,flags,_,actual_serial=struct.unpack_from('<QIIi',raw)
            if serial!=actual_serial or flags&0x30000000:return None
            # A read-only inventory is non-atomic; reject a changed weak reference.
            if read(addr,8)!=struct.pack('<ii',index,serial):raise RuntimeError('Weak reference changed during inspection')
            return pointer or None
        def components(target):
            result=describe(target,(0x5F8,0x600,0x680,0x698))
            result['side_index']=struct.unpack('<i',read(target+0x3A0,4))[0]
            storage=u64(target+0x3B0)
            count=struct.unpack('<i',read(target+0x3B8,4))[0]
            if not 0<=count<=256:raise RuntimeError('Unexpected component count')
            result['component_count']=count
            result['components']=[]
            for j in range(count):
                component=u64(storage+j*8)
                result['components'].append({'index':j,**describe(component,(0x308,0x3A8))})
            result['attachment_count']=struct.unpack('<i',read(target+0x528,4))[0]
            if storage!=u64(target+0x3B0) or count!=struct.unpack('<i',read(target+0x3B8,4))[0]:
                raise RuntimeError('Component array changed during inspection')
            return result
        interface=u64(base+0x470D188)
        delegate=interface+0xA18
        count=struct.unpack('<i',read(delegate+0x50,4))[0]
        if not 0<=count<=64: raise RuntimeError('Unexpected listener count')
        storage=u64(delegate+0x40) or delegate
        listeners=[]
        for i in range(count):
            entry=storage+i*0x40
            active=struct.unpack('<i',read(entry+0x30,4))[0]
            obj=u64(entry+0x20) or entry
            method=u64(obj+0x10)-base
            item={'index':i,'active':active,'bound_method_rva':hex(method),**describe(obj,(0x68,))}
            if a.components:
                target=weak_target(obj+8)
                item['weak_target']=hex(target) if target else None
                if target and method==0x3C6050:item['target_details']=components(target)
                elif target:item['target_vtable_rva']=hex(u64(target)-base)
            listeners.append(item)
        report={'schema':2,'pid':a.pid,'module_base':hex(base),'read_only':True,'atomic':False,'listener_count':count,'listeners':listeners}
        with a.output.open('x') as f:json.dump(report,f,indent=2)
        print(json.dumps(report,indent=2))
    finally: k.CloseHandle(h)

if __name__=='__main__': main()




