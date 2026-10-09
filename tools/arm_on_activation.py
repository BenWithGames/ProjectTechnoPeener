"""Observe activation state through read-only process access, then arm one offline save via its control file."""
import argparse, ctypes as C, hashlib, json, struct, time
from ctypes import wintypes as W
from pathlib import Path
from observe_training import EXPECTED_SHA256


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid',type=int,required=True)
    ap.add_argument('--condition',choices=['activation','slowdown'],default='activation')
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
        project=Path(__file__).resolve().parents[1]
        mod=exe.parent/'Mods'/'SC6Rollback'
        staged=project/'package'/'Mods'/'SC6Rollback'/'dlls'/'main.dll'
        if hashlib.sha256(staged.read_bytes()).digest()!=hashlib.sha256((mod/'dlls'/'main.dll').read_bytes()).digest():
            raise RuntimeError('Installed DLL differs from staged build')
        captures=mod/'captures'
        if not (captures/'status.txt').is_file(): raise RuntimeError('Recorder status missing')
        if list(captures.glob('*.arm')): raise RuntimeError('An arm request is already pending')
        print('Watcher ready',flush=True)
        deadline=time.monotonic()+30
        observed=set()
        while time.monotonic()<deadline:
            selector=struct.unpack('<i',read(base+0x4846458,4))[0]
            scale=struct.unpack('<f',read(base+0x48462F4,4))[0]
            observed.add((selector,scale))
            matched=(selector==0 and scale==0) if a.condition=='activation' else (selector==-1 and 0<scale<0.99)
            if matched:
                with (captures/'offline-save.arm').open('x',encoding='ascii') as trigger:
                    trigger.write(f'OFFLINE_SAVE {a.pid}\n')
                a.output.write_text(json.dumps({'armed':True,'condition':a.condition,'observed':sorted(observed)},indent=2))
                print('Activation detected; one-shot save armed',flush=True)
                return
            time.sleep(.01)
        a.output.write_text(json.dumps({'armed':False,'observed':sorted(observed)},indent=2))
        raise RuntimeError('Activation condition not observed within 30 seconds; not armed')
    finally: k.CloseHandle(h)

if __name__=='__main__': main()




