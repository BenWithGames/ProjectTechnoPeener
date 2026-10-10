"""Same-PC pipeline timings only. Never subtract clocks from different computers."""
import argparse,csv,json,math,statistics
from pathlib import Path

def stats(ticks,frequency):
    if frequency<=0:raise ValueError('Invalid QPC frequency')
    values=sorted(t*1000/frequency for t in ticks)
    if any(v<0 for v in values):raise ValueError('Timestamp order violation')
    if not values:return {'samples':0}
    return dict(samples=len(values),min_ms=values[0],median_ms=statistics.median(values),p95_ms=values[math.ceil(.95*len(values))-1],max_ms=values[-1])

def read(path):
    with open(path,newline='') as f:return [{k:int(v) for k,v in r.items()} for r in csv.DictReader(f)]

def analyze(stem):
    meta=dict(line.split('=',1) for line in Path(str(stem)+'.txt').read_text().splitlines() if '=' in line)
    frequency=int(meta['qpc_frequency']);local=read(str(stem)+'-local.csv');timing=read(str(stem)+'-timing.csv')
    if len(local)!=len(timing):raise ValueError('Mismatched lengths')
    delays={'capture_to_send_complete':[],'capture_to_import':[],'import_to_send_begin':[],'send_call':[]}
    missing=0
    for i,(r,t) in enumerate(zip(local,timing)):
        if r['sample']!=i or t['sample']!=i or r['word0']!=t['local_outer']:raise ValueError('Mismatched records')
        capture=(r['word12']<<32)|r['word11'];sent=t['first_send_qpc']
        if not capture or not sent:missing+=1;continue
        delays['capture_to_send_complete'].append(sent-capture)
        imported=t.get('import_qpc',0);begin=t.get('first_send_begin_qpc',0)
        if imported and begin:
            delays['capture_to_import'].append(imported-capture);delays['import_to_send_begin'].append(begin-imported);delays['send_call'].append(sent-begin)
    result={'source':str(stem),'status':meta.get('status'),'missing_send_samples':missing,'metrics':{k:stats(v,frequency) for k,v in delays.items()},
            'limitations':['No one-way network latency measurement: peer clocks are not synchronized.','Arrival means application dequeue, not network-interface arrival.','No game input consumption is measured; rollback remains disabled.']}
    path=Path(str(stem)+'-pipeline.csv')
    if path.exists():
        rows=read(path)
        for label,start,end in [('receive_work','start_qpc','receive_end_qpc'),('pre_send_work','receive_end_qpc','send_begin_qpc'),('send_batch_work','send_begin_qpc','send_end_qpc')]:
            result['metrics'][label]=stats([r[end]-r[start] for r in rows],frequency)
        result['metrics']['pump_interval']=stats([b['start_qpc']-a['start_qpc'] for a,b in zip(rows,rows[1:])],frequency)
        result['pipeline_dropped']=int(meta.get('pipeline_dropped','0'))
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stem',type=Path);p.add_argument('--output',type=Path);args=p.parse_args()
    result=analyze(args.stem);text=json.dumps(result,indent=2)
    if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(text+'\n')
    print(text)
