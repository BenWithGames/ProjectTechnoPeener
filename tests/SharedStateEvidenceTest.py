"""Verify exact shared-state evidence decoding without game access."""
import sys
import struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from analyze_replay_evidence import analyze
sections={}
for side in ('expected','actual'):
    for name,data in (('writes',struct.pack('<I',0)),('size',struct.pack('<Q',0)),('lengths',b''),('payload',b''),('traces',b'')):
        sections['native.'+side+'.'+name]=data
sections['motion.signed_zero']=struct.pack('<5Q',1,356,0x1CC,0x80000000,0)
assert analyze(sections)['motion_signed_zero_exception']['expected_bits']==0x80000000
payload=b''.join(struct.pack('<I',v)+bytes(508) for v in (0,456,456,789))
sections['wind.preflight_shared']=struct.pack('<6IQ',3,22,11,24,0,1,0x12345678)+payload
sections['wind.preflight_line']=struct.pack('<I',432)
r=analyze(sections)['shared_state_preflight']
assert r['check']=='current snapshot/live memory mismatch' and r['offset']==0
assert r['address']=='0x12345678' and r['current'].startswith('c8010000') and r['live'].startswith('15030000')
sections['wind.preflight_shared']=struct.pack('<6IQ',2,22,11,24,0,0,0x12345678)+payload
assert analyze(sections)['shared_state_preflight']['live'] is None
sections['wind.preflight_shared']=struct.pack('<6IQ',3,22,11,513,0,1,0)+payload
try:
    analyze(sections)
    raise AssertionError('oversized record accepted')
except ValueError:
    pass
print('PASS: shared-state evidence exact values, unread-live distinction and bounds')
