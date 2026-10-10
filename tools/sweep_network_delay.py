"""Compare hypothetical delays using a completed Steam capture; never modifies SC6."""
import argparse
import csv
import json
from pathlib import Path
from analyze_shadow_prediction import analyze
from analyze_steam_inputs import read

def sweep(stem):
    metadata=dict(line.split('=',1) for line in Path(str(stem)+'.txt').read_text().splitlines() if '=' in line)
    if metadata.get('status')!='PASS' or metadata.get('shadow_remote_slot') not in ('1','2'):
        raise ValueError('Requires a passing shadow capture with a recorded remote slot')
    local=read(str(stem)+'-local.csv');remote=read(str(stem)+'-remote.csv')
    with open(str(stem)+'-timing.csv',newline='') as stream:
        timing=[{k:int(v) for k,v in row.items()} for row in csv.DictReader(stream)]
    results=[analyze(local,remote,timing,int(metadata['shadow_remote_slot']),'hold_clear_edges',delay) for delay in range(7)]
    return {'source':str(stem),'scope':'recorded_network_delay_sweep','actual_replays':0,'rollback_enabled':False,
            'limitation':'Frame alignment and slot mapping are assumed. Original SC6 networking paced these frames; this cannot predict replacement-netcode latency. Depth excludes unseeded frames and may be censored.',
            'profiles':results}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('stem',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();result=sweep(args.stem);args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print('delay,errors,max_depth,over_3,unseeded,truth_mismatch,censored')
    for row in result['profiles']:
        print(','.join(str(row[key]) for key in ['hypothetical_delay_frames','predicted_pair_mismatches','max_estimated_replay_frames','estimated_corrections_over_three_frames','skipped_unseeded_frames','ground_truth_mismatches','censored_corrections']))
