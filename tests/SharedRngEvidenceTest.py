import sys,struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_replay_evidence import shared_rng

def bundle(events):
 b=bytearray(24+8192*168);struct.pack_into('<4IQ',b,0,1,1,len(events),0,len(events))
 for seq,(caller,result,phase,frame,stream,state) in enumerate(events):struct.pack_into('<3Q36I',b,24+seq*168,seq,caller,result,7,phase,frame,stream,*([state]*32))
 return b
x=[(100,11,0,40,1,1),(200,22,0,40,2,2),(0,39,2,39,0,0),(100,11,2,40,1,1),(201,23,2,40,2,3),(0,39,2,40,3,3)]
a=shared_rng(bundle(x));d=a['comparisons'][0]['first_difference'];assert d['index']==1 and d['natural']['caller']==200 and d['replay']['caller']==201
x[4]=(200,22,2,40,2,2);assert shared_rng(bundle(x))['comparisons'][0]['first_difference'] is None
x.insert(5,(200,22,2,40,2,2));assert shared_rng(bundle(x))['comparisons'][0]['first_difference']['index']==2
b=bundle(x);struct.pack_into('<I',b,12,1);assert shared_rng(b)['comparisons'][0]['coverage']=='candidate_only'
for bad in (b[:-1],bytes(24)):
 try:shared_rng(bad);raise AssertionError('accepted malformed trace')
 except ValueError:pass
print('PASS: shared RNG caller/order/state differences, extra calls, incomplete coverage and malformed evidence')
