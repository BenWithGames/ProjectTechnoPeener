"""Raw captured byte comparison; accepted padding differences are not failures."""
import argparse
import json
import struct
from pathlib import Path

def read(path):
    data = Path(path).read_bytes()
    if len(data) > 32*1024*1024 or data[:8] != struct.pack('<Q', 0x3144564543364353):
        raise ValueError('Invalid evidence header/size')
    sections, at = {}, 8
    while at < len(data):
        if at+16 > len(data):
            raise ValueError('Truncated section header')
        n, size = struct.unpack_from('<QQ', data, at)
        at += 16
        if n > 128 or n+size > len(data)-at:
            raise ValueError('Invalid section bounds')
        name = data[at:at+n].decode('ascii')
        at += n
        if name in sections:
            raise ValueError('Duplicate section')
        sections[name] = data[at:at+size]
        at += size
    return sections

def animation_copy(data):
    schema = struct.unpack_from('<I',data)[0] if len(data)>=4 else 0
    size = {1:88,2:1048}.get(schema,0)
    if not size or len(data) != 24 + 4096 * size:
        raise ValueError('Invalid animation-copy trace size')
    schema, installed, count, dropped, total = struct.unpack_from('<4IQ', data)
    if schema not in (1,2) or installed > 1 or count > 4096 or count > total:
        raise ValueError('Invalid animation-copy trace header')
    events = []
    for seq in range(total-count, total):
        values = struct.unpack_from('<4Q14I', data, 24+(seq % 4096)*size)
        event = dict(zip(('sequence','cache','ring','entry_mask','thread','phase','frame','site','valid','condition'), values[:10]))
        if event['sequence'] != seq or event['site'] > (5 if schema==2 else 2) or event['valid'] > 1:
            raise ValueError('Invalid animation-copy event')
        event['cache_bits'] = list(values[10:14])
        event['ring_bits'] = list(values[14:18])
        if schema==2 and event['site']>=3:
            event['layers_hex']=data[24+(seq%4096)*size+88:24+(seq%4096+1)*size].hex()
        event['direction'] = ('entry' if event['site'] in (0,3) else 'cache_to_ring' if event['condition'] else 'ring_to_cache') if event['valid'] else 'unreadable'
        events.append(event)
    return dict(schema=schema, installed=installed, dropped=dropped, total=total,
                overwritten=total-count, sites={0:'entry_gate',1:'before_copy',2:'after_copy',3:'matrix_update_entry',4:'matrix_before_copy',5:'matrix_after_copy'}, events=events)

def timer_lookup(data):
    size, capacity = 1056, 4096
    if len(data) != 24+size*capacity:
        raise ValueError('Invalid timer lookup trace size')
    schema, installed, count, dropped, total = struct.unpack_from('<4IQ', data)
    if schema != 1 or installed > 1 or count > capacity or count > total:
        raise ValueError('Invalid timer lookup header')
    names = ['activation_entry','activation_accepted','lookup_entry','lookup_selected','lookup_sample','lookup_exit','weight_input','controller_entry','before_replay','after_replay']
    events = []
    for seq in range(total-count,total):
        raw = data[24+(seq%capacity)*size:24+(seq%capacity+1)*size]
        values = struct.unpack_from('<25Q12I', raw)
        if values[0] != seq or values[25] >= len(names):
            raise ValueError('Invalid timer lookup event')
        e = dict(zip(('sequence','qpc','player','caller','slot','descriptor','sample','category_output','weight_output'),values[:9]))
        e['registers'] = list(values[9:25])
        e.update(zip(('site','thread','phase','frame','valid_mask','side','rng','character','category','weight_bits','mxcsr','reserved'),values[25:]))
        e['site_name'] = names[e['site']]
        e['weight'] = struct.unpack('<f',struct.pack('<I',e['weight_bits']))[0] if e['valid_mask'] & 64 else None
        e['timers'] = []
        for i in range(3):
            t = raw[248+i*128:248+(i+1)*128]
            e['timers'].append(dict(slot=i, header=struct.unpack_from('<Q',t)[0], descriptors=struct.unpack_from('<Q',t,8)[0], samples=struct.unpack_from('<Q',t,16)[0], category=struct.unpack_from('<I',t,0x38)[0], descriptor_index=struct.unpack_from('<I',t,0x3c)[0], cursor_bits=struct.unpack_from('<I',t,0x40)[0], rate_bits=struct.unpack_from('<I',t,0x44)[0], state=struct.unpack_from('<I',t,0x48)[0], identity=struct.unpack_from('<I',t,0x4c)[0], raw=t.hex()))
        for name,start,end in [('controller',632,728),('controls',728,856),('descriptor_bytes',856,940),('sample_bytes',940,956),('xmm',956,1052)]:e[name]=raw[start:end].hex()
        if e['valid_mask'] & 32:
            sample = raw[940:956]
            category_byte = struct.unpack_from('<b',sample)[0]
            quantized = struct.unpack_from('<H',sample,2)[0]
            e['sample_decoded'] = dict(category_byte=category_byte, category_remainder=category_byte-int(category_byte/5)*5, weight_u16=quantized, weight_float=struct.unpack('<f',struct.pack('<f',quantized/65535.0))[0])
        events.append(e)
    return dict(schema=schema,installed=installed,dropped=dropped,total=total,overwritten=total-count,events=events,validity_bits={'fx':1,'player_identity':2,'state':4,'caller':8,'descriptor':16,'sample':32,'output':64,'read_exception':2147483648})

def camera_calculations(data):
    size, capacity = 9568, 512
    if len(data) != 24+size*capacity:
        raise ValueError('Invalid camera calculation trace size')
    schema, installed, count, dropped, total = struct.unpack_from('<4IQ', data)
    if schema != 1 or installed > 1 or count > capacity or count > total:
        raise ValueError('Invalid camera calculation header')
    events, natural, comparisons = [], {}, []
    for seq in range(total-count, total):
        raw=data[24+(seq%capacity)*size:24+(seq%capacity+1)*size]
        sequence,qpc,thread,phase,frame,site,valid,children_valid=struct.unpack_from('<2Q6I',raw)
        if sequence!=seq or site>=6 or valid&~63 or children_valid&~65535:
            raise ValueError('Invalid camera calculation event')
        e=dict(sequence=seq,qpc=qpc,thread=thread,phase=phase,frame=frame,site=site,valid=valid,children_valid=children_valid)
        key=(thread,frame,site)
        if phase==0:natural[key]=(e,raw)
        elif phase in (2,3,4):
            previous=natural.get(key)
            comparison=dict(sequence=seq,frame=frame,site=site,phase=phase,original_available=previous is not None)
            if previous:
                old,source=previous
                comparison['original_sequence']=old['sequence']
                comparison['differences']={}
                for name,start,length,mask in [('controller',680,864,8),('temporary',1544,464,16),('rng',9432,132,32)]:
                    if valid&old['valid']&mask:
                        diff=difference(source[start:start+length],raw[start:start+length])
                        if diff['changed_bytes']:comparison['differences'][name]=diff
                for child in range(16):
                    if children_valid&old['children_valid']&(1<<child):
                        start=2008+child*464
                        diff=difference(source[start:start+464],raw[start:start+464])
                        if diff['changed_bytes']:comparison['differences']['child_'+str(child)]=diff
                comparison['complete_fields']=(valid&old['valid']&45)==45 and (site<3 or bool(valid&old['valid']&16)) and children_valid==old['children_valid']==65535
            comparisons.append(comparison)
        events.append(e)
    return dict(schema=schema,installed=installed,total=total,dropped=dropped,overwritten=total-count,
                sites=['before_advance','after_blend','after_effect','after_copy','after_adjustments','final_output'],
                events=events,comparisons=comparisons,note='Pairs use same thread/frame/site and preceding natural sample; missing or overwritten samples are not equality evidence. Raw registers and FX state retained in binary.')

def shared_rng(data):
    capacity, size = 8192, 168
    if len(data) != 24 + capacity*size:
        raise ValueError('Invalid shared RNG trace size')
    schema, installed, count, dropped, total = struct.unpack_from('<4IQ', data)
    if schema != 1 or installed > 1 or count > capacity or count > total:
        raise ValueError('Invalid shared RNG trace header')
    events = []
    for seq in range(total-count, total):
        v = struct.unpack_from('<3Q36I', data, 24+(seq % capacity)*size)
        e = dict(zip(('sequence','caller','result','thread','phase','frame','stream'), v[:7]))
        if e['sequence'] != seq or e['stream'] > 3:
            raise ValueError('Invalid shared RNG event')
        e['state'] = list(v[7:]);e['caller_rva'] = hex(e['caller'])
        events.append(e)
    comparisons = []
    begin = None
    for e in events:
        if e['stream'] == 0:
            begin = e
        elif e['stream'] == 3 and begin and e['thread']==begin['thread']:
            replay = [x for x in events if begin['sequence'] < x['sequence'] < e['sequence'] and x['thread']==e['thread'] and x['phase']==2 and x['stream'] in (1,2)]
            frame = replay[0]['frame'] if replay else None
            natural = [x for x in events if x['sequence'] < begin['sequence'] and x['thread']==e['thread'] and x['phase']==0 and x['frame']==frame and x['stream'] in (1,2)]
            # Frame numbers can repeat across rounds. Retain the last contiguous
            # occurrence, never merge distinct visits to the same frame.
            if natural:
                first = natural[-1]['sequence']
                for x in reversed(events[:next(i for i,x in enumerate(events) if x['sequence']==first)]):
                    if x['thread']!=e['thread']:continue
                    if x['phase']!=0 or x['frame']!=frame:break
                    first=x['sequence']
                natural=[x for x in natural if x['sequence']>=first]
            mismatch = None
            for i in range(max(len(natural),len(replay))):
                a=natural[i] if i<len(natural) else None;b=replay[i] if i<len(replay) else None
                if a is None or b is None or any(a[k]!=b[k] for k in ('stream','caller','result','state')):
                    mismatch=dict(index=i,natural=a,replay=b);break
            comparisons.append(dict(source_frame=begin['result'],observed_frame=frame,natural_calls=len(natural),replay_calls=len(replay),first_difference=mismatch,coverage='candidate_only' if dropped or not natural or total>count else 'retained_trace',entry_state=begin['state'],end_state=e['state']))
            begin=None
    return dict(schema=schema,installed=installed,dropped=dropped,total=total,overwritten=total-count,events=events,comparisons=comparisons,scope='direct helper calls; inline writers and racing native calls are not fully covered')

def inline_rng(data):
    if len(data) != 24+8192*168:
        raise ValueError('Invalid inline RNG size')
    schema, installed, count, dropped, total = struct.unpack_from('<4IQ', data)
    if schema != 1 or installed > 1 or count > 8192 or count > total:
        raise ValueError('Invalid inline RNG header')
    sites = [0x2f9f1e,0x2fa159,0x33154e,0x331927,0x33220d,0x3330a4,0x3346b3,0x34f867]
    names = ['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15']
    events = []
    for seq in range(total-count,total):
        v = struct.unpack_from('<17Q8I',data,24+(seq%8192)*168)
        e = dict(zip(('site','thread','phase','frame','before','after','expected','valid'),v[17:]))
        if v[0] != seq or e['site'] > 9 or e['valid'] > 1:
            raise ValueError('Invalid inline RNG event')
        e.update(sequence=seq, registers=dict(zip(names,v[1:17])))
        e['instruction_rva'] = hex(sites[e['site']]) if e['site'] < 8 else None
        e['boundary'] = {8:'before_replay',9:'after_replay'}.get(e['site'])
        if e['site'] >= 8: e['seed_matches'] = bool(e['valid'] and e['before']==e['expected'])
        events.append(e)
    return dict(schema=schema,installed=installed,dropped=dropped,total=total,overwritten=total-count,events=events,
                coverage='Eight pinned inline stores; records before instruction execution, after is proposed store value. Registers are raw context, not validated pointers.')

def difference(a, b):
    offsets = [i for i in range(min(len(a),len(b))) if a[i] != b[i]]
    return dict(expected_size=len(a), actual_size=len(b),
                changed_bytes=len(offsets)+abs(len(a)-len(b)),
                first_offsets=offsets[:64], omitted_offsets=max(0,len(offsets)-64))

def selector_snapshot(data):
    stride = 48 + 24 + 24 + 576 + 262144
    if len(data) != 16 + 2*stride:
        raise ValueError('Invalid selector snapshot size')
    schema, valid, epoch = struct.unpack_from('<IIQ', data)
    if schema == 0 and not valid:
        return dict(schema=schema,valid=valid,epoch=epoch,entries=[])
    if schema != 1:
        raise ValueError('Unknown selector schema')
    entries = []
    for side in range(2):
        at = 16 + side*stride
        entry = dict(zip(('player','object','header','allocation','groups_pointer','variants_pointer','active','previous','groups','variants','prefix','bytes'),struct.unpack_from('<6Q6I',data,at)))
        if entry['bytes'] > 262144 or entry['groups'] > entry['prefix']//8 or entry['prefix'] > entry['bytes']:
            raise ValueError('Invalid selector table bounds')
        entry['pending'] = []
        for i in range(24):
            delay,action,param = struct.unpack_from('<3h',data,at+96+i*24)
            if delay != -1:
                owner,flags = struct.unpack_from('<QI',data,at+96+i*24+8)
                entry['pending'].append(dict(index=i,delay=delay,action=action,param=param,owner=owner,flags=flags))
        entry['counters'] = [struct.unpack_from('<h',data,at+672+i*8+2)[0] for i in range(entry['groups'])]
        entries.append(entry)
    return dict(schema=schema,valid=valid,epoch=epoch,entries=entries)

def analyze(sections):
    result = {'note': 'Raw bytes including padding and accepted exceptions; native checks determine validity.', 'pairs': {}}
    if 'selector.enabled' in sections:
        result['selector'] = {'enabled':bool(struct.unpack('<?',sections['selector.enabled'])[0])}
        for name in ('source','expected','restored','actual'):
            result['selector'][name] = selector_snapshot(sections['selector.'+name])
        result['selector']['diagnostic'] = dict(zip(('schema','phase','reason','side','offset','expected','actual','written','verified'),struct.unpack('<9I',sections['selector.diagnostic'])))
    if 'wind.rng_boundary' in sections:
        data = sections['wind.rng_boundary']
        if len(data) != 548:
            raise ValueError('Invalid shared RNG boundary size')
        names = ('schema','check','source_valid','expected_valid','entry_valid','after_valid','entry_match','after_match','exception')
        boundary = dict(zip(names, struct.unpack_from('<9I', data)))
        for i, name in enumerate(('source','expected','entry','after')):
            words = struct.unpack_from('<32I', data, 36+i*128)
            boundary[name] = {'generator_words': list(words[:25]), 'cursor': words[25], 'secondary_words': list(words[26:])}
        result['effect_rng_boundary'] = boundary
    if 'camera.calculations' in sections:
        result['camera_calculations'] = camera_calculations(sections['camera.calculations'])
    if 'timer.lookup' in sections:
        result['timer_lookup'] = timer_lookup(sections['timer.lookup'])
    if 'rng.inline' in sections:
        result['inline_rng'] = inline_rng(sections['rng.inline'])
    if 'animation.copy' in sections:
        result['animation_copy'] = animation_copy(sections['animation.copy'])
    if 'rng.shared_callers' in sections:
        result['shared_rng'] = shared_rng(sections['rng.shared_callers'])
    if 'rng.callers' in sections:
        raw = sections['rng.callers']
        if len(raw) != 24+8192*40:
            raise ValueError('Invalid RNG caller trace size')
        schema, installed, count, dropped, total = struct.unpack_from('<4IQ', raw)
        if schema != 1 or count > 8192 or count > total:
            raise ValueError('Invalid RNG caller trace metadata')
        names = ('sequence','caller_rva','thread','phase','frame','before','after','reserved')
        events = [dict(zip(names, struct.unpack_from('<QQ6I',raw,24+(seq%8192)*40))) for seq in range(total-count,total)]
        if any(e['sequence'] != seq for e,seq in zip(events,range(total-count,total))):
            raise ValueError('Invalid RNG trace sequence')
        result['rng_callers'] = dict(schema=schema, installed=bool(installed), dropped=dropped,
            overwritten=total-count, coverage='34F550 helper only; inline RNG writes are not intercepted',
            phases={0:'natural/unclassified',1:'restoration',2:'reference replay',3:'prediction',4:'correction'}, events=events)
    if 'motion.signed_zero' in sections:
        result['motion_signed_zero_exception'] = dict(zip(('count','record','offset','expected_bits','actual_bits'),struct.unpack('<5Q',sections['motion.signed_zero'])))
    if 'wind.preflight_shared' in sections:
        raw = sections['wind.preflight_shared']
        if len(raw) != 2080:
            raise ValueError('Invalid shared-state diagnostic size')
        reason, record, kind, size, offset, live_valid, address = struct.unpack_from('<6IQ', raw)
        if size > 512 or live_valid > 1:
            raise ValueError('Invalid shared-state diagnostic bounds')
        reasons = {0:'passed',1:'topology mismatch',2:'next/current snapshot mismatch',
                   3:'current snapshot/live memory mismatch',4:'non-restorable shared field changed',
                   7:'read exception',8:'record policy check rejected'}
        detail = dict(reason=reason, check=('not observed' if record == 0xffffffff and reason == 0 else reasons.get(reason,'unknown')), record=record,
                      kind=kind, size=size, offset=None if offset == 0xffffffff else offset,
                      address=hex(address), live_valid=bool(live_valid),
                      failure_line=struct.unpack('<I',sections['wind.preflight_line'])[0])
        for i, name in enumerate(('saved','next','current','live')):
            detail[name] = raw[32+i*512:32+i*512+size].hex() if name != 'live' or live_valid else None
        result['shared_state_preflight'] = detail
    if 'camera.restore_diagnostic' in sections:
        raw = sections['camera.restore_diagnostic']
        if len(raw) != 32:
            raise ValueError('Invalid camera restore diagnostic')
        names = ('failure_line', 'record', 'section', 'offset', 'saved', 'next', 'loaded', 'live')
        result['camera_restore'] = dict(zip(names, struct.unpack('<8I', raw)))
        if 'camera.source' in sections and 'camera.loaded' in sections:
            result['camera_source_loaded_raw'] = difference(sections['camera.source'], sections['camera.loaded'])
    if 'blend_timer.history' in sections:
        count = struct.unpack('<I', sections['blend_timer.count'])[0]
        history = sections['blend_timer.history']
        sample_size = 8+2*3*128
        if count > 32 or len(history) != 32*sample_size:
            raise ValueError('Invalid blend timer history')
        def timer_sample(data):
            if len(data) != sample_size:
                raise ValueError('Invalid blend timer sample')
            phase, valid = struct.unpack_from('<II', data)
            slots = []
            for side in range(2):
                for slot in range(3):
                    offset = 8+(side*3+slot)*128
                    # Raw words preserve NaNs and exact bits; never dereference pointers.
                    words = struct.unpack_from('<32I', data, offset)
                    slots.append(dict(side=side, slot=slot, valid=bool(valid & (1 << (side*3+slot))),
                                      scalar_words=list(words[6:22]),
                                      pointer_words=list(words[:6])+list(words[22:]),
                                      cursor_bits=words[16], rate_bits=words[17], state=words[18]))
            return dict(phase=phase, slots=slots)
        result['blend_timers'] = dict(
            dropped=struct.unpack('<I', sections['blend_timer.dropped'])[0],
            expected=timer_sample(sections['blend_timer.expected']),
            history=[timer_sample(history[i*sample_size:(i+1)*sample_size]) for i in range(count)])
        if 'blend_timer.source' in sections:
            result['blend_timers']['source']=timer_sample(sections['blend_timer.source'])
            for field in ['restore_requested','restored']:
                result['blend_timers'][field]=bool(struct.unpack('<?',sections['blend_timer.'+field])[0])
        for field in ['next','live']:
            if 'blend_timer.'+field in sections:
                result['blend_timers'][field]=timer_sample(sections['blend_timer.'+field])
        if 'blend_timer.diagnostic' in sections:
            result['blend_timers']['diagnostic']=dict(zip(
                ['phase','reason','side','slot','offset','expected','actual'],
                struct.unpack('<7I',sections['blend_timer.diagnostic'])))
    if 'postload.failure_check' in sections:
        result['postload_failure_check'] = struct.unpack('<I', sections['postload.failure_check'])[0]
    if 'scalar_reset.diagnostic' in sections:
        data = sections['scalar_reset.diagnostic']
        if len(data) != 48:
            raise ValueError('Invalid scalar reset diagnostic size')
        result['scalar_resets'] = [dict(zip(
            ['side', 'saved', 'loaded', 'live', 'reason', 'restored'],
            struct.unpack_from('<6I', data, side * 24))) for side in range(2)]
    for name, value in sections.items():
        other = name.replace('.expected', '.actual')
        if '.expected' in name and other in sections:
            result['pairs'][name] = difference(value, sections[other])
    for side in ['expected', 'actual']:
        prefix = 'native.'+side
        count = struct.unpack('<I', sections[prefix+'.writes'])[0]
        size = struct.unpack('<Q', sections[prefix+'.size'])[0]
        lengths, payload, traces = (sections[prefix+s] for s in ['.lengths','.payload','.traces'])
        if count > len(lengths)//8 or count > len(traces)//32 or size > len(payload):
            raise ValueError('Invalid native bounds')
        records, at = [], 0
        for i in range(count):
            length = struct.unpack_from('<Q', lengths, i*8)[0]
            if length > size-at:
                raise ValueError('Invalid native extent')
            source, destination, writer, reader = struct.unpack_from('<QQQQ', traces, i*32)
            records.append(dict(index=i, offset=at, length=length, source=source, writer=writer, reader=reader))
            at += length
        if at != size:
            raise ValueError('Native size mismatch')
        result[prefix] = records
    a, b = result['native.expected'], result['native.actual']
    result['native_shape_equal'] = [r['length'] for r in a] == [r['length'] for r in b]
    if result['native_shape_equal']:
        result['native_differences'] = []
        for record in a:
            start, end = record['offset'], record['offset']+record['length']
            diff = difference(sections['native.expected.payload'][start:end], sections['native.actual.payload'][start:end])
            if diff['changed_bytes']:
                result['native_differences'].append(dict(record=record['index'], **diff))
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('evidence')
    parser.add_argument('--output')
    args = parser.parse_args()
    raw = Path(args.evidence).read_bytes()
    result = inline_rng(raw) if len(raw)==24+8192*168 else animation_copy(raw) if len(raw) in (24+4096*88,24+4096*1048) else analyze(read(args.evidence))
    text = json.dumps(result, indent=2)
    if args.output:
        Path(args.output).write_text(text)
    else:
        print(text)
