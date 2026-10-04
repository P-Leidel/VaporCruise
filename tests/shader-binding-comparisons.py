"""Strict binding equivalence with retained raw captures and median references references."""
import json
from pathlib import Path
import sys
import numpy as np
from PIL import Image

def stats(first,second):
    delta=np.abs(first.astype(np.int16)-second.astype(np.int16))
    maximum=int(delta.max());fraction=float(np.any(delta,axis=2).mean())
    return dict(maximum_channel_delta=maximum,changed_pixel_fraction=fraction,passed=maximum<=1 and fraction<=.001)

def compare_arrays(controls,candidates):
    if len(controls)!=len(candidates) or len(controls)<3 or len(controls)%2==0: raise ValueError('Matching odd capture counts of at least three required')
    if any(v.shape!=controls[0].shape for v in controls+candidates): raise ValueError('Capture dimensions disagree')
    control=np.median(np.stack(controls),axis=0).astype(np.uint8)
    candidate=np.median(np.stack(candidates),axis=0).astype(np.uint8)
    return stats(control,candidate),control,candidate

def run(root):
    records=[]
    for path in sorted(root.glob('*-control.png')):
        stem=path.name.removesuffix('-control.png')
        paths=[root/(stem+suffix+'.png') for suffix in ['-control','-control2','-control3','-control4','-control5','-cleared','-cleared2','-cleared3','-cleared4','-cleared5']]
        arrays=[np.asarray(Image.open(p).convert('RGB')) for p in paths]
        result,control,candidate=compare_arrays(arrays[:5],arrays[5:])
        result.update(name=stem,raw_repeat=stats(arrays[0],arrays[1]),raw_cleared=stats(arrays[0],arrays[5]))
        records.append(result)
        Image.fromarray(control).save(root/(stem+'-consensus-control.png'))
        Image.fromarray(candidate).save(root/(stem+'-consensus-cleared.png'))
    if len(records)!=25: raise ValueError(f'Incomplete binding matrix: {len(records)}/25')
    report=dict(completion=all(r['passed'] for r in records),pairs=len(records),reference='median of five captures per path',
                maximum_channel_delta=1,maximum_changed_pixel_fraction=.001,records=records)
    (root/'comparisons.json').write_text(json.dumps(report,indent=2))
    print(f"{'PASS' if report['completion'] else 'FAIL'}: {len(records)} binding consensus pairs; max delta {max(r['maximum_channel_delta'] for r in records)}; max changed fraction {max(r['changed_pixel_fraction'] for r in records):.8f}")
    return 0 if report['completion'] else 1

if __name__=='__main__': sys.exit(run(Path(sys.argv[1])))
