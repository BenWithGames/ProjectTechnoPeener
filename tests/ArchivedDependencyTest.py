import sys
import struct
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from reconstruct_match_dependencies import DependencyImage,RING_SIZE

def fixture(mutate=None):
    module=0x140000000;players=(0x200000000,0x300000000)
    rings=bytearray(4*RING_SIZE);cursor=288
    for group in range(4):
        side,kind=divmod(group,2);player=players[side]
        owner=player+(0x27760 if kind else 0x35A0);stride=0x800 if kind else 0xC000
        first=(owner+0xBF)&~0x7F;slots=tuple(first+i*stride for i in range(3))
        struct.pack_into('<8QI',rings,group*72,player,owner,module+(0x3E87700 if kind else 0x3E87638),*slots,slots[1],slots[0],1)
        for slot in range(3):rings[cursor:cursor+stride]=bytes([group*3+slot])*stride;cursor+=stride
    rings[307488]=1
    measures=bytearray(3360);struct.pack_into('<2Q',measures,8,*players);measures[472]=1
    s={'capture_valid':b'\1','capture_stage':struct.pack('<I',15),'exception':bytes(4),
       'module':struct.pack('<Q',module),'rings':rings,'measurements':measures}
    if mutate:mutate(s)
    out=bytearray(struct.pack('<Q',0x3144564543364353))
    for name,data in s.items():out+=struct.pack('<QQ',len(name),len(data))+name.encode()+data
    return bytes(out)

class Tests(unittest.TestCase):
    def test_restore_is_independent_and_repeatable(self):
        image=DependencyImage(fixture());a=image.instantiate();b=image.instantiate()
        self.assertEqual(len(a),12)
        for _ in range(3):
            for data in a.values():data[-1]^=255
            image.restore(a)
            self.assertEqual(a,b)
        self.assertFalse(image.report()['native_restore_authorized'])
        self.assertFalse(image.report()['replay_verified'])
    def test_reject_before_mutation(self):
        image=DependencyImage(fixture());arena=image.instantiate()
        for data in arena.values():data[0]^=255
        arena[next(iter(arena))].pop();before={n:bytes(v) for n,v in arena.items()}
        with self.assertRaises(ValueError):image.restore(arena)
        self.assertEqual(before,{n:bytes(v) for n,v in arena.items()})
    def test_aliased_destinations_rejected(self):
        image=DependencyImage(fixture());arena=image.instantiate();names=list(arena)
        arena[names[1]]=arena[names[0]]
        with self.assertRaises(ValueError):image.restore(arena)
    def test_bad_index_rejected(self):
        with self.assertRaises(ValueError):DependencyImage(fixture(lambda s:struct.pack_into('<I',s['rings'],64,3)))
    def test_foreign_player_rejected(self):
        with self.assertRaises(ValueError):DependencyImage(fixture(lambda s:struct.pack_into('<Q',s['rings'],0,123)))
    def test_wrong_vtable_rejected(self):
        with self.assertRaises(ValueError):DependencyImage(fixture(lambda s:struct.pack_into('<Q',s['rings'],16,123)))
    def test_uncaptured_layout_rejected(self):
        with self.assertRaises(ValueError):DependencyImage(fixture(lambda s:s['rings'].__setitem__(307488,0)))
if __name__=='__main__':unittest.main()
