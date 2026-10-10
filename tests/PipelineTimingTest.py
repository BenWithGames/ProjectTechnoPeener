import pathlib,sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from analyze_pipeline import stats
r=stats([0,100,200,300],10000)
assert r['samples']==4 and r['median_ms']==15 and r['p95_ms']==30
assert stats([],10000)=={'samples':0}
for values,freq in [([-1],10000),([1],0)]:
    try:stats(values,freq)
    except ValueError:pass
    else:raise AssertionError('Invalid timing accepted')
print('PASS: timing units, quantiles, empty input, reversed timestamps and frequency validation')
