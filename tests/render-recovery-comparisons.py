"""Validate independent retained recovery references with unchanged image gates."""
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from camera_projection import validate_camera


def reference_samples(width):
    return 29 if width == 480 else 5


def delta(first, second):
    difference = np.abs(first.astype(np.int16) - second.astype(np.int16))
    return {'maximum': int(difference.max()),
            'fraction': float(np.any(difference, axis=2).mean())}


def compare_arrays(controls, recovered, width, height):
    samples = reference_samples(width)
    if len(controls) != samples or len(recovered) != samples:
        raise ValueError(f'Exactly {samples} independent pairs are required for this pose')
    frames = controls + recovered
    if width <= 0 or height <= 0 or any(
            not isinstance(frame, np.ndarray) or frame.dtype != np.uint8
            or frame.shape != (height, width, 4) for frame in frames):
        raise ValueError('Capture arrays must be RGBA8 with the requested dimensions')
    references = [np.median(np.stack(stack), axis=0).astype(np.uint8)
                  for stack in (controls, recovered)]
    individual = [delta(first, second) for first, second in zip(controls, recovered)]
    median = delta(*references)
    raw_to_median = {kind: [delta(frame, reference) for frame in stack]
                     for kind, stack, reference in zip(
                         ('control', 'recovered'), (controls, recovered), references)}
    raw_guard = all(row['maximum'] <= 1
                    for rows in raw_to_median.values() for row in rows)
    passed = raw_guard and median['maximum'] <= 1 and median['fraction'] <= .001
    return {'individual': individual, 'median': median,
            'raw_to_median': raw_to_median, 'passed': passed}, *references


def run(root):
    root = Path(root)
    report = json.loads((root / 'attempts.json').read_text())
    if not report['passed'] or [case['index'] for case in report['cases']] != list(range(54)):
        raise ValueError('Incomplete recovery fixture')
    ownership = json.loads((root / 'terrain-ownership.json').read_text())
    if not ownership.get('completion') or len(ownership.get('cases', [])) != 6:
        raise ValueError('Complete scene terrain ownership evidence required')
    for case in ownership['cases']:
        if (case['recovery_rows'] != 0 or case['preparations'] != 1
                or case['hash_before'] != case['hash_after']
                or case['complete_slots'] != case['uploaded_slots']
                or case['generation_after'] <= case['generation_before']
                or case['original_rows'] != (4 if case['partial'] else 0)):
            raise ValueError('Terrain changed or required uploads were omitted during recovery')
    results = []
    reference_camera = None
    for case in report['cases']:
        if case.get('seed') != '6215337262362608466':
            raise ValueError('Missing or changed requested recovery seed')
        samples = reference_samples(case['width'])
        if len(case['attempts']) != samples or any(
                attempt['outcome'] != 0 or attempt['hr'] != 0 or attempt['recreations'] != 1
                for attempt in case['attempts']):
            raise ValueError('Incomplete recovery attempts')
        stacks = []
        for kind in ('control', 'recovered'):
            frames = []
            for sample in range(samples):
                path = root / f"{case['index']}-{sample}-{kind}.png"
                with Image.open(path) as image:
                    if image.size != (case['width'], case['height']):
                        raise ValueError('Wrong recovery dimensions')
                    frames.append(np.asarray(image.convert('RGBA')).copy())
                sidecar = root / f'camera-{path.stem}.json'
                camera = json.loads(sidecar.read_text())
                if camera['width'] != case['width'] or camera['height'] != case['height']:
                    raise ValueError('Wrong sidecar dimensions')
                validate_camera(camera['camera'])
                if reference_camera is None:
                    reference_camera = camera['camera']
                if reference_camera != camera['camera']:
                    raise ValueError('Changed recovery camera')
            stacks.append(frames)
        result, control, recovered = compare_arrays(*stacks, case['width'], case['height'])
        for kind, reference in (('control', control), ('recovered', recovered)):
            Image.fromarray(reference).save(root / f"consensus-{case['index']}-{kind}.png")
        # Each stage retains its own complete reference sets. No images from
        # another stage contribute to either median or the raw maximum guard.
        results.append({'request': case, **result})
    hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
              for path in root.iterdir() if path.is_file()}
    complete = all(row['passed'] for row in results)
    (root / 'comparisons.json').write_text(json.dumps({
        'completion': complete, 'results': results, 'hashes': hashes,
        'reference': 'independent per-stage medians: 29 pairs at 480, 5 at other sizes',
        'maximum_channel_delta': 1, 'maximum_changed_pixel_fraction': .001,
        'maximum_raw_to_median_delta': 1}, indent=2))
    if complete:
        print('PASS: 54 independent recovery pose/stage medians; 29 small/5 larger retained pairs')
    else:
        print('FAIL: recovery capture comparison; raw evidence retained')
    return 0 if complete else 1


if __name__ == '__main__':
    raise SystemExit(run(Path(sys.argv[1])))
