import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from analyze_shadow_prediction import analyze

rows = [dict(word0=i, word1=0, word2=0, word3=i, word4=0,
             word11=10000+i*1000, word12=0) for i in range(8)]
timing = [dict(sample=i, local_outer=i, remote_outer=i, arrival_qpc=11500+i*1000)
          for i in range(8)]
result = analyze(rows, rows, timing, 2)
assert result['skipped_unseeded_frames'] == 2
assert result['predicted_pair_mismatches'] == 6
assert result['max_estimated_replay_frames'] == 2
assert result['actual_replays'] == 0
instant = [dict(t, arrival_qpc=rows[i]['word11']) for i, t in enumerate(timing)]
assert analyze(rows, rows, instant, 2)['matching_input_unavailable_frames'] == 0
other = [dict(r) for r in rows]
other[3]['word3'] = 999
assert analyze(rows, other, timing, 2)['ground_truth_mismatches'] == 1
assert analyze(rows, rows, timing, 2, delay_frames=2)['matching_input_unavailable_frames'] == 0
held = [dict(r, word3=1, word4=1 if i==0 else 0) for i,r in enumerate(rows)]
repeat = analyze(held, held, timing, 2, 'repeat_pair')
cleared = analyze(held, held, timing, 2, 'hold_clear_edges')
assert repeat['predicted_pair_mismatches'] == 1
assert cleared['predicted_pair_mismatches'] == 0
print('PASS: delayed changing inputs, zero delay, unseeded exclusion, replay-depth estimate, ground-truth mismatch')
