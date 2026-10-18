import sys,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_replay_evidence import timer_lookup
b=bytearray(24+4096*1056);struct.pack_into('<4IQ',b,0,1,1,1,0,1)
struct.pack_into('<Q',b,24,0);struct.pack_into('<I',b,24+200,4);struct.pack_into('<I',b,24+216,32)
b[24+940]=23
r=timer_lookup(b);assert r['events'][0]['site_name']=='lookup_sample' and r['events'][0]['sample_bytes'].startswith('17') and r['events'][0]['weight'] is None
for bad in [b[:-1],bytearray(b)]:
 if len(bad)==len(b):struct.pack_into('<I',bad,24+200,99)
 try:timer_lookup(bad)
 except ValueError:pass
 else:raise AssertionError('accepted malformed trace')
print('PASS: timer trace decoder validates schema, bounds, sequence and optional output')

# Wrap sequence validation is independent of physical ring order.
w=bytearray(24+4096*1056);struct.pack_into('<4IQ',w,0,1,1,1,3,4097)
struct.pack_into('<Q',w,24,4096)
r=timer_lookup(w);assert r['overwritten']==4096 and r['dropped']==3 and r['events'][0]['sequence']==4096
struct.pack_into('<Q',w,24,0)
try:timer_lookup(w)
except ValueError:pass
else:raise AssertionError('accepted stale wrapped event')
