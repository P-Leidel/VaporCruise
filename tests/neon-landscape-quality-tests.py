"""Check actual GPU output for localized glow and animated horizon atmosphere.

The fixed shoulder samples use the flat road corridor, independently of the
terrain composition. Fog phases keep every other capture input identical.
"""
import argparse
from pathlib import Path
import unittest
import numpy as np
from PIL import Image
from camera_projection import load_camera, project_world

DIRECTORY = Path()
BASELINE = None
PALETTES = ('magenta', 'cyan', 'amber')


def rgb(directory, name):
    with Image.open(directory / (name + '.png')) as frame:
        return np.asarray(frame.convert('RGB'), dtype=np.float32)


def shoulder_halo(directory, name):
    frame = rgb(directory, name)
    camera = load_camera(directory)
    height, width = frame.shape[:2]
    samples = []
    for y in range(round(height * .50), round(height * .59)):
        depth = camera['height'] * height * camera['focal_scale'] / (
            y + .5 - height * camera['horizon_fraction'])
        # x = -4 is a longitudinal wire outside the road on the flat shoulder.
        x, _, _ = project_world(camera, width, height, -4, 0, depth)
        x = round(x)
        strip = frame[y, x-12:x+13].max(axis=1)
        # Avoid crossings with moving horizontal wires. The floor beyond the
        # local halo is otherwise almost black in this protected corridor.
        if len(strip) == 25 and max(strip[0], strip[-1]) < 35:
            samples.append(float(np.mean(strip[[8, 9, 15, 16]])))
    if len(samples) < 6:
        raise AssertionError('not enough unobscured shoulder halo samples')
    return float(np.median(samples))


class NeonLandscapeQuality(unittest.TestCase):
    def test_fog_moves_over_distant_grid_in_every_palette(self):
        for name in PALETTES:
            a, b = rgb(DIRECTORY, name), rgb(DIRECTORY, name + '-fog-quarter')
            h, w = a.shape[:2]
            difference = np.abs(a-b).max(axis=2)
            region = difference[round(h*.34):round(h*.43)]
            # Static city-window animation cannot satisfy this ground region.
            with self.subTest(palette=name):
                changed = float((region > 1).mean())
                print(f'{name} moving ground mist: {changed:.3f} changed, mean {region.mean():.3f}')
                self.assertGreater(changed, .06, 'fog must drift over the distant grid')
                self.assertGreater(float(region.mean()), .30)

    def test_fog_keeps_sky_outside_sun_and_foreground_stable(self):
        for name in PALETTES:
            a, b = rgb(DIRECTORY, name), rgb(DIRECTORY, name + '-fog-quarter')
            h,w = a.shape[:2]
            horizon=load_camera(DIRECTORY)['horizon_fraction']
            yy,xx=np.indices((round(h*.15),w))
            # Shared atmosphere time also drives the local sun. The rest of
            # the sky must remain stationary rather than breathing globally.
            outside_sun=((xx+.5-w*.5)/h)**2+((yy+.5)/h-(horizon-.16))**2>.26**2
            sky_delta=np.abs(a[:outside_sun.shape[0]]-b[:outside_sun.shape[0]])[outside_sun]
            for label, region in [('sky outside sun', sky_delta),
                                  ('foreground', np.abs(a[round(h*.74):]-b[round(h*.74):]))]:
                with self.subTest(palette=name, region=label):
                    self.assertLessEqual(float(region.max()), 1)
                    self.assertLess(float((region > 0).mean()), .001)

    def test_fog_loop_wrap_is_continuous(self):
        frames = [rgb(DIRECTORY, name) for name in
                  ('fog-9998', 'fog-9999', 'fog-0001', 'fog-0002')]
        differences = [float(np.abs(a-b).mean()) for a,b in zip(frames, frames[1:])]
        self.assertLessEqual(differences[1], max(differences[0],differences[2])*2+.03)

    def test_grid_has_a_visible_local_halo_in_every_palette(self):
        for name in PALETTES:
            halo = shoulder_halo(DIRECTORY, name)
            with self.subTest(palette=name):
                print(f'{name} shoulder halo: {halo:.2f} RGB8')
                self.assertGreater(halo, 15, 'wires need visible surrounding glow')
                if BASELINE:
                    before = shoulder_halo(BASELINE, name)
                    self.assertGreater(halo, before*1.25, 'glow should improve over the reviewed scene')

    def test_repeat_capture_is_stable(self):
        difference = np.abs(rgb(DIRECTORY, 'magenta')-rgb(DIRECTORY, 'repeat'))
        self.assertLessEqual(float(difference.max()), 1)
        self.assertLess(float((difference > 0).mean()), .001)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--baseline-directory', type=Path)
    args, extra = parser.parse_known_args()
    DIRECTORY, BASELINE = args.directory, args.baseline_directory
    unittest.main(argv=[__file__, *extra], verbosity=2)
