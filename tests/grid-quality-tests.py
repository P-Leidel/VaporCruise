from camera_projection import load_camera as _load_camera, legacy_fixture_camera
"""Quality gates on actual GPU PNGs, independent of shader implementation.

Capture the complete matrix with tools/capture-visuals.ps1 -Motion, then run:
    python tests/grid-quality-tests.py --directory build/visual/horizon-after

Run the same command on before/after directories to compare the printed metrics.
The tests deliberately fail on a missing or partial capture matrix. Values are
display-referred RGB8 levels. Motion uses the brightest channel to cover all
three neon hues; the horizon join checks every color channel.
"""

import argparse
import sys
_LEGACY_CAMERA = "--legacy-camera" in sys.argv
if _LEGACY_CAMERA: sys.argv.remove("--legacy-camera")
def load_camera(directory):
    return legacy_fixture_camera() if _LEGACY_CAMERA else _load_camera(directory)
from pathlib import Path
import re
import sys
import unittest

import numpy as np
from PIL import Image

sys.dont_write_bytecode = True

PALETTES = ("magenta", "cyan", "amber")
STATIC_FRAMES = (*PALETTES, "small", "ultrawide", "portrait")
DIRECTORY = Path("build/visual/horizon-after")
EXTRA_MOTION_DIRECTORIES = []


def read_rgb(path):
    with Image.open(path) as image:
        return np.asarray(image.convert("RGB"))


def patch(frames, top, bottom, left=0.0, right=1.0):
    """Crop normalized image coordinates, retaining the phase dimension."""
    height, width = frames.shape[-2:]
    return frames[..., round(height * top):round(height * bottom),
                  round(width * left):round(width * right)]


def boundary_jump(rgb):
    # Average across the width to isolate the full-width plane boundary from
    # individual grid lines. The landscape meets the ground at 48% height.
    height = rgb.shape[0]
    profile = rgb.astype(np.float32).mean(axis=1)
    band = profile[round(height * 0.474):round(height * 0.490)]
    return float(np.abs(np.diff(band, axis=0)).max())


def compressed_patch(frames, top, bottom, left, right):
    # In perspective, projected row spacing grows with image height times the
    # square of distance from the vanishing point. Scale horizon-relative patch
    # distance so different resolutions measure equivalent pixel crowding. This
    # keeps resolved higher-resolution lines out of an unresolved-grid gate.
    scale = (360.0 / frames.shape[-2]) ** 0.5
    horizon=load_camera(DIRECTORY)["horizon_fraction"]+(0.48-1/3)
    top = horizon + (top - 0.48) * scale
    bottom = horizon + (bottom - 0.48) * scale
    return patch(frames, top, bottom, left, right)


def temporal_contrast(frames, left, right):
    # The dense 52–56% reference band should converge to a stable pixel average
    # across a complete cell period. Exclude the already static far background,
    # which otherwise dilutes p95. This measures phase variation in a deliberately
    # quiet compressed region, not live presentation cadence or alias frequency.
    dense = compressed_patch(frames, 0.52, 0.56, left, right)
    return float(np.quantile(dense.std(axis=0), 0.95))


def stationary_edge_ringing(frames, left, right):
    # Averaging a full grid-cell cycle removes travelling horizontal rows. The
    # remaining column peaks identify stationary oblique stripes. Only visible
    # peaks repeating in fewer than four pixels are crowded; individually resolved
    # rays are allowed, including the steeper rays of a portrait projection.
    average = frames.mean(axis=0)
    dense = compressed_patch(average, 0.56, 0.62, left, right)
    center = dense[2:-2]
    valley = np.minimum.reduce((dense[:-4], dense[1:-3], dense[3:-1], dense[4:]))
    peaks = ((center > dense[1:-3]) & (center >= dense[3:-1])
             & (center - valley >= 4.0))  # Ignore one-level quantization/dither.
    crowded = np.zeros(peaks.shape, dtype=bool)
    for distance in (2, 3):
        pairs = peaks[:-distance] & peaks[distance:]
        for offset in range(distance + 1):
            crowded[offset:offset + len(pairs)] |= pairs
    count = int(crowded.sum())
    if not count:
        return 0.0, 0  # No visible stripes repeating at a crowded pixel spacing.
    ringing = np.abs(np.diff(dense, n=2, axis=0))[1:-1]
    return float(np.quantile(ringing[crowded], 0.95)), count


def load_motion(paths, expected_dimensions=None):
    frames = []
    first_sky = None
    first_dimensions = None
    maximum_sky_change = 0
    for path in paths:
        rgb = read_rgb(path)
        dimensions = (rgb.shape[1], rgb.shape[0])
        if first_dimensions is None:
            first_dimensions = dimensions
        if dimensions != first_dimensions:
            raise AssertionError(f"{path} dimensions {dimensions} differ from "
                                 f"the first phase {first_dimensions}")
        if expected_dimensions and dimensions != expected_dimensions:
            raise AssertionError(f"{path} dimensions {dimensions}, "
                                 f"expected {expected_dimensions}")
        sky = rgb[:round(rgb.shape[0] * 0.42)].astype(np.int16)
        if first_sky is None:
            first_sky = sky
        maximum_sky_change = max(
            maximum_sky_change, int(np.abs(sky - first_sky).max()))
        frames.append(rgb.max(axis=2).astype(np.float32))
    return np.stack(frames), maximum_sky_change


def extra_sequences(directory):
    palettes = {}
    for path in directory.glob("motion-p*.png"):
        match = re.fullmatch(r"motion-p([0-2])-(\d+)\.png", path.name)
        if not match:
            raise AssertionError(f"Unrecognized motion capture name: {path}")
        palette, index = map(int, match.groups())
        palettes.setdefault(palette, []).append((index, path))
    if not palettes:
        raise AssertionError(f"No motion-pN-XX.png phase sequences in {directory}")
    for palette, indexed in sorted(palettes.items()):
        indexed.sort()
        indexes = [index for index, _ in indexed]
        if len(indexed) < 24 or indexes != list(range(len(indexed))):
            raise AssertionError(
                f"{directory}, palette {palette}: need at least 24 contiguous "
                f"phases starting at 00; found {indexes}")
        yield f"{directory.name}/{PALETTES[palette]}", [path for _, path in indexed]


class RenderedGridQualityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        required = [f"{name}.png" for name in STATIC_FRAMES]
        required += ["repeat.png", "seam-before.png", "seam-after.png"]
        required += [f"motion-p{palette}-{frame:02}.png"
                     for palette in range(3) for frame in range(24)]
        missing = [name for name in required if not (DIRECTORY / name).is_file()]
        if missing:
            raise AssertionError(
                f"Incomplete GPU capture matrix in {DIRECTORY}: "
                + ", ".join(missing)
                + ". Run tools/capture-visuals.ps1 -Motion first.")

        cls.static = {name: read_rgb(DIRECTORY / f"{name}.png")
                      for name in STATIC_FRAMES}
        cls.motion = {}
        cls.sky_change = {}
        for palette, name in enumerate(PALETTES):
            paths = [DIRECTORY / f"motion-p{palette}-{frame:02}.png"
                     for frame in range(24)]
            cls.motion[name], cls.sky_change[name] = load_motion(paths, (640, 360))
        for directory in EXTRA_MOTION_DIRECTORIES:
            for name, paths in extra_sequences(directory):
                if name in cls.motion:
                    raise AssertionError(f"Duplicate motion sequence label: {name}")
                cls.motion[name], cls.sky_change[name] = load_motion(paths)

        print(f"\nGPU grid quality metrics: {DIRECTORY}")
        for name in STATIC_FRAMES:
            print(f"  {name}: horizon boundary jump="
                  f"{boundary_jump(cls.static[name]):.3f}")
        for name, frames in cls.motion.items():
            foreground = patch(frames, 0.78, 0.95)
            motion = float(np.sqrt(np.mean(
                (foreground - np.roll(foreground, 1, axis=0)) ** 2)))
            contrast = float(foreground.std(axis=(1, 2)).mean())
            left_ringing, left_crowded = stationary_edge_ringing(frames, 0.0, 0.18)
            right_ringing, right_crowded = stationary_edge_ringing(frames, 0.82, 1.0)
            print(f"  {name} ({frames.shape[2]}x{frames.shape[1]}, "
                  f"{len(frames)} phases): dense phase std p95="
                  f"{temporal_contrast(frames, 0.0, 1.0):.3f}, "
                  f"left={temporal_contrast(frames, 0.0, 0.18):.3f}, "
                  f"right={temporal_contrast(frames, 0.82, 1.0):.3f}, "
                  f"stationary edge ringing="
                  f"{left_ringing:.3f}/{right_ringing:.3f} "
                  f"({left_crowded}/{right_crowded} crowded pixels), "
                  f"foreground motion RMS={motion:.3f}, "
                  f"contrast={contrast:.3f}, sky change={cls.sky_change[name]}")

    def test_compressed_horizon_does_not_flash_across_grid_phase(self):
        # Removing the pixel footprint filter or restoring distant sharp rows
        # exposes tens of RGB levels of phase-dependent flashing in this band.
        # Four levels permit quantization and a mild fade while bounding the
        # compressed grid's visible phase fluctuation to about 1.6% of RGB range.
        for name, frames in self.motion.items():
            with self.subTest(palette=name):
                self.assertLessEqual(
                    temporal_contrast(frames, 0.0, 1.0), 4.0,
                    "Compressed horizon pixels flash as the grid moves")

    def test_offcenter_compressed_stripes_do_not_flash(self):
        # Testing both edges separately catches filtering that only accounts for
        # the central projection and misses oblique compressed grid stripes.
        for name, frames in self.motion.items():
            for side, left, right in (("left", 0.0, 0.18),
                                      ("right", 0.82, 1.0)):
                with self.subTest(palette=name, side=side):
                    self.assertLessEqual(
                        temporal_contrast(frames, left, right), 4.0,
                        f"Compressed {side} grid stripes flash across phase")

    def test_stationary_offcenter_stripes_do_not_ring_at_pixel_scale(self):
        # Filtering only the moving row axis can hide temporal shimmer while
        # leaving strongly alternating pixels on crowded longitudinal stripes.
        for name, frames in self.motion.items():
            for side, left, right in (("left", 0.0, 0.18),
                                      ("right", 0.82, 1.0)):
                with self.subTest(palette=name, side=side):
                    ringing, _ = stationary_edge_ringing(frames, left, right)
                    self.assertLessEqual(
                        ringing, 4.0,
                        f"Stationary {side} stripes retain pixel-scale ringing")

    def test_horizon_background_has_no_abrupt_full_width_seam(self):
        # A hard sky/ground branch produces a 5–10-level step. A three-level
        # adjacent-row ceiling admits the ordinary gradient on either side and
        # requires a smooth join at normal, small and ultrawide resolutions.
        for name, rgb in self.static.items():
            with self.subTest(frame=name):
                self.assertLessEqual(
                    boundary_jump(rgb), 3.0,
                    "Sky and ground background have an abrupt horizon seam")

    def test_foreground_grid_retains_visible_motion(self):
        # A frozen grid would satisfy all fluctuation ceilings; it must fail this.
        for name, frames in self.motion.items():
            with self.subTest(palette=name):
                foreground = patch(frames, 0.78, 0.95)
                difference = foreground - np.roll(foreground, 1, axis=0)
                self.assertGreaterEqual(
                    float(np.sqrt(np.mean(difference ** 2))), 6.0 * 24 / len(frames),
                    "Foreground grid must visibly travel through the full phase cycle")

    def test_foreground_grid_retains_resolved_line_contrast(self):
        # Fading the entire grid away would suppress shimmer but erase its design.
        for name, frames in self.motion.items():
            with self.subTest(palette=name):
                foreground = patch(frames, 0.78, 0.95)
                self.assertGreaterEqual(
                    float(foreground.std(axis=(1, 2)).mean()), 15.0,
                    "Resolved foreground grid lines have lost their visible contrast")

    def test_sky_does_not_change_with_grid_phase(self):
        for name, change in self.sky_change.items():
            with self.subTest(palette=name):
                self.assertEqual(change, 0, "Grid phase must leave the sun and sky static")

    def test_grid_motion_wrap_has_no_visible_pop(self):
        before = read_rgb(DIRECTORY / "seam-before.png").astype(np.int16)
        after = read_rgb(DIRECTORY / "seam-after.png").astype(np.int16)
        self.assertEqual(before.shape, after.shape)
        difference = np.abs(before - after)
        # Captures straddle the phase wrap by 0.0002 of one cell period. A tiny
        # legitimate movement is allowed; a discontinuous reset is not.
        self.assertLessEqual(float(np.quantile(difference, 0.99)), 2.0)
        self.assertLessEqual(int(difference.max()), 6)

    def test_repeated_fixed_phase_is_deterministic(self):
        np.testing.assert_array_equal(
            self.static["magenta"], read_rgb(DIRECTORY / "repeat.png"),
            err_msg="Identical GPU capture inputs must render identical pixels")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=DIRECTORY,
                        help="Complete directory produced by capture-visuals.ps1 -Motion")
    parser.add_argument("--extra-motion-directory", type=Path, action="append", default=[],
                        help="Additional motion-pN-XX.png full-cycle phase sequences; repeatable")
    parser.add_argument("--historical", action="store_true", help="Run archived analytic-grid thresholds explicitly")
    options, unittest_arguments = parser.parse_known_args()
    DIRECTORY = options.directory.resolve()
    EXTRA_MOTION_DIRECTORIES = [path.resolve() for path in options.extra_motion_directory]
    # Archived analytic-grid captures retain their original tests. Raised terrain
    # uses actual flat shoulders for filtering, and adjacent-frame slope motion.
    if (DIRECTORY / 'captures.json').is_file() and not options.historical:
        import importlib.util
        spec = importlib.util.spec_from_file_location('terrain_filters', Path(__file__).with_name('terrain-filter-quality.py'))
        terrain_filters = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(terrain_filters)
        raise SystemExit(terrain_filters.run(DIRECTORY, EXTRA_MOTION_DIRECTORIES))
    unittest.main(argv=[sys.argv[0], *unittest_arguments], verbosity=2)
