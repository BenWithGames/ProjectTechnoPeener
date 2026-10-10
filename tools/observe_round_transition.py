"""Read-only round-transition trace. Never injects, restores, or writes game memory."""
import argparse, ctypes as C, hashlib, json, struct, time
from ctypes import wintypes as W
from pathlib import Path
from observe_training import EXPECTED_SHA256


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--pid',type=int,required=True)
    ap.add_argument('--private-match-confirmed',action='store_true',required=True)
    ap.add_argument('--seconds',type=int,default=45)
    ap.add_argument('--output',type=Path,required=True)
    a=ap.parse_args()
    if not 1<=a.seconds<=120: ap.error('seconds must be1..120')
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
        # Raw candidates, not asserted round IDs. Addresses come from inspected
        # initialization/reset paths; collect evidence before assigning meaning.
        fields={'counter':0x470D0C4,'p1_timer':0x475A038,'p2_timer':0x47F1428,
                'raw_470d0c0':0x470D0C0,'raw_470d0d8':0x470D0D8,
                'raw_470f284':0x470F284,'raw_4844010':0x4844010}
        rows=[];unstable=0;start=time.perf_counter();deadline=start+a.seconds
        print('CAPTURE READY: finish one round now, then allow the next round to begin.',flush=True)
        while time.perf_counter()<deadline:
            before=int.from_bytes(read(base+fields['counter'],4),'little')
            row={name:int.from_bytes(read(base+rva,4),'little') for name,rva in fields.items()}
            after=int.from_bytes(read(base+fields['counter'],4),'little')
            if before==row['counter']==after:
                row['elapsed_ms']=round((time.perf_counter()-start)*1000,3);rows.append(row)
            else:unstable+=1
            time.sleep(.01)
        resets=[];changes=[]
        for previous,current in zip(rows,rows[1:]):
            if current['counter']<previous['counter']:
                resets.append({'before':previous,'after':current})
            changed={key:[previous[key],current[key]] for key in fields
                     if key.startswith('raw_') and previous[key]!=current[key]}
            if changed:changes.append({'elapsed_ms':current['elapsed_ms'],'counter':current['counter'],'changes':changed})
        result={'schema':1,'read_only':True,'pid':a.pid,'samples':len(rows),'unstable_samples':unstable,
                'observed_counter_resets':resets,'raw_field_changes':changes,'rows':rows,
                'limitations':'External polling can miss transitions; counter bracketing is not an atomic snapshot. Raw fields are not verified round identifiers.'}
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(f"Saved {len(rows)} samples, {len(resets)} observed counter resets and {len(changes)} candidate changes: {a.output}",flush=True)
    finally:
        k.CloseHandle(h)

if __name__=='__main__':main()
