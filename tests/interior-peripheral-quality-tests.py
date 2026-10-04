"""Historical checker for archived car-interior peripheral material evidence.

Retired when car interiors and their capture options were removed. Current
visual verification rejects this suite; the source remains for interpreting
archived baseline/candidate captures.

The supplied capture matrix includes unchanged cabin poses over two distinct
world poses. These image checks catch center distortion, repeated edge columns,
and world transmission through bodywork; they do not implement the shader's
material or alpha masks. Source image dimensions only locate the original art
rectangle. Inspect the full images as well as these bounded checks.

The original 27-pose wide-interior capture profile has been retired. This
historical checker accepts archived captures directly with --baseline
BASELINE_DIRECTORY --directory CANDIDATE_DIRECTORY.
"""
import argparse
import json
from pathlib import Path
import unittest

import numpy as np
from PIL import Image

BASELINE = Path()
DIRECTORY = Path()
ARTWORK_ASPECT = 1672 / 941
SIZES = {"standard": (1280, 720), "ultrawide": (2560, 1080), "superwide": (5120, 1440)}


def records(directory):
    metadata = json.loads((directory / "capture-build.json").read_text(encoding="utf-8-sig"))
    if metadata.get("completion") is not True:
        raise ValueError(f"{directory}: capture evidence is incomplete")
    executable_hash = metadata.get("executable_sha256", metadata.get("Sha256"))
    if not isinstance(executable_hash, str) or len(executable_hash) != 64:
        raise ValueError(f"{directory}: missing captured executable hash")
    values = json.loads((directory / "captures.json").read_text(encoding="utf-8-sig"))
    if not isinstance(values, list):
        raise ValueError(f"{directory}: expected pose list")
    result = {}
    for value in values:
        name = value.get("Name")
        if not isinstance(name, str) or name in result:
            raise ValueError(f"{directory}: missing/duplicate pose name")
        result[name] = value
    return result


def rgb(directory, name):
    with Image.open(directory / (name + ".png")) as image:
        image.load()
        return np.asarray(image.convert("RGB"), dtype=np.int16)


def crop(frame, x0, x1, y0, y1):
    h, w = frame.shape[:2]
    return frame[round(h*y0):round(h*y1), round(w*x0):round(w*x1)]


def peripheral(frame, side, y0, y1):
    h, w = frame.shape[:2]
    # The authored rectangle stays at its natural aspect, centered in the wide
    # image. Leave 40 px beside the join and 8 px at the outer screenshot edge.
    border = round((w - h*ARTWORK_ASPECT) / 2)
    if side == "left":
        x0, x1 = 8, border - 40
    else:
        x0, x1 = w - border + 40, w - 8
    if x1-x0 < 32:
        raise ValueError("peripheral capture is too narrow for a material comparison")
    return frame[round(h*y0):round(h*y1), x0:x1]


def horizontal_range(frame):
    # Average 8-pixel columns to ignore the existing one-level output dither.
    # Repeated source columns have virtually no horizontal variation even if
    # they contain strong vertical texture. Broad panels/seams must vary in X.
    h, w, channels = frame.shape
    blocks = frame[:, :w//8*8].reshape(h, w//8, 8, channels).mean(axis=2)
    return float((np.quantile(blocks, .9, axis=1) - np.quantile(blocks, .1, axis=1)).mean())


class PeripheralMaterials(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before = records(BASELINE)
        cls.after = records(DIRECTORY)

    def assert_resolve_equal(self, first, second, message):
        self.assertEqual(first.shape, second.shape, message)
        delta = np.abs(first-second)
        self.assertLessEqual(int(delta.max()), 1, message)
        self.assertLessEqual(float(np.count_nonzero(delta.max(axis=2))/delta.shape[0]/delta.shape[1]),
                             .001, message)

    def test_complete_matching_real_capture_poses(self):
        for car in range(3):
            for shape, (width, height) in SIZES.items():
                suffixes = ("",) if shape == "standard" else ("", "-world", "-motion", "-motion-world")
                for suffix in suffixes:
                    name = f"car-{car}-{shape}{suffix}"
                    with self.subTest(name=name):
                        self.assertIn(name, self.before)
                        self.assertIn(name, self.after)
                        self.assertEqual(self.before[name], self.after[name], "baseline/candidate pose changed")
                        self.assertEqual((self.after[name]["Width"], self.after[name]["Height"]), (width, height))
                        self.assertEqual(int(self.after[name]["Interior"]), car)
                        self.assertEqual(float(self.after[name]["Hint"]), 0, "hints would conceal cabin evidence")
                        self.assertEqual(float(self.after[name]["InteriorTransition"]), 0, "settled-car evidence required")
                        for directory in (BASELINE, DIRECTORY):
                            self.assertEqual(rgb(directory, name).shape, (height, width, 3))

    def test_original_wheel_and_dashboard_preserve_their_appearance(self):
        # Hand-inspected opaque wheel/instrument/dashboard regions in all
        # selected cars; changing terrain behind the windshield cannot affect
        # these crops. A stretched art transform or new tint must fail here.
        for car in range(3):
            name = f"car-{car}-standard"
            before, after = rgb(BASELINE, name), rgb(DIRECTORY, name)
            with self.subTest(car=car):
                self.assert_resolve_equal(crop(before, .15, .42, .84, .98), crop(after, .15, .42, .84, .98),
                                          "original wheel/instruments changed")
                self.assert_resolve_equal(crop(before, .46, .88, .84, .98), crop(after, .46, .88, .84, .98),
                                          "original passenger dashboard changed")

    def test_materials_no_longer_repeat_a_single_edge_column(self):
        for car in range(3):
            bands = ((.08, .20), (.86, .95)) if car == 0 else (((.86, .95),) if car == 1 else ((.12, .25), (.48, .64), (.86, .95)))
            for shape in ("ultrawide", "superwide"):
                for motion in ("", "-motion"):
                    name = f"car-{car}-{shape}{motion}"
                    before, after = rgb(BASELINE, name), rgb(DIRECTORY, name)
                    for side in ("left", "right"):
                        for y0, y1 in bands:
                            with self.subTest(car=car, shape=shape, motion=motion, side=side, band=(y0, y1)):
                                old_range = horizontal_range(peripheral(before, side, y0, y1))
                                new_range = horizontal_range(peripheral(after, side, y0, y1))
                                print(f"{name} {side} y={y0}..{y1}: horizontal material range {old_range:.3f}->{new_range:.3f}")
                                self.assertGreater(new_range, 2, "peripheral material still copies an edge column")
                                self.assertGreater(new_range, old_range + 1, "peripheral presentation did not improve")

    def test_opaque_bodywork_is_independent_of_the_world_behind_it(self):
        for car in range(3):
            for shape in ("ultrawide", "superwide"):
                for motion in ("", "-motion"):
                    name = f"car-{car}-{shape}{motion}"
                    first, second = rgb(DIRECTORY, name), rgb(DIRECTORY, name + "-world")
                    pose, world_pose = self.after[name], self.after[name + "-world"]
                    with self.subTest(car=car, shape=shape, motion=motion):
                        for field in ("Interior", "Palette", "Hint", "InteriorTransition", "CabinSeconds"):
                            self.assertEqual(pose[field], world_pose[field], f"world control changed cabin field {field}")
                        self.assertNotEqual((pose["Seed"], pose["Cell"], pose["Phase"]),
                                            (world_pose["Seed"], world_pose["Cell"], world_pose["Phase"]))
                        # Verify a real visible world change before interpreting
                        # equal body pixels as an opacity result. The center
                        # windshield and road are open in every approved car.
                        control_delta = np.abs(crop(first, .38, .62, .40, .65)-crop(second, .38, .62, .40, .65))
                        self.assertGreater(int((control_delta.max(axis=2)>20).sum()), 100,
                                           "world control did not visibly change; opacity evidence is uninformative")
                        bands = ((.40, .64), (.86, .95)) if car == 2 else ((.86, .95),)
                        for side in ("left", "right"):
                            for y0, y1 in bands:
                                self.assert_resolve_equal(peripheral(first, side, y0, y1), peripheral(second, side, y0, y1),
                                                          f"{side} bodywork transmits changing terrain at y={y0}..{y1}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    args, extra = parser.parse_known_args()
    BASELINE, DIRECTORY = args.baseline, args.directory
    unittest.main(argv=[__file__, *extra], verbosity=2)
