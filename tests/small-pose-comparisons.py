"""Compare explicitly requested baseline/candidate batch captures at 480 x 320."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import numpy as np
from PIL import Image
from camera_projection import validate_camera

spec = importlib.util.spec_from_file_location('binding_comparisons', Path(__file__).with_name('shader-binding-comparisons.py'))
comparisons = importlib.util.module_from_spec(spec)
spec.loader.exec_module(comparisons)

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def run(root):
    baseline, candidate = root/'baseline', root/'candidate'
    first, second = read(baseline/'poses.json'), read(candidate/'poses.json')
    if first != second or not first['completion'] or first['width'] != 480 or first['height'] != 320 or first['captures_per_pose'] != 5:
        raise ValueError('Complete matching 480 x 320 five-capture manifests required')
    expected = []
    progress = (0, .25, .5, 1)
    for p in range(3):
        for pp in range(4):
            expected.append(dict(name=f'p{p}-pp{pp}',palette=p,
                palette_progress=progress[pp],seed='6215337262362608466',
                cell='36028797018963971',phase=.25,city_light_phase=.25))
    if first['poses'] != expected:
        raise ValueError('Incomplete or changed small pose requests')
    reference_camera = None
    records = []
    for pose in expected:
        paths = [directory/f"{pose['name']}-{frame}.png" for directory in (baseline,candidate) for frame in range(5)]
        arrays = [np.asarray(Image.open(path).convert('RGB')) for path in paths]
        if any(array.shape != (320,480,3) for array in arrays):
            raise ValueError('Small capture dimensions disagree')
        for path in paths[5:]:
            sidecar = read(path.with_name('camera-'+path.stem+'.json'))
            camera = validate_camera(sidecar['camera'])
            if sidecar['width'] != 480 or sidecar['height'] != 320:
                raise ValueError('Small capture camera dimensions disagree')
            if reference_camera is not None and camera != reference_camera:
                raise ValueError('Small capture camera changed')
            reference_camera = camera
        result, control, changed = comparisons.compare_arrays(arrays[:5],arrays[5:])
        result.update(name=pose['name'],raw_pair=comparisons.stats(arrays[0],arrays[5]),
            baseline_repeat=comparisons.stats(arrays[0],arrays[1]),candidate_repeat=comparisons.stats(arrays[5],arrays[6]),
            capture_sha256=[hashlib.sha256(path.read_bytes()).hexdigest() for path in paths])
        records.append(result)
        Image.fromarray(control).save(root/(pose['name']+'-baseline-consensus.png'))
        Image.fromarray(changed).save(root/(pose['name']+'-candidate-consensus.png'))
    report = dict(completion=all(record['passed'] for record in records),pairs=len(expected),camera=reference_camera,
        reference='median of five captures per renderer',maximum_channel_delta=1,maximum_changed_pixel_fraction=.001,
        limitation='Individual small captures can exceed the sparse resolve fraction; this certifies median scene references.',records=records)
    (root/'comparison.json').write_text(json.dumps(report,indent=2))
    print(f"{'PASS' if report['completion'] else 'FAIL'}: {len(expected)} small scene consensus pairs; max delta {max(r['maximum_channel_delta'] for r in records)}; max changed fraction {max(r['changed_pixel_fraction'] for r in records):.8f}")
    return 0 if report['completion'] else 1

if __name__ == '__main__':
    sys.exit(run(Path(sys.argv[1])))
