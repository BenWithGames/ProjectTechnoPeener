"""Reconstruct typed ring dependencies into an offline arena, never game memory.

Schema is the pinned Windows x64 RingSnapshot layout. Unknown references remain
unresolved; serializer chunks are not assumed to be whole allocated objects.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from select_match_windows import sections, number, RECORD, key

RING_SIZE = 307496

class DependencyImage:
    def __init__(self, snapshot):
        self.sections = sections(snapshot)
        s = self.sections
        if number(s, 'capture_valid') != 1 or number(s, 'capture_stage') != 15 or number(s, 'exception'):
            raise ValueError('Invalid capture')
        if len(s['rings']) != 4 * RING_SIZE or s['rings'][307488] != 1:
            raise ValueError('Unsupported or invalid ring layout')
        if len(s['measurements']) != 3360 or s['measurements'][472] != 1:
            raise ValueError('Invalid measurement')
        self.module = number(s, 'module')
        self.players = struct.unpack_from('<2Q', s['measurements'], 8)
        self.nodes = []
        self.edges = []
        self.buffers = {}
        self.ranges = []
        for side, player in enumerate(self.players):
            self.nodes.append(dict(id=f'p{side+1}', kind='player', original_address=hex(player),
                                   complete=False, blocker='Full player allocation and external dependencies not archived'))
        cursor = 288
        for group in range(4):
            values = struct.unpack_from('<8QI', s['rings'], group * 72)
            player, owner, vtable, a, b, c, current, previous, index = values
            slots = (a, b, c); side, kind = divmod(group, 2)
            stride = 0x800 if kind else 0xC000
            first = (owner + 0xBF) & ~0x7F
            if (player != self.players[side] or owner != player + (0x27760 if kind else 0x35A0)
                or vtable != self.module + (0x3E87700 if kind else 0x3E87638)
                or index >= 3 or current != slots[index] or previous not in slots
                or slots != tuple(first + i * stride for i in range(3))):
                raise ValueError('Ring identity/layout mismatch')
            node = f'p{side+1}.ring{kind}'
            self.nodes.append(dict(id=node, kind='ring_header', original_address=hex(owner),
                                   complete=False, index=index, blocker='Only typed header fields captured'))
            self.edges.append(dict(source=f'p{side+1}', field=f'ring{kind}', target=node))
            self.edges.append(dict(source=node, field='vtable', target=f'module+{vtable-self.module:x}',
                                   resolution='requires pinned executable, not an arena allocation'))
            for slot, address in enumerate(slots):
                target = f'{node}.slot{slot}'
                self.buffers[target] = bytes(s['rings'][cursor:cursor+stride]); cursor += stride
                self.ranges.append((address, address+stride, target))
                self.nodes.append(dict(id=target, kind='ring_payload', original_address=hex(address),
                                       byte_count=stride, complete=True,
                                       reference_semantics='Opaque bytes; embedded references are not relocated'))
                self.edges.append(dict(source=node, field=f'slot{slot}', target=target))
            for field, address in [('current', current), ('previous', previous)]:
                self.edges.append(dict(source=node, field=field, target=f'{node}.slot{slots.index(address)}'))
        if cursor != 307488:
            raise ValueError('Ring payload size mismatch')
        ordered = sorted(self.ranges)
        if any(a[1] > b[0] for a, b in zip(ordered, ordered[1:])):
            raise ValueError('Aliased ring allocations')

    def instantiate(self):
        # An independent Python-owned arena; original addresses are identifiers.
        return {name: bytearray(data) for name, data in self.buffers.items()}

    def restore(self, arena):
        # Transactional validation before any mutation. Never resize or add nodes.
        if arena.keys() != self.buffers.keys() or any(type(arena[n]) is not bytearray or
            len(arena[n]) != len(data) for n, data in self.buffers.items()):
            raise ValueError('Arena topology mismatch')
        if len({id(v) for v in arena.values()}) != len(arena):
            raise ValueError('Aliased destination buffers')
        for name, data in self.buffers.items():
            arena[name][:] = data

    def report(self):
        return dict(nodes=self.nodes, edges=self.edges, reconstructed_payload_bytes=sum(map(len,self.buffers.values())),
                    native_restore_authorized=False, replay_verified=False,
                    blockers=['Player allocation and resource payload closure absent',
                              'No recorded ownership control blocks or allocation generations',
                              'Opaque payload references require typed relocation, not pointer scanning'])

def main():
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);args=p.parse_args()
    output={'schema':1,'scope':'offline_typed_ring_dependencies','native_restore':False,'replay_verified':False,'fixtures':[]}
    for side in ('local','peer'):
        folder=args.root/f'match-2-{side}'
        rows=list(RECORD.iter_unpack((folder/'frames.bin').read_bytes()))
        if len(rows)!=4 or any(key(r)[:3]!=key(rows[0])[:3] or r[2]!=rows[0][2]+i for i,r in enumerate(rows)):
            raise ValueError('Round/frame discontinuity')
        for i,row in enumerate(rows):
            raw=(folder/f'snapshot-{i}.bin').read_bytes();image=DependencyImage(raw)
            if image.sections['input']!=row[6] or struct.unpack_from('<I',image.sections['keys'])[0]!=row[2]:
                raise ValueError('Frame/input mismatch')
            arena=image.instantiate()
            for data in arena.values(): data[0]^=0xFF
            image.restore(arena)
            if any(bytes(arena[n])!=data for n,data in image.buffers.items()): raise ValueError('Restore mismatch')
            report=image.report();report.update(side=side,frame=row[2],archive_sha256=hashlib.sha256(raw).hexdigest(),
                                               offline_arena_restore_verified=True)
            output['fixtures'].append(report)
    (args.root/'match-2-dependency-reconstruction.json').write_text(json.dumps(output,indent=2))
    print('PASS: 8 typed dependency images; 12 independent buffers and 307200 bytes restored per image. Native replay remains blocked.')
if __name__=='__main__':main()
