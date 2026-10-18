"""Inventory archived serializer ranges without dereferencing recorded pointers."""
import argparse
import json
import struct
from pathlib import Path
from select_match_windows import sections, number

def inventory(path):
    s=sections(path.read_bytes())
    lengths=struct.unpack('<4096Q',s['native.saved.lengths'])
    traces=list(struct.iter_unpack('<4Q',s['native.saved.traces']))
    size=number(s,'native.saved.size'); offset=0; records=[]; ended=False
    for i,length in enumerate(lengths):
        if not length:
            ended=True
            continue
        if ended or length>size-offset: raise ValueError('Invalid stream shape')
        source,destination,writer,reader=traces[i]
        if destination or reader: raise ValueError('Archive already contains reader state')
        records.append(dict(record=i,source=hex(source),length=length,payload_offset=offset,
                            writer_rva=hex(writer-number(s,'module')),
                            live_binding='unresolved'))
        offset+=length
    if offset!=size: raise ValueError('Incomplete payload')
    return records

def main():
    p=argparse.ArgumentParser();p.add_argument('fixture_root',type=Path);args=p.parse_args()
    result={'native_restore_authorized':False,'replay_verified':False,'sides':{},
            'blockers':['No archived ownership bindings or portable object identity map',
                        'Serializer source ranges are not complete object allocations',
                        'Embedded references need typed relocation and dependency reconstruction'],
            'note':'Equal recorded addresses across snapshots do not prove current object lifetime. Writer RVAs and source addresses are evidence only.'}
    for side in ('local','peer'):
        snapshots=[inventory(args.fixture_root/f'match-2-{side}'/f'snapshot-{i}.bin') for i in range(4)]
        first=snapshots[0]
        stable=sum(all(len(other)>i and (other[i]['source'],other[i]['length'],other[i]['writer_rva'])==
                       (r['source'],r['length'],r['writer_rva']) for other in snapshots[1:]) for i,r in enumerate(first))
        result['sides'][side]={'record_counts':[len(s) for s in snapshots],
                               'stable_recorded_source_ranges':stable,
                               'unresolved_live_bindings':len(first),'snapshots':snapshots}
    output=args.fixture_root/'match-2-object-audit.json'
    output.write_text(json.dumps(result,indent=2))
    print(json.dumps({side:{k:v for k,v in data.items() if k!='snapshots'} for side,data in result['sides'].items()},indent=2))
if __name__=='__main__':main()
