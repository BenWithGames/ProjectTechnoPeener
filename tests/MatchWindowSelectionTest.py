import importlib.util,struct,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('selector',Path(__file__).parents[1]/'tools/select_match_windows.py');s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
class Tests(unittest.TestCase):
 def row(self,n,frame=None):return (n*10,1,n if frame is None else frame,1,50,123,struct.pack('<10I',n,n*3,n*7,n*11,*([0]*6)))
 def test_zero_offset_and_auxiliary(self):
  a=[self.row(i) for i in range(1,30)];b=[(*r[:6],r[6][:16]+bytes([1])*24) for r in a]
  report,_,_=s.compare(a,b);best=max(report['offsets'],key=lambda x:x['both_slots_equal']);self.assertEqual(best['peer_frame_offset'],0);self.assertEqual(best['both_slots_equal'],29);self.assertEqual(report['word_equal'][4],0)
 def test_known_offset(self):
  a=[self.row(i) for i in range(1,30)];b=[self.row(i,i+2) for i in range(1,30)]
  report,_,_=s.compare(a,b);self.assertEqual(max(report['offsets'],key=lambda x:x['both_slots_equal'])['peer_frame_offset'],2)
 def test_duplicates_rejected(self):
  r=self.row(1)
  with self.assertRaises(ValueError):s.compare([r,r],[r])
 def test_truncated_snapshot(self):
  with self.assertRaises(ValueError):s.sections(struct.pack('<Q',0x3144564543364353)+b'bad')
 def test_snapshot_fields_must_match_record(self):
  r=self.row(9);measurement=bytearray(7*480)
  for at in [0,480]:struct.pack_into('<I',measurement,at,123);measurement[at+472]=1
  fields={'record_index':struct.pack('<I',0),'capture_valid':b'\x01','capture_stage':struct.pack('<I',15),'exception':bytes(4),'keys':struct.pack('<I',9),'input':r[6],'measurements':measurement,'native.saved.size':struct.pack('<Q',4),'native.saved.lengths':struct.pack('<4096Q',4,*([0]*4095)),'native.saved.payload':b'abcd'}
  def encode():return struct.pack('<Q',0x3144564543364353)+b''.join(struct.pack('<QQ',len(k),len(v))+k.encode()+v for k,v in fields.items())
  self.assertTrue(s.validate_snapshot(encode(),[r])['valid']);fields['input']=bytes(40);self.assertFalse(s.validate_snapshot(encode(),[r])['valid'])
if __name__=='__main__':unittest.main()
