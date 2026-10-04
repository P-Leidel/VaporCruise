"""Measure road dash spacing and length in native full-screen palette captures.

Use tests/capture-terrain-grid-dashes.ps1, then pass its output directory.
This reads displayed pixels and native camera metadata, not shader constants.
"""
import argparse
from pathlib import Path
import unittest

import numpy as np
from PIL import Image
from camera_projection import load_camera

DIRECTORY = Path()
PALETTES = ('magenta', 'cyan', 'amber')


def dash_spans(directory, name):
    camera = load_camera(directory)
    with Image.open(directory / (name + '-large.png')) as image:
        frame = np.asarray(image.convert('RGB'))
    height, width = frame.shape[:2]
    if (width, height) != (2560, 1397):
        raise AssertionError('Dash measurements require the full-screen reference size')
    rows = np.arange(round(height * .44), height)
    # Bright center cores stand apart from black pavement and soft end halos.
    lit = frame[rows, width//2-1:width//2+1].max(axis=(1, 2)) > 100
    transitions = np.flatnonzero(np.diff(np.r_[False, lit, False]))
    spans = []
    for start, end in transitions.reshape(-1, 2):
        if start == 0 or end == len(rows):
            continue  # Partial dashes at either crop boundary cannot measure length.
        endpoints = rows[[start, end]] + .5
        cells = camera['height'] * camera['focal_scale'] / (
            endpoints/height - camera['horizon_fraction']) / camera['cell_size']
        spans.append(cells)
    if len(spans) < 2:
        raise AssertionError('Need at least two complete visible foreground dashes')
    return np.asarray(spans)


class RoadDashQuality(unittest.TestCase):
    def test_center_dashes_repeat_every_two_cells(self):
        for name in PALETTES:
            spans = dash_spans(DIRECTORY, name)
            period = float(np.median(-np.diff(spans[:, 0])))
            with self.subTest(palette=name):
                print(f'{name} visible dash period: {period:.3f} cells')
                self.assertGreaterEqual(period, 1.8)
                self.assertLessEqual(period, 2.2)

    def test_center_dashes_have_short_luminous_cores(self):
        for name in PALETTES:
            spans = dash_spans(DIRECTORY, name)
            length = float(np.median(spans[:, 0] - spans[:, 1]))
            with self.subTest(palette=name):
                print(f'{name} visible dash length: {length:.3f} cells')
                self.assertGreaterEqual(length, .85)
                self.assertLessEqual(length, 1.35)

    def test_all_palettes_use_the_same_dash_placement(self):
        camera = load_camera(DIRECTORY)
        def pixel_rows(spans):
            return 1397 * camera['horizon_fraction'] + 1397 * camera['height'] * camera['focal_scale'] / (
                spans * camera['cell_size']) - .5
        reference = dash_spans(DIRECTORY, PALETTES[0])
        for name in PALETTES[1:]:
            with self.subTest(palette=name):
                spans = dash_spans(DIRECTORY, name)
                self.assertEqual(spans.shape, reference.shape)
                # Different authored hues cross the RGB8 brightness threshold
                # one pixel apart. Test that raster uncertainty in pixels;
                # fixed world-cell tolerance grows unfair near the horizon.
                self.assertLessEqual(float(np.abs(pixel_rows(spans)-pixel_rows(reference)).max()), 1.01)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    args, extra = parser.parse_known_args()
    DIRECTORY = args.directory
    unittest.main(argv=[__file__, *extra], verbosity=2)
