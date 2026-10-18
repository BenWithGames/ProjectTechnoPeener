import sys,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_replay_evidence import animation_copy
raw=bytearray(24+4096*88)
struct.pack_into('<4IQ',raw,0,1,1,1,2,1)
struct.pack_into('<4Q14I',raw,24,0,100,200,0,5,2,755,1,1,0,1,2,3,4,5,6,7,8)
r=animation_copy(raw)
assert r['events'][0]['direction']=='ring_to_cache' and r['events'][0]['cache_bits']==[1,2,3,4] and r['events'][0]['ring_bits']==[5,6,7,8]
for invalid in (raw[:-1],struct.pack('<I',3)+raw[4:]):
 try:
  animation_copy(invalid)
  raise AssertionError('malformed trace accepted')
 except ValueError: pass
print('PASS: animation copy exact words, phase, direction and malformed trace rejection')

new=bytearray(24+4096*1048)
struct.pack_into("<4IQ",new,0,2,1,1,0,1)
struct.pack_into("<4Q14I",new,24,0,100,200,0,5,2,755,4,1,1,1,2,3,4,5,6,7,8)
new[24+88]=77
r=animation_copy(new)
assert r["events"][0]["direction"]=="cache_to_ring" and r["events"][0]["layers_hex"].startswith("4d")
