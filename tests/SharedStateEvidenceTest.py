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

sections.pop('wind.preflight_shared')
sections['postload.failure_check']=struct.pack('<I',4)
sections['scalar_reset.diagnostic']=struct.pack('<12I',0,0x3F333333,0x3F800000,0x3F800000,0,1,1,0,0,0,0,0)
r=analyze(sections)
assert r['postload_failure_check']==4 and r['scalar_resets'][0]['saved']==0x3F333333
sections['scalar_reset.diagnostic']=b'bad'
try:
    analyze(sections)
    raise AssertionError('bad scalar reset size accepted')
except ValueError:
    pass
print('PASS: scalar reset evidence and bounds')

sections.pop('scalar_reset.diagnostic', None)
sections['wind.rng_boundary']=struct.pack('<9I',1,0,1,1,1,1,1,0,0)+struct.pack('<128I',*range(128))
r=analyze(sections)['effect_rng_boundary']
assert r['entry_match']==1 and r['after_match']==0
assert r['source']['cursor']==25 and r['after']['secondary_words']==list(range(122,128))
sections['wind.rng_boundary']=b'bad'
try:
    analyze(sections)
    raise AssertionError('bad effect RNG boundary size accepted')
except ValueError:
    pass
print('PASS: full shared RNG boundary decoding and malformed-size rejection')
