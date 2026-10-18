import sys,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_replay_evidence import inline_rng
b=bytearray(24+8192*168);struct.pack_into('<4IQ',b,0,1,1,2,0,2)
struct.pack_into('<17Q8I',b,24,0,*range(16),8,1,2,77,123,123,123,1)
struct.pack_into('<17Q8I',b,192,1,*range(16),9,1,2,78,124,124,123,1)
a=inline_rng(b);assert a['events'][0]['seed_matches'] and not a['events'][1]['seed_matches']
try: inline_rng(b[:-1]);raise AssertionError('accepted truncation')
except ValueError:pass
print('PASS: inline RNG evidence boundaries and malformed data')
