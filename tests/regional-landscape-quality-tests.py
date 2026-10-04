"""Compare matching native regional-landscape captures; run --self-test for guard probes."""
import argparse
from importlib.util import module_from_spec, spec_from_file_location
import json
from pathlib import Path
import re
import unittest

import numpy as np
from PIL import Image
from camera_projection import load_camera, legacy_fixture_camera

DIRECTORY = Path()
BASELINE = Path()
REFERENCE_DIRECTORY = None
BASELINE_REFERENCE_DIRECTORY = None
REPORT = None
OBSERVATIONS = {'preservation': {}, 'tiny_steps': {}, 'regional_color': {}, 'repeat': {}}
PALETTES = ('magenta', 'cyan', 'amber')
SEEDS = ('6215337262362608466', '18446744073709551615')
_repeat_spec = spec_from_file_location('regional_existing_repeat_protocol',
                                     Path(__file__).with_name('current-background-quality-tests.py'))
_repeat_module = module_from_spec(_repeat_spec)
_repeat_spec.loader.exec_module(_repeat_module)
median_reference = _repeat_module.median_reference


def ground_coordinates(shape, camera):
    height, width = shape[:2]
    yy, xx = np.indices((height, width))
    below = (yy+.5)/height-camera['horizon_fraction']
    depth = camera['height']*camera['focal_scale']/np.maximum(below, 1e-9)
    lateral = (xx+.5-width*.5)*depth/(height*camera['focal_scale'])
    return yy, lateral, depth


def protected_masks(shape, camera):
    height, width = shape[:2]
    yy, lateral, depth = ground_coordinates(shape, camera)
    foreground = yy >= height*(camera['horizon_fraction']+.12)
    horizontal = np.abs(np.arange(width)[None, :]+.5-width*.5)
    above_horizon = height*camera['horizon_fraction']-(yy+.5)
    # Projection gives above/abs(horizontal)=(worldY-cameraHeight)/abs(worldX).
    # A raw-height/abs(worldX) bound of1.5 conservatively covers the terrain
    # clearance envelope. It also covers archived object-bearing baselines.
    # Fading only lowers vertices. Eight pixels cover nearby raster/resolve
    # footprints; the low horizon band is excluded separately. This guards
    # central atmosphere/solar/city pixels, not the entire skyline silhouette.
    central_atmosphere = ((above_horizon > 1.5*horizontal+8) &
                          (above_horizon > height*.04))
    return {
        'central_upper_atmosphere': central_atmosphere,
        'road_interior': foreground & (np.abs(lateral) <= 2.9),
        # Center cores/halos and road-facing side-marking profiles. Only the
        # road-facing part is protected: terrain wires outside may change hue.
        'road_line_profiles': foreground & ((np.abs(lateral) <= .10) |
                                            ((np.abs(lateral) >= 2.85) & (np.abs(lateral) <= 3.0))),
        'black_pavement': foreground & (depth <= 30) &
                          (np.abs(lateral) >= .75) & (np.abs(lateral) <= 2.25),
    }


def delta_stats(first, second, mask):
    delta = np.abs(first.astype(np.int16)-second.astype(np.int16))[mask]
    if not len(delta):
        raise ValueError('Empty comparison mask')
    return dict(pixels=int(len(delta)), maximum=int(delta.max()),
                mean=float(delta.mean()), p99=float(np.percentile(delta, 99)),
                changed_fraction=float(np.any(delta, axis=1).mean()))


def preservation_errors(reference, candidate, camera):
    errors = []
    for label, mask in protected_masks(reference.shape, camera).items():
        if not np.any(mask):
            errors.append(f'{label}: empty protected mask')
            continue
        if label == 'black_pavement':
            if int(candidate[mask].max()) > 32:
                errors.append('black pavement exceeds 32 RGB8')
        else:
            stats = delta_stats(reference, candidate, mask)
            # Same tolerances as retained sparse-city repeat/locality guards:
            # one RGB8 level at no more than .1% of protected pixels.
            if stats['maximum'] > 1 or stats['changed_fraction'] > .001:
                errors.append(f'{label}: {stats}')
    return errors


def repeat_errors(first, second):
    stats = delta_stats(first, second, np.ones(first.shape[:2], dtype=bool))
    return [] if stats['maximum'] <= 1 and stats['changed_fraction'] <= .001 else [f'repeat: {stats}']


def repeat_consensus_errors(first, second):
    try:
        a, b = median_reference(first), median_reference(second)
        stats = {'median': delta_stats(a, b, np.ones(a.shape[:2], dtype=bool)),
                 'raw_standard': [delta_stats(frame, a, np.ones(a.shape[:2], dtype=bool)) for frame in first],
                 'raw_repeat': [delta_stats(frame, b, np.ones(b.shape[:2], dtype=bool)) for frame in second]}
        return repeat_errors(a, b), stats
    except ValueError as error:
        return [str(error)], {'raw_reference_error': str(error)}


def join_capture_references(primary_metadata, primary_poses, reference_metadata, reference_poses):
    original_hash = primary_metadata.get('executable_sha256', '')
    if not re.fullmatch('[0-9a-fA-F]{64}', original_hash) or original_hash.lower() != reference_metadata.get('executable_sha256', '').lower():
        raise ValueError('Separate repeat references must match the original capture executable SHA256')
    if primary_metadata.get('camera') != reference_metadata.get('camera'):
        raise ValueError('Separate repeat reference camera metadata differs')
    required = {f'{palette}-s{seed}-{stem}-reference-{index}'
                for palette in PALETTES for seed in range(2)
                for stem in ('standard', 'repeat') for index in range(1, 5)}
    if set(reference_poses) != required:
        raise ValueError('Separate repeat reference directory must contain all48 fixed reference poses')
    if set(primary_poses) & set(reference_poses):
        raise ValueError('Repeat reference names overlap original captures')
    return {**primary_poses, **reference_poses}


def smooth_mask(frames, camera):
    height, width = frames[0].shape[:2]
    yy = np.arange(height)[:, None]
    region = np.broadcast_to(yy > height*(camera['horizon_fraction']+.03), (height, width))
    edges = np.zeros((height, width), dtype=bool)
    for frame in frames:
        # Derive eligibility from spatial gradients, never from temporal
        # differences. A flat region-wide flash therefore cannot mask itself.
        vertical = np.any(np.abs(np.diff(frame.astype(np.int16), axis=0)) > 12, axis=2)
        horizontal = np.any(np.abs(np.diff(frame.astype(np.int16), axis=1)) > 12, axis=2)
        edges[:-1] |= vertical
        edges[1:] |= vertical
        edges[:, :-1] |= horizontal
        edges[:, 1:] |= horizontal
    padded = np.pad(edges, 2)
    expanded = np.zeros_like(edges)
    for y in range(5):
        for x in range(5):
            expanded |= padded[y:y+height, x:x+width]
    return region, region & ~expanded


def smooth_step_errors(frames, camera, mask_frames=None):
    region, eligible = smooth_mask(frames if mask_frames is None else mask_frames, camera)
    count, total = int(eligible.sum()), int(region.sum())
    stats = dict(eligible_pixels=count, region_pixels=total,
                 eligible_fraction=count/max(total, 1), steps=[])
    if count < 1000 or count/max(total, 1) < .10:
        return ['Insufficient smooth pixels away from spatial edges'], stats
    errors = []
    for index, (first, second) in enumerate(zip(frames, frames[1:])):
        step = delta_stats(first, second, eligible)
        stats['steps'].append(step)
        # .0002-cell route steps (0.0004 world units) and .0001 transition
        # steps must not flash broad smooth surfaces. Quantization permits a
        # few RGB8 levels; spatial edge exclusion handles moving silhouettes.
        if step['maximum'] > 3 or step['mean'] > .04 or step['p99'] > 1:
            errors.append(f'tiny step {index}: {step}')
    return errors, stats


def rgb(directory, name):
    with Image.open(directory/(name+'.png')) as frame:
        return np.asarray(frame.convert('RGB'), dtype=np.int16)


def metadata(directory):
    result = json.loads((directory/'capture-build.json').read_text(encoding='utf-8-sig'))
    if result.get('completion') is not True:
        raise ValueError(f'Incomplete captures: {directory}')
    frames = result['frames']
    if not frames or len({pose['Name'] for pose in frames}) != len(frames):
        raise ValueError('Missing or duplicate capture poses')
    captures = json.loads((directory/'captures.json').read_text(encoding='utf-8-sig'))
    if frames != captures:
        raise ValueError('Capture manifests disagree')
    return result, {pose['Name']: pose for pose in frames}


def capture_bundle(directory, reference_directory=None):
    primary, poses = metadata(directory)
    locations = {name: directory for name in poses}
    if reference_directory is not None:
        reference, reference_poses = metadata(reference_directory)
        load_camera(reference_directory)
        poses = join_capture_references(primary, poses, reference, reference_poses)
        locations.update({name: reference_directory for name in reference_poses})
    # Every retained reference must reproduce its corresponding full primary
    # pose. No missing-sample fallback, pose substitution or reference cropping.
    for palette in PALETTES:
        for seed in range(2):
            for stem in ('standard', 'repeat'):
                name = f'{palette}-s{seed}-{stem}'
                if name not in poses:
                    raise ValueError(f'Missing primary repeat pose: {name}')
                for index in range(1, 5):
                    reference_name = f'{name}-reference-{index}'
                    if reference_name not in poses:
                        raise ValueError(f'Missing fixed repeat reference pose: {reference_name}')
                    if poses[reference_name] != dict(poses[name], Name=reference_name):
                        raise ValueError(f'Repeat reference full pose differs: {reference_name}')
    return primary, poses, locations


def regional_observation(reference, candidate, camera):
    height = reference.shape[0]
    yy, lateral, _ = ground_coordinates(reference.shape, camera)
    mask = (yy > height*(camera['horizon_fraction']+.08)) & (np.abs(lateral) > 6)
    stats = delta_stats(reference, candidate, mask)
    for label, frame in (('baseline', reference), ('candidate', candidate)):
        colors = frame[mask].astype(np.float64)/255
        linear = np.where(colors <= .04045, colors/12.92, ((colors+.055)/1.055)**2.4)
        luminance = linear @ np.array([.2126, .7152, .0722])
        visible = luminance > .003
        chroma = linear[visible]/np.maximum(linear[visible].sum(axis=1, keepdims=True), 1e-9)
        stats[label] = dict(visible_pixels=int(visible.sum()),
                            mean_linear_luminance=float(luminance.mean()),
                            mean_chromaticity=chroma.mean(axis=0).tolist() if len(chroma) else None,
                            clipped_fraction=float(np.any(frame[mask] >= 255, axis=1).mean()))
    return stats


def collect_regional_observations(poses, camera):
    # Measurements carry no pass/fail color or silhouette threshold. Inspect
    # these alongside actual stills and the production regional/profile probes.
    for name in poses:
        if '-route-' in name or name.endswith(('-standard', '-large', '-small', '-portrait', '-huge-53', '-huge-maximum')):
            stats = regional_observation(rgb(BASELINE, name), rgb(DIRECTORY, name), camera)
            OBSERVATIONS['regional_color'][name] = stats
            print(f'{name}: regional changed={stats["changed_fraction"]:.4f}, mean RGB8 delta={stats["mean"]:.4f}; chroma={stats["candidate"]["mean_chromaticity"]}')


class RegionalLandscapeQuality(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.candidate, cls.poses, cls.locations = capture_bundle(DIRECTORY, REFERENCE_DIRECTORY)
        cls.baseline, cls.baseline_poses, cls.baseline_locations = capture_bundle(BASELINE, BASELINE_REFERENCE_DIRECTORY)
        cls.camera = load_camera(DIRECTORY)

    def test_complete_identical_full_pose_evidence(self):
        self.assertEqual(self.poses, self.baseline_poses, 'Baseline/candidate full poses must match')
        self.assertEqual(self.camera, load_camera(BASELINE))
        for palette_index, palette in enumerate(PALETTES):
            for seed_index, seed in enumerate(SEEDS):
                prefix = f'{palette}-s{seed_index}'
                expected = {'standard': ('0', .25, 1280, 720, 0, 0),
                            'large': ('0', .25, 2560, 1397, 0, 0),
                            'small': ('0', .25, 481, 320, 0, 0),
                            'portrait': ('0', .25, 720, 1280, 0, 0),
                            'repeat': ('0', .25, 1280, 720, 0, 0),
                            'huge-53': ('9007199254740993', .25, 1280, 720, 0, 0),
                            'huge-maximum': ('9223372036854774783', .25, 1280, 720, 0, 0)}
                expected.update({f'route-{cell}': (str(cell), .25, 1280, 720, 0, 0) for cell in (256, 512, 1024)})
                expected.update({f'transition-{t}': ('128', .25, 1280, 720, t, .25) for t in (.4999, .5, .5001)})
                for boundary in (32, 128):
                    expected.update({f'r{boundary}-{suffix}': (str(cell), phase, 1280, 720, 0, 0)
                                     for suffix, cell, phase in (('control-before', boundary-1, .9997),
                                                                ('before', boundary-1, .9999),
                                                                ('after', boundary, .0001),
                                                                ('control-after', boundary, .0003))})
                for suffix, (cell, phase, width, height, transition, light) in expected.items():
                    name = prefix+'-'+suffix
                    with self.subTest(pose=name):
                        self.assertIn(name, self.poses)
                        pose = self.poses[name]
                        self.assertEqual((pose['Seed'], pose['Cell']), (seed, cell))
                        self.assertEqual((pose['Width'], pose['Height'], pose['Palette']), (width, height, palette_index))
                        self.assertEqual((float(pose['Phase']), float(pose['Transition']), float(pose['CityLightPhase'])),
                                         (phase, transition, light))
        for name, pose in self.poses.items():
            with self.subTest(pose=name):
                self.assertIsInstance(pose['Cell'], str)
                self.assertIsInstance(pose['Seed'], str)
                for directory in (self.baseline_locations[name], self.locations[name]):
                    with Image.open(directory/(name+'.png')) as frame:
                        self.assertEqual(frame.size, (pose['Width'], pose['Height']))
                    actual_camera = json.loads((directory/('camera-'+name+'.json')).read_text(encoding='utf-8-sig'))
                    self.assertEqual(actual_camera['camera'], self.camera)
                    self.assertEqual((actual_camera['width'], actual_camera['height']), (pose['Width'], pose['Height']))
        motion = sorted(name for name in self.poses if name.startswith('motion-p0-'))
        if motion:
            self.assertEqual(motion, [f'motion-p0-{i:02d}' for i in range(48)])

    def test_central_atmosphere_black_road_and_road_line_profiles_are_preserved(self):
        for name in self.poses:
            with self.subTest(pose=name):
                reference, candidate = rgb(self.baseline_locations[name], name), rgb(self.locations[name], name)
                self.assertEqual(preservation_errors(reference, candidate, self.camera), [])
                OBSERVATIONS['preservation'][name] = {
                    label: delta_stats(reference, candidate, mask)
                    for label, mask in protected_masks(reference.shape, self.camera).items()}

    def test_repeat_is_deterministic_in_each_palette_and_seed(self):
        for build, locations in (('baseline', self.baseline_locations), ('candidate', self.locations)):
            for palette in PALETTES:
                for seed in range(2):
                    prefix = f'{palette}-s{seed}'
                    first_names = [prefix+'-standard']+[f'{prefix}-standard-reference-{i}' for i in range(1, 5)]
                    second_names = [prefix+'-repeat']+[f'{prefix}-repeat-reference-{i}' for i in range(1, 5)]
                    first = [rgb(locations[name], name) for name in first_names]
                    second = [rgb(locations[name], name) for name in second_names]
                    errors, stats = repeat_consensus_errors(first, second)
                    key = build+'-'+prefix
                    OBSERVATIONS['repeat'][key] = stats
                    print(f'{key}: fixed5 repeat statistics={stats}')
                    with self.subTest(build=build, pose=prefix):
                        self.assertEqual(errors, [])

    def test_tiny_rollover_and_paused_transition_steps_are_smooth(self):
        for palette in PALETTES:
            for seed in range(2):
                prefix = f'{palette}-s{seed}'
                sequences = {f'r{boundary}': [f'{prefix}-r{boundary}-{suffix}' for suffix in
                                             ('control-before', 'before', 'after', 'control-after')]
                             for boundary in (32, 128)}
                sequences['transition'] = [f'{prefix}-transition-{t}' for t in (.4999, .5, .5001)]
                for label, names in sequences.items():
                    reference = [rgb(BASELINE, name) for name in names]
                    candidate = [rgb(DIRECTORY, name) for name in names]
                    # The exact same spatial mask observes both builds; added
                    # terrain edges exclude only their local moving boundary pixels.
                    for build, frames in (('baseline', reference), ('candidate', candidate)):
                        errors, stats = smooth_step_errors(frames, self.camera, reference+candidate)
                        key = prefix+'-'+label+'-'+build
                        OBSERVATIONS['tiny_steps'][key] = stats
                        print(f'{key}: eligible {stats["eligible_pixels"]}/{stats["region_pixels"]}; steps={stats["steps"]}')
                        with self.subTest(sequence=key):
                            self.assertEqual(errors, [])

class HarnessGuardTests(unittest.TestCase):
    def test_separate_reference_build_hash_must_match_original_capture(self):
        with self.assertRaises(ValueError):
            join_capture_references({'executable_sha256': 'a'*64}, {},
                                    {'executable_sha256': 'b'*64}, {})

    def test_repeat_consensus_requires_every_fifth_reference(self):
        frame = np.zeros((100, 100, 3), dtype=np.int16)
        self.assertTrue(repeat_consensus_errors([frame]*4, [frame]*5)[0])

    def test_repeat_consensus_rejects_raw_two_level_outlier_with_equal_medians(self):
        frame = np.zeros((100, 100, 3), dtype=np.int16)
        outlier = frame.copy()
        outlier[20, 30, 0] = 2
        self.assertTrue(repeat_consensus_errors([outlier, frame, frame, frame, frame], [frame]*5)[0])

    def test_repeat_consensus_preserves_changed_pixel_denominator(self):
        frame = np.zeros((100, 100, 3), dtype=np.int16)
        changed = frame.copy()
        changed[0, :11, 0] = 1
        self.assertTrue(repeat_consensus_errors([frame]*5, [changed]*5)[0])

    def test_repeat_consensus_accepts_only_transient_one_level_noise(self):
        frame = np.zeros((100, 100, 3), dtype=np.int16)
        outlier = frame.copy()
        outlier[0, :12, 0] = 1
        errors, stats = repeat_consensus_errors([outlier, frame, frame, frame, frame], [frame]*5)
        self.assertEqual(errors, [])
        self.assertEqual(stats.get('median', {}).get('changed_fraction'), 0)

    def test_road_radiance_regression_is_detected(self):
        baseline = np.zeros((120, 160, 3), dtype=np.int16)
        candidate = baseline.copy()
        candidate[110, 90] = 30
        self.assertTrue(preservation_errors(baseline, candidate, legacy_fixture_camera()))

    def test_protected_upper_atmosphere_regression_is_detected(self):
        baseline = np.zeros((120, 160, 3), dtype=np.int16)
        candidate = baseline.copy()
        candidate[10, 80] = 30
        self.assertTrue(preservation_errors(baseline, candidate, legacy_fixture_camera()))

    def test_intended_high_side_terrain_tint_is_allowed(self):
        baseline = np.zeros((120, 160, 3), dtype=np.int16)
        candidate = baseline.copy()
        # Above the old broad skyline cutoff, but well outside the protected
        # camera-centered atmosphere cone. Tall foreground is allowed here.
        candidate[20:35, 130:155] = 70
        self.assertEqual(preservation_errors(baseline, candidate, legacy_fixture_camera()), [])

    def test_central_protected_solar_and_city_radiance_changes_fail(self):
        baseline = np.zeros((120, 160, 3), dtype=np.int16)
        for row in (20, 27):
            candidate = baseline.copy()
            candidate[row, 80] = 70
            with self.subTest(row=row):
                self.assertTrue(preservation_errors(baseline, candidate, legacy_fixture_camera()))

    def test_side_terrain_accents_are_allowed(self):
        baseline = np.zeros((120, 160, 3), dtype=np.int16)
        candidate = baseline.copy()
        candidate[65:85, :10] = 150
        self.assertEqual(preservation_errors(baseline, candidate, legacy_fixture_camera()), [])

    def test_nondeterministic_repeat_is_detected(self):
        baseline = np.zeros((120, 160, 3), dtype=np.int16)
        candidate = baseline.copy()
        candidate[65, 8] = 30
        self.assertTrue(repeat_errors(baseline, candidate))

    def test_broad_tiny_step_flash_is_detected(self):
        frame = np.zeros((120, 160, 3), dtype=np.int16)
        self.assertTrue(smooth_step_errors([frame, frame, frame+20, frame+20],
                                          legacy_fixture_camera())[0])

    def test_no_useful_smooth_pixels_cannot_pass(self):
        yy, xx = np.indices((120, 160))
        noise = np.repeat((((yy+xx)%2)*200)[..., None], 3, axis=2)
        self.assertTrue(smooth_step_errors([noise]*4, legacy_fixture_camera())[0])

    def test_moving_silhouette_edge_is_excluded_without_hiding_flat_field(self):
        first = np.zeros((120, 160, 3), dtype=np.int16)
        first[:, 15:35] = 150
        second = np.roll(first, 1, axis=1)
        errors, stats = smooth_step_errors([first, first, second, second],
                                          legacy_fixture_camera())
        self.assertEqual(errors, [])
        self.assertGreater(stats.get('eligible_pixels', 0), 1000)
        self.assertLess(stats['eligible_pixels'], stats['region_pixels'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--baseline-directory', type=Path)
    parser.add_argument('--reference-directory', type=Path, help='Separate48 fixed candidate reference poses, with matching executable hash')
    parser.add_argument('--baseline-reference-directory', type=Path, help='Separate48 fixed baseline reference poses, with matching executable hash')
    parser.add_argument('--report', type=Path, help='Retain observational JSON and actual guard outcome')
    args, extra = parser.parse_known_args()
    if not args.self_test and (args.directory is None or args.baseline_directory is None):
        parser.error('native checks require --directory and --baseline-directory')
    DIRECTORY, BASELINE = args.directory, args.baseline_directory
    REFERENCE_DIRECTORY, BASELINE_REFERENCE_DIRECTORY = args.reference_directory, args.baseline_reference_directory
    tests = 'HarnessGuardTests' if args.self_test else 'RegionalLandscapeQuality'
    program = unittest.main(argv=[__file__, tests, *extra], verbosity=2, exit=False)
    if not args.self_test:
        try:
            _, poses = metadata(DIRECTORY)
            collect_regional_observations(poses, load_camera(DIRECTORY))
        except (OSError, ValueError, KeyError) as error:
            OBSERVATIONS['observation_error'] = str(error)
    if args.report:
        OBSERVATIONS['passed'] = program.result.wasSuccessful()
        OBSERVATIONS['failures'] = [str(test)+': '+message for test, message in program.result.failures+program.result.errors]
        OBSERVATIONS['note'] = ('RGB8 observations of actual hidden native captures. Regional hue/terrain accents are '
                                'observational; these guards do not certify terrain geometry, full-address '
                                'field anchoring or GPU profile luminance. Run the existing production/native suites too.')
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(OBSERVATIONS, indent=2)+'\n', encoding='utf-8')
    raise SystemExit(0 if program.result.wasSuccessful() else 1)
