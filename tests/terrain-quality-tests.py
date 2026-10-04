"""Check actual GPU output, with road/terrain criteria independent of shader code."""
import argparse
from pathlib import Path
import sys
import unittest
import json
import numpy as np
from PIL import Image
sys.dont_write_bytecode = True
DIRECTORY = Path()
SMOKE = False
HORIZON = 1/3

def rgb(name):
    with Image.open(DIRECTORY / name) as frame:
        return np.asarray(frame.convert('RGB')).astype(np.float32)

class MotionSequenceQuality(unittest.TestCase):
    def test_exact_consecutive_pose_manifest(self):
        manifests = sorted(DIRECTORY.glob('motion-p*.json'))
        self.assertTrue(manifests, 'motion pose manifests are required')
        for path in manifests:
            records = json.loads(path.read_text(encoding='utf-8-sig'))
            self.assertGreaterEqual(len(records), 24)
            for a,b in zip(records,records[1:]):
                self.assertIsInstance(a['Cell'], str)
                self.assertIsInstance(a['Seed'], str)
                delta = int(b['Cell'])-int(a['Cell']) + float(b['Phase'])-float(a['Phase'])
                self.assertGreater(delta, 0)
                self.assertLessEqual(delta, 1)

    def test_motion_and_sky(self):
        for manifest in sorted(DIRECTORY.glob('motion-p*.json')):
            records = json.loads(manifest.read_text(encoding='utf-8-sig'))
            a,b = [rgb(r['Name']+'.png') for r in records[:2]]
            h,w = a.shape[:2]
            self.assertGreater(float(np.abs(a-b)[round(h*.5):round(h*.9)].mean()), .1)
            for record in records[1:]:
                frame = rgb(record['Name']+'.png')
                np.testing.assert_array_equal(a[:round(h*.1),round(w*.45):round(w*.55)],
                                              frame[:round(h*.1),round(w*.45):round(w*.55)])

class TerrainQuality(unittest.TestCase):
    def test_grid_reaches_marked_horizon_distance(self):
        frame = rgb('magenta.png'); h,w = frame.shape[:2]
        # User's red line is about 24 px below the horizon in a 720 px view.
        # World x=4 is a grid ray; x=5 is between rays on the flat shoulder.
        rows = np.arange(round(h*(HORIZON+.032)), round(h*(HORIZON+.038)))
        contrasts = []
        for y in rows:
            ray = round(w*.5 + 4/3.2*(y-h*HORIZON))
            gap = round(w*.5 + 5/3.2*(y-h*HORIZON))
            contrasts.append(frame[y,ray-1:ray+2].max() - frame[y,gap].max())
        self.assertGreater(float(np.median(contrasts)), 8,
                           'grid rays must remain distinct up to the marked line')

    def test_distant_faces_remain_readable_silhouettes(self):
        frame = rgb('magenta.png'); h,w = frame.shape[:2]
        # Interior of the left distant peak on the fixed capture seed. Examine
        # the face between wires rather than its bright grid intersections.
        x,y = round(w*24/1280), round(h*(HORIZON-35.6/720))
        face = frame[y-2:y+3,x-2:x+3].max(axis=2)
        backdrop = frame[y-2:y+3,round(w*110/1280)-2:round(w*110/1280)+3].max(axis=2)
        self.assertGreater(float(np.median(backdrop)-np.median(face)), 12,
                           'the distant formation should remain legible against its backdrop')

    def test_ground_blends_into_horizon(self):
        for name in (('magenta',) if SMOKE else ('magenta', 'cyan', 'amber')):
            frame = rgb(name + '.png'); h,w = frame.shape[:2]
            # Follow the flat shoulder, outside the road and inside all rocks.
            rows = np.arange(round(h*(HORIZON+.04)), round(h*(HORIZON+.08)))
            columns = np.rint(w*.5 - 4/3.2*(rows-h*HORIZON)).astype(int)
            # A mean can hide a black trough between bright grid intersections.
            self.assertGreater(float(np.quantile(frame[rows,columns].max(axis=1), .1)), 35,
                               'unresolved grid must blend into haze, not a black shelf')

    def test_continuous_world_manifest(self):
        if SMOKE: self.skipTest('smoke uses the fixed GPU pose')
        manifest = DIRECTORY / 'captures.json'
        self.assertTrue(manifest.is_file(), 'captures must record seed and exact integer world poses')
        records = json.loads(manifest.read_text(encoding='utf-8-sig'))
        before = next(r for r in records if r['Name'] == 'seam-before')
        after = next(r for r in records if r['Name'] == 'seam-after')
        self.assertIsInstance(before['Cell'], str)
        self.assertIsInstance(before['Seed'], str)
        self.assertEqual(int(after['Cell']), int(before['Cell']) + 1)
        self.assertAlmostEqual(float(before['Phase']), .9999)
        self.assertAlmostEqual(float(after['Phase']), .0001)

    def test_adjacent_world_wrap(self):
        if SMOKE: self.skipTest('full matrix only')
        frames = [rgb(n + '.png') for n in ('seam-control-before','seam-before','seam-after','seam-control-after')]
        differences = [np.abs(b-a) for a,b in zip(frames,frames[1:])]
        controls = max(float(d.mean()) for d in (differences[0],differences[2]))
        seam = float(differences[1].mean())
        print(f'  seam mean={seam:.5f}, neighboring tiny steps={controls:.5f}')
        self.assertLessEqual(seam, controls*2 + .03)
        self.assertLessEqual(float((differences[1]>20).mean()), .001)

    def test_different_world_sections(self):
        if SMOKE: self.skipTest('full matrix only')
        frames = [rgb(n + '.png') for n in ('magenta','world-320','world-3200')]
        for a,b in zip(frames,frames[1:]):
            h,w = a.shape[:2]
            sides = np.abs(a-b)[round(h*.25):round(h*.9)]
            sides[:,round(w*.35):round(w*.65)] = 0
            # Sparse rocks occupy little image area; measure changed formations
            # instead of requiring broad landscape changes across a flat plain.
            self.assertGreater(int((sides.max(axis=2)>40).sum()), 100, 'formations vary over the route')

    def test_forward_marking_motion(self):
        if SMOKE: self.skipTest('full matrix only')
        for palette in range(3):
            a,b = [rgb(f'motion-p{palette}-{i:02}.png') for i in (0,1)]
            h,w = a.shape[:2]
            def boundaries(frame):
                column = frame[:,w//2].max(axis=1)
                return np.flatnonzero((column[1:] > 60) != (column[:-1] > 60))+1
            first = boundaries(a)
            first = first[(first>h*.6)&(first<h*.85)]
            self.assertGreater(len(first), 0)
            second = boundaries(b)
            y = first[-1]; near = second[np.abs(second-y).argmin()]
            self.assertGreater(near, y, f'palette {palette}: dash boundary approaches viewer')

    def test_slope_motion_and_unobstructed_sky(self):
        if SMOKE: self.skipTest('full matrix only')
        for palette in range(3):
            frames = [rgb(f'motion-p{palette}-{i:02}.png') for i in range(24)]
            # Central upper sky is outside the inspected mountain silhouettes.
            h,w = frames[0].shape[:2]
            sky = frames[0][:round(h*.12),round(w*.4):round(w*.6)]
            for frame in frames[1:]:
                np.testing.assert_array_equal(sky,frame[:round(h*.12),round(w*.4):round(w*.6)])
            values = np.stack(frames)[:,round(h*.45):round(h*.85),:round(w*.22)]
            difference = np.diff(values,axis=0)
            self.assertGreater(float(np.sqrt(np.mean(difference**2))), 1, 'slope grids visibly move')

    def test_projection_across_shapes(self):
        for path in sorted(DIRECTORY.glob('aspect-*.png')):
            frame = rgb(path.name); h, w = frame.shape[:2]
            # Above the dashboard, both edges meet the same straight projection.
            # In portrait, the wider foreground road exits the side of the
            # viewport; inspect a row where both projected edges are visible.
            y = round(h*min(.65, HORIZON + w/h*.35))
            radius = 3/3.2 * (y-h*HORIZON)
            for x in (w*.5-radius, w*.5+radius):
                self.assertGreater(float(frame[y, round(x)-3:round(x)+4].max()), 60, path.name)

    def test_road_markings_and_symmetry(self):
        frame = rgb('magenta.png')
        h, w = frame.shape[:2]
        y = round(h*.82)
        # Flat pavement projection: x = center +/- (3/3.2)*(y - horizon).
        radius = 3/3.2 * (y - h*HORIZON)
        for x in (w*.5-radius, w*.5+radius):
            self.assertGreater(float(frame[y, round(x)-3:round(x)+4].max()), 80)
        center = frame[round(h*.52):round(h*.89), round(w*.5)]
        self.assertGreater(int((center.max(axis=1) > 80).sum()), 20, 'center dashes are visible')
        self.assertGreater(int((center.max(axis=1) < 20).sum()), 20, 'dashes have dark gaps')

    def test_hints_survive_terrain(self):
        plain, hint = rgb('magenta.png'), rgb('hint.png')
        changed = np.any(plain != hint, axis=2)
        self.assertGreater(int(changed.sum()), 1000)
        # Existing fixed-DPI hint box; terrain and sky outside it are identical.
        h, w = changed.shape
        self.assertFalse(changed[:h-122].any())
        self.assertFalse(changed[:, :int(w/2)-265].any())
        self.assertFalse(changed[:, int(w/2)+265:].any())
        self.assertGreater(float(hint[changed].max(axis=1).max()), 180)

    def test_asymmetric_mountains(self):
        frame = rgb('magenta.png')
        h, w = frame.shape[:2]
        # Above the road/rocks, the symmetric sky should contain visible,
        # independently placed distant silhouettes on this fixed seed.
        left = frame[round(h*(HORIZON-.13)):round(h*(HORIZON-.01)), :w//3]
        right = frame[round(h*(HORIZON-.13)):round(h*(HORIZON-.01)), -(w//3):][:, ::-1]
        self.assertGreater(int((np.abs(left-right).max(axis=2)>12).sum()), 500,
                           'distant mountains must remain visible above the horizon')

    def test_dark_pavement(self):
        frame = rgb('magenta.png')
        h, w = frame.shape[:2]
        lane = frame[round(h*.80):round(h*.89), round(w*.40):round(w*.44)]
        self.assertLess(float(lane.max(axis=2).mean()), 12, 'unmarked pavement must be dark')

    def test_open_sky_without_tunnel_walls(self):
        frame = rgb('magenta.png')
        h, w = frame.shape[:2]
        sides = np.concatenate((frame[round(h*.01):round(h*(HORIZON-.03)), :round(w*.30)],
                                frame[round(h*.01):round(h*(HORIZON-.03)), round(w*.70):]), axis=1)
        lit = (sides[:,:,0] > 100) & (sides[:,:,0] > sides[:,:,1]*1.4) & (sides[:,:,2] > sides[:,:,1]*1.4)
        self.assertLess(float(lit.mean()), .003, 'small formations leave open sky instead of tall side walls')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--motion-only', action='store_true')
    args = parser.parse_args()
    DIRECTORY = args.directory
    SMOKE = args.smoke
    target = MotionSequenceQuality if args.motion_only else TerrainQuality
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(target))
    raise SystemExit(0 if result.wasSuccessful() else 1)
