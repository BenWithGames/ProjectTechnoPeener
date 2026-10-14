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

def difference(a, b):
    offsets = [i for i in range(min(len(a),len(b))) if a[i] != b[i]]
    return dict(expected_size=len(a), actual_size=len(b),
                changed_bytes=len(offsets)+abs(len(a)-len(b)),
                first_offsets=offsets[:64], omitted_offsets=max(0,len(offsets)-64))

def analyze(sections):
    result = {'note': 'Raw bytes including padding and accepted exceptions; native checks determine validity.', 'pairs': {}}
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
    text = json.dumps(analyze(read(args.evidence)), indent=2)
    if args.output:
        Path(args.output).write_text(text)
    else:
        print(text)
