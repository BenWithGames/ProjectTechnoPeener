import sys,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_replay_evidence import selector_snapshot
stride=262816
b=bytearray(16+2*stride)
assert not selector_snapshot(b)['valid']
struct.pack_into('<IIQ',b,0,1,1,12)
for side in range(2):
    at=16+side*stride
    struct.pack_into('<6Q6I',b,at,0x10000,0xa5788,0x20000,0x30000,0x30000,0x30008,1,2,1,3,8,404)
    for i in range(24):struct.pack_into('<h',b,at+96+i*24,-1)
    struct.pack_into('<h',b,at+672+2,2)
struct.pack_into('<3h',b,16+96,3,4,5)
struct.pack_into('<QI',b,16+96+8,0x10000,7)
r=selector_snapshot(b)
assert r['entries'][0]['previous']==2 and r['entries'][0]['counters']==[2]
assert r['entries'][0]['pending'][0]['flags']==7
for bad in (b[:-1],b+b'\0'):
    try:selector_snapshot(bad);raise AssertionError('accepted invalid size')
    except ValueError:pass
struct.pack_into('<I',b,16+68,262145)
try:selector_snapshot(b);raise AssertionError('accepted invalid table')
except ValueError:pass
print('PASS: selector raw evidence layout, pending queue, table counters and bounds')
