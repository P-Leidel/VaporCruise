"""Native skyline-foot regressions across matched palettes and fog phases.

The low-city crop is centered on the road's distant goal, above the horizon.
It excludes near terrain and measures dark facades, rather than warm windows.
"""
import argparse
import json
from pathlib import Path
import unittest

import numpy as np
from PIL import Image
from camera_projection import load_camera

DIRECTORY = Path()
BASELINE = Path()
PALETTES = ("magenta", "cyan", "amber")
PHASE_SUFFIXES = ("", "-fog-0.25", "-fog-0.5", "-fog-0.75", "-fog-0.95")
BLACK_LIMITS = {"magenta": 23, "cyan": 28, "amber": 22}


def rgb(directory, name):
    with Image.open(directory / (name + ".png")) as frame:
        frame.load()
        return np.asarray(frame.convert("RGB"), dtype=np.int16)


def city_crop(frame, horizon, low, high, half_width=.15):
    h, w = frame.shape[:2]
    return frame[round(h*(horizon-high)):round(h*(horizon-low)),
                 round(w*.5-h*half_width):round(w*.5+h*half_width)]


def mean_longest_run(mask):
    lengths = []
    for row in mask:
        boundaries = np.flatnonzero(np.diff(np.r_[False, row, False]))
        lengths.append(float(np.diff(boundaries.reshape(-1, 2), axis=1).max(initial=0)))
    return float(np.mean(lengths))


class SkylineFuturismQuality(unittest.TestCase):
    def test_low_city_no_longer_forms_the_same_dark_shelf(self):
        horizon = load_camera(DIRECTORY)["horizon_fraction"]
        self.assertEqual(horizon, load_camera(BASELINE)["horizon_fraction"])
        for palette in PALETTES:
            old_runs, new_runs = [], []
            for suffix in PHASE_SUFFIXES:
                name = palette + suffix
                before, after = rgb(BASELINE, name), rgb(DIRECTORY, name)
                old_dark = city_crop(before, horizon, .010, .025).max(axis=2) < BLACK_LIMITS[palette]
                new_dark = city_crop(after, horizon, .010, .025).max(axis=2) < BLACK_LIMITS[palette]
                old_runs.append(mean_longest_run(old_dark))
                new_runs.append(mean_longest_run(new_dark))
                with self.subTest(palette=palette, phase=suffix or "0"):
                    self.assertEqual(before.shape, after.shape)
                    old_low = city_crop(before, horizon, .010, .018).max(axis=2)
                    new_low = city_crop(after, horizon, .010, .018).max(axis=2)
                    lift = float(np.quantile(new_low, .20)-np.quantile(old_low, .20))
                    print(f"{name}: low-facade q20 lift={lift:.2f} RGB8, "
                          f"dark foot fraction={old_dark.mean():.3f}->{new_dark.mean():.3f}")
                    self.assertGreaterEqual(lift, 2,
                        "the darkest low facades must visibly settle into shared distant mist")
                    self.assertLess(float(new_dark.mean()), float(old_dark.mean()),
                        "the connected black shelf must lose dark coverage at every fog phase")
            print(f"{palette}: mean longest dark run {np.mean(old_runs):.2f} -> {np.mean(new_runs):.2f}px")
            with self.subTest(palette=palette, metric="dark runs"):
                self.assertLess(float(np.mean(new_runs)), float(np.mean(old_runs))*.85,
                    "the city foot must break up long connected dark runs, not only brighten windows")

    def test_black_upper_facades_keep_contrast(self):
        # Fixed-seed 1280x720 facade core above the foot taper, in front of the
        # lowest bright sun stripe. Both authored coverage masks are fully
        # opaque in its conservative 5x5-texel sampling neighborhood, with
        # steady/animated emission below 3/255. A broad skyline crop would
        # confuse newly opened crown gaps with a brighter building material.
        for palette in PALETTES:
            for suffix in PHASE_SUFFIXES:
                with self.subTest(palette=palette, phase=suffix or "0"):
                    before, after = rgb(BASELINE, palette+suffix), rgb(DIRECTORY, palette+suffix)
                    self.assertEqual(before.shape, (720, 1280, 3))
                    self.assertEqual(after.shape, before.shape)
                    old_facade, new_facade = before[208:212, 664:667], after[208:212, 664:667]
                    self.assertLessEqual(int(np.abs(new_facade-old_facade).max()), 1,
                        "opaque higher facades must keep their existing material and fog contrast")
                    self.assertLess(int(new_facade.max()), 40,
                        "the opaque facade core must remain black against the bright sun stripe")

    def test_horizon_is_continuous_at_all_captured_sizes(self):
        horizon = load_camera(DIRECTORY)["horizon_fraction"]
        metadata = json.loads((DIRECTORY/"capture-build.json").read_text(encoding="utf-8-sig"))
        self.assertIs(metadata.get("completion"), True)
        expected = {palette+suffix for palette in PALETTES for suffix in
                    (*PHASE_SUFFIXES, "-large", "-small", "-late-fog-small", "-portrait")}
        expected.update(("tall", "minimum-aspect", "ultrawide", "superwide"))
        self.assertTrue(expected.issubset({pose["Name"] for pose in metadata["frames"]}),
                        "the full palette, phase and supported-aspect matrix is required")
        for pose in metadata["frames"]:
            name = pose["Name"]
            frame = rgb(DIRECTORY, name)
            h, w = frame.shape[:2]
            with self.subTest(pose=name):
                self.assertEqual((w, h), (pose["Width"], pose["Height"]))
                boundary = int(np.ceil(h*horizon-.5))
                profile = frame[boundary-1:boundary+1, round(w*.48):round(w*.52)].mean(axis=1)
                self.assertLessEqual(float(np.abs(profile[1]-profile[0]).max()), 3,
                    "sky, city foot and distant ground must share the continuous horizon color")

    def test_sky_and_foreground_remain_stable(self):
        for palette in PALETTES:
            before, after = rgb(BASELINE, palette), rgb(DIRECTORY, palette)
            h = after.shape[0]
            for label, difference in (("upper sky", np.abs(before[:round(h*.13)]-after[:round(h*.13)])),
                                      ("foreground", np.abs(before[round(h*.65):]-after[round(h*.65):]))):
                with self.subTest(palette=palette, region=label):
                    self.assertLessEqual(int(difference.max()), 1)
                    self.assertLessEqual(float(np.any(difference, axis=2).mean()), .001)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--baseline-directory", type=Path, required=True)
    args, extra = parser.parse_known_args()
    DIRECTORY, BASELINE = args.directory, args.baseline_directory
    unittest.main(argv=[__file__, *extra], verbosity=2)
