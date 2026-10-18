import sys,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_replay_evidence import camera_calculations
b=bytearray(24+9568*512);struct.pack_into('<4IQ',b,0,1,1,2,0,2)
for seq,phase in [(0,0),(1,2)]:struct.pack_into('<2Q6I',b,24+9568*seq,seq,1,5,phase,650,5,63,65535)
b[24+9568+680+544]=1
r=camera_calculations(b);assert r['comparisons'][0]['differences']['controller']['changed_bytes']==1
struct.pack_into('<I',b,24+9568+24,651)
assert not camera_calculations(b)['comparisons'][0]['original_available']
try:camera_calculations(b[:-1]);raise AssertionError('accepted truncated trace')
except ValueError:pass
print('PASS: camera stage pairing, mismatch and missing evidence handling')
