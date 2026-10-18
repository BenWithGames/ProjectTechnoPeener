"""Validate paired diagnostic recordings; never import or restore native pointers."""
import argparse, hashlib, json, struct, zipfile
from pathlib import Path
RECORD=struct.Struct('<QQIIII40s')
def sections(data):
    if len(data)>32*1024*1024 or data[:8]!=struct.pack('<Q',0x3144564543364353):raise ValueError('Invalid snapshot')
    out={};at=8
    while at<len(data):
        if at+16>len(data):raise ValueError('Truncated section')
        n,size=struct.unpack_from('<QQ',data,at);at+=16
        if n>128 or at+n+size>len(data):raise ValueError('Invalid section bounds')
        name=data[at:at+n].decode('ascii');at+=n
        if name in out:raise ValueError('Duplicate section')
        out[name]=data[at:at+size];at+=size
    return out

def key(r):return r[1],r[3],r[4],r[2]
def number(s,k):return int.from_bytes(s[k],'little')
def validate_snapshot(data,rows):
    s=sections(data);i=number(s,'record_index');r=rows[i]
    checks={'capture':number(s,'capture_valid')==1 and number(s,'capture_stage')==15 and number(s,'exception')==0,
        'frame':struct.unpack_from('<I',s['keys'])[0]==r[2], 'inputs':s['input']==r[6],
        'rng':struct.unpack_from('<I',s['measurements'])[0]==r[5]}
    measures=s['measurements'];assert len(measures)==7*480
    checks['save_preserved_state']=measures[472]==1 and measures[952]==1 and measures[:4]==measures[480:484] and measures[8:472]==measures[488:952]
    size=number(s,'native.saved.size');lengths=struct.unpack('<4096Q',s['native.saved.lengths']);total=0;writes=0
    for length in lengths:
        if not length:break
        total+=length;writes+=1
    checks['stream']=size>0 and size<=len(s['native.saved.payload']) and total==size
    return {'index':i,'key':list(key(r)),'checks':checks,'valid':all(checks.values()),'native_bytes':size,'writes':writes,'sha256':hashlib.sha256(data).hexdigest()}

def compare(a,b):
    ma={key(r):r for r in a};mb={key(r):r for r in b}
    if len(ma)!=len(a) or len(mb)!=len(b):raise ValueError('Duplicate identity/frame')
    common=sorted(ma.keys()&mb.keys());offsets=[]
    for delta in range(-6,7):
        n=both=p1=p2=changed_n=changed_equal=0
        for k in common:
            other=mb.get((*k[:3],k[3]+delta))
            if other is None:continue
            x,y=ma[k][6],other[6];n+=1;p1+=x[:8]==y[:8];p2+=x[8:16]==y[8:16];both+=x[:16]==y[:16]
            prev=ma.get((*k[:3],k[3]-1))
            if prev and prev[6][:16]!=x[:16]:changed_n+=1;changed_equal+=x[:16]==y[:16]
        offsets.append(dict(peer_frame_offset=delta,overlap=n,both_slots_equal=both,p1_equal=p1,p2_equal=p2,transition_samples=changed_n,transition_equal=changed_equal))
    words=[sum(ma[k][6][i*4:i*4+4]==mb[k][6][i*4:i*4+4] for k in common) for i in range(10)]
    swap=sum(ma[k][6][:8]==mb[k][6][8:16] and ma[k][6][8:16]==mb[k][6][:8] for k in common)
    return dict(overlap=len(common),word_equal=words,swapped_slots_equal=swap,rng_equal=sum(ma[k][5]==mb[k][5] for k in common),offsets=offsets),ma,mb

def main():
    parser=argparse.ArgumentParser();parser.add_argument('local');parser.add_argument('peer');parser.add_argument('output');args=parser.parse_args();out=Path(args.output);out.mkdir(parents=True,exist_ok=True)
    result={'schema':1,'purpose':'offline validation fixtures; not executable replays','metadata_override':{'characters':['Yoshimitsu','Siegfried'],'stage':'snow stage (user description)'},'sources':[], 'matches':[]}
    for p in [args.local,args.peer]:result['sources'].append({'path':p,'sha256':hashlib.sha256(Path(p).read_bytes()).hexdigest()})
    with zipfile.ZipFile(args.local) as za,zipfile.ZipFile(args.peer) as zb:
        for match in range(1,4):
            rows=[list(RECORD.iter_unpack(z.read(f'match-{match}/frames.bin'))) for z in [za,zb]]
            stats,ma,mb=compare(*rows);entry={'match':match,'alignment':stats,'candidates':[]};snapshots=[]
            for side,z in zip(['local','peer'],[za,zb]):
                ss=[]
                for n in range(8):ss.append(validate_snapshot(z.read(f'match-{match}/snapshot-{n}.bin'),rows[0 if side=='local' else 1]))
                snapshots.append(ss)
                for start in [0,4]:
                    window=ss[start:start+4];keys=[s['key'] for s in window]
                    consecutive=all(keys[i][:3]==keys[0][:3] and keys[i][3]==keys[0][3]+i for i in range(4))
                    bilateral=all(tuple(k) in ma and tuple(k) in mb for k in keys)
                    slots_equal=bilateral and all(ma[tuple(k)][6][:16]==mb[tuple(k)][6][:16] for k in keys)
                    rng_equal=bilateral and all(ma[tuple(k)][5]==mb[tuple(k)][5] for k in keys)
                    candidate={'side':side,'start_snapshot':start,'snapshots':window,'consecutive':consecutive,'peer_frame_coverage':bilateral,'bilateral_player_inputs_equal':slots_equal,'bilateral_rng_equal':rng_equal,'clean_local':consecutive and all(s['valid'] for s in window)}
                    entry['candidates'].append(candidate)
            # Select the best independent local-state window on each PC per match.
            entry['selected']=[]
            for side,z,r in zip(['local','peer'],[za,zb],rows):
                eligible=[c for c in entry['candidates'] if c['side']==side and c['clean_local']]
                if not eligible:continue
                chosen=max(eligible,key=lambda c:(c['bilateral_player_inputs_equal'],c['bilateral_rng_equal']))
                dest=out/f'match-{match}-{side}';dest.mkdir(exist_ok=True)
                for n in range(chosen['start_snapshot'],chosen['start_snapshot']+4):
                    data=z.read(f'match-{match}/snapshot-{n}.bin');(dest/f'snapshot-{n}.bin').write_bytes(data)
                indexes=[s['index'] for s in chosen['snapshots']]
                (dest/'frames.bin').write_bytes(b''.join(RECORD.pack(*r[i]) for i in indexes))
                (dest/'manifest.json').write_text(json.dumps(chosen,indent=2));entry['selected'].append({'side':side,'folder':str(dest),'first_frame':chosen['snapshots'][0]['key'][3],'inputs_equal':chosen['bilateral_player_inputs_equal'],'rng_equal':chosen['bilateral_rng_equal']})
            result['matches'].append(entry)
    (out/'analysis.json').write_text(json.dumps(result,indent=2))
    print(json.dumps([{'match':m['match'],'alignment':m['alignment'],'selected':m['selected']} for m in result['matches']],indent=2))
if __name__=='__main__':main()
