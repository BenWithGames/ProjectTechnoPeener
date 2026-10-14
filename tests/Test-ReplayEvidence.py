import sys,struct,tempfile
from pathlib import Path
sys.path.insert(0,'tools')
from analyze_replay_evidence import read,analyze
sections={'blend.expected':bytes(100),'blend.actual':bytes([1])*100}
for side in ['expected','actual']:
 prefix='native.'+side
 sections[prefix+'.writes']=struct.pack('<I',20)
 sections[prefix+'.size']=struct.pack('<Q',80)
 sections[prefix+'.lengths']=struct.pack('<'+'Q'*20,*([4]*20))
 sections[prefix+'.traces']=bytes(20*32)
 sections[prefix+'.payload']=bytes(80) if side=='expected' else bytes([1])*80
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'evidence.bin'
 data=struct.pack('<Q',0x3144564543364353)
 for k,v in sections.items():data+=struct.pack('<QQ',len(k),len(v))+k.encode()+v
 p.write_bytes(data)
 result=analyze(read(p))
 assert len(result['native_differences'])==20 and result['pairs']['blend.expected']['changed_bytes']==100
 assert result['pairs']['blend.expected']['omitted_offsets']==36
 p.write_bytes(data[:-1])
 try:read(p);raise AssertionError('truncation accepted')
 except ValueError:pass
print('PASS: all 20 native mismatches, simultaneous blend differences, explicit offset truncation, malformed rejection')

# Timer decoder preserves pointer bits, scalar words, validity and phase ordering.
sample=bytearray(776)
struct.pack_into('<II',sample,0,5,1)
struct.pack_into('<Q',sample,8,0x123456789abcdef0)
struct.pack_into('<III',sample,8+0x40,0x40400000,0x3f800000,2)
sections.update({'blend_timer.history':bytes(sample)+bytes(776*31),
                 'blend_timer.count':struct.pack('<I',1),
                 'blend_timer.dropped':struct.pack('<I',0),
                 'blend_timer.expected':bytes(sample)})
r=analyze(sections)['blend_timers']
assert len(r['history'])==1 and r['history'][0]['phase']==5
slot=r['history'][0]['slots'][0]
assert slot['valid'] and slot['cursor_bits']==0x40400000 and slot['state']==2
assert not r['history'][0]['slots'][1]['valid']
assert slot['pointer_words'][:2]==[0x9abcdef0,0x12345678]
sections['blend_timer.count']=struct.pack('<I',33)
try:analyze(sections);raise AssertionError('oversized timer count accepted')
except ValueError:pass
print('PASS: timer phases, validity, raw scalar/pointer bits and bounds')
