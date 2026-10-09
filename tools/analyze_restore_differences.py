"""Map SC6 full-probe differences in the statically audited player prefix.
Uses captured player self-pointers plus recorded fields to validate prefix starts.
Does not assume sizes of the variable linked-list sections or classify padding.
"""
import argparse,csv,hashlib,json,struct
from pathlib import Path

# Native writer RVA 30A980, in transfer order. None denotes an indirect buffer.
BLOCKS=[(0x10,0x80),(0x90,0x3510),(None,0x1840),(None,0x800),
        (0x2B3E0,0x50),(0x43D80,0x50),(0x43DF0,0x30),
        (0x444F0,0xD38),(0x45230,0x370),(0x95FA0,0x11F0),
        (0x971A8,0x14),(0x971E8,0x60)]

def locate_offset(player_offset):
    cursor=0
    for address,length in BLOCKS:
        if address is not None and address<=player_offset<address+length:
            return cursor+player_offset-address
        cursor+=length
    raise ValueError('Offset outside audited direct blocks')

def starts(before,after,row):
    result=[]
    for side in (1,2):
        pointer=int(row[f'p{side}_pointer']);needle=struct.pack('<Q',pointer)
        candidates=[];offset=0
        while True:
            found=after.find(needle,offset)
            if found<0:break
            offset=found+1;start=found-locate_offset(0x43DF0)
            if start<0 or start+0x79AC>len(before):continue
            checks=[(0x23C,'B',int(row[f'p{side}_index'])),
                    (0x19EC,'H',int(row[f'p{side}_state'])),
                    (0x44948,'I',int(row[f'p{side}_timer'])),
                    (0x4495A,'H',int(row[f'p{side}_move'])),
                    (0x444F8,'f',float(row[f'p{side}_progress']))]
            if all(struct.unpack_from('<'+fmt,before,start+locate_offset(addr))[0]==value for addr,fmt,value in checks):
                candidates.append(start)
        if len(candidates)!=1:raise ValueError(f'P{side} prefix validation ambiguous: {candidates}')
        result.append(candidates[0])
    if result[0]!=0 or result[1]<0x79AC:raise ValueError('Unexpected player order')
    return result

def category(offset):
    if 0x43DF0<=offset<0x43DF8:return 0x43DF0,8,'rebuilt_self_pointer','30B12A stores player pointer'
    if 0x97100<=offset<0x97178:return offset&~3,4,'animation_rebuild_float','349290 -> 3493F0 writes 30 float values of 1.0 across both groups'
    if 0x97208<=offset<0x9720C or 0x97218<=offset<0x9721C:
        return offset&~3,4,'rng_derived_float','30B0A0 -> 2F8FA0 -> 2FA080(mode 1) -> 34F550 advances RNG at RVA 485EB28'
    known={0x9720C:('mode_parameter','2FA080 copies a mode-dependent table value at +24'),
           0x9721C:('reset_integer','2F909F stores -1 at subobject +34'),
           0x97220:('rebuilt_flag','30B09D stores nullness of subobject pointer at +38'),
           0x97228:('reset_integer','2F908C zeroes subobject +40'),
           0x97238:('reset_float','2F90A6 stores float 1.0 at subobject +50'),
           0x9723C:('reset_float','2F90AD stores float 1.0 at subobject +54')}
    aligned=offset&~3
    if aligned in known:return aligned,4,*known[aligned]
    return aligned,4,'unclassified_player_field','No cause assigned'

def analyze(stem):
    before=Path(str(stem)+'-before.bin').read_bytes();after=Path(str(stem)+'-after.bin').read_bytes()
    if len(before)!=len(after):raise ValueError('Different stream lengths; fixed-offset comparison is invalid')
    with Path(str(stem)+'.csv').open() as f:row=next(csv.DictReader(f))
    origins=starts(before,after,row);groups={};unmapped=[]
    for index,(a,b) in enumerate(zip(before,after)):
        if a==b:continue
        mapped=False
        for side,start in enumerate(origins,1):
            cursor=start
            for address,length in BLOCKS:
                if cursor<=index<cursor+length and address is not None:
                    player_offset=address+index-cursor
                    field,width,label,evidence=category(player_offset)
                    key=(side,field)
                    if key not in groups:
                        pos=start+locate_offset(field);x=before[pos:pos+width];y=after[pos:pos+width]
                        item={'player':side,'player_offset':hex(field),'file_offset':pos,'width':width,'classification':label,
                              'evidence':evidence,'before_hex':x.hex(),'after_hex':y.hex(),'different_bytes':0}
                        if width==4:
                            item.update(before_float=struct.unpack('<f',x)[0],after_float=struct.unpack('<f',y)[0],
                                        before_u32=int.from_bytes(x,'little'),after_u32=int.from_bytes(y,'little'))
                        groups[key]=item
                    groups[key]['different_bytes']+=1;mapped=True;break
                cursor+=length
            if mapped:break
        if not mapped:unmapped.append(index)
    totals={}
    for g in groups.values():totals[g['classification']]=totals.get(g['classification'],0)+g['different_bytes']
    result={'capture':stem.name,'before_sha256':hashlib.sha256(before).hexdigest(),'after_sha256':hashlib.sha256(after).hexdigest(),
            'player_starts':origins,'different_bytes':sum(totals.values())+len(unmapped),'category_bytes':totals,
            'fields':list(groups.values()),'unmapped_offsets':unmapped,
            'limitation':'Mapping explains observed writes, not gameplay harmlessness, complete state coverage, or rewind correctness.'}
    output=Path(str(stem)+'-field-differences.json');output.write_text(json.dumps(result,indent=2,allow_nan=True))
    print(json.dumps({k:v for k,v in result.items() if k!='fields'},indent=2))
    return result

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('capture_stems',type=Path,nargs='+')
    for stem in ap.parse_args().capture_stems:analyze(stem)
