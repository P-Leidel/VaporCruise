"""Native city image checks; does not approximate rendering in Python."""
import argparse
from pathlib import Path
import unittest
import numpy as np
from PIL import Image
DIRECTORY = Path()
def rgb(name):
    with Image.open(DIRECTORY / name) as frame:
        return np.asarray(frame.convert('RGB')).astype(np.float32)
class BackgroundQuality(unittest.TestCase):
    def test_tall_landmark_visible(self):
        if not (DIRECTORY/'tall.png').exists(): self.skipTest('tall view not captured')
        for name in ('tall.png','minimum-aspect.png'):
            if not (DIRECTORY/name).exists(): continue
            frame = rgb(name); h,w = frame.shape[:2]
            # Only the landmark rises this far above the ordinary skyline.
            region = frame[round(h*(1/3-.115)):round(h*(1/3-.075)), w//2:]
            warm = (region[:,:,0] > region[:,:,2]*1.18) & (region[:,:,1] > region[:,:,2]*1.08)
            self.assertGreater(int(warm.sum()), 30, 'right-hand landmark survives '+name)
    def test_superwide_mountain_join(self):
        if not (DIRECTORY/'superwide.png').exists(): self.skipTest('32:9 view not captured')
        frame = rgb('superwide.png'); h,w = frame.shape[:2]
        y0,y1 = round(h*(1/3-.055)),round(h*(1/3-.035))
        # This route exposes the right atlas edge; opaque terrain covers the left.
        boundary = round(w/2+h*1.5)
        jump = np.abs(frame[y0:y1,boundary-2]-frame[y0:y1,boundary+2]).mean()
        self.assertLess(float(jump), 3, 'exposed mountain atlas edge remains continuous')
    def test_readable_warm_city(self):
        frame = rgb('magenta.png'); h,w = frame.shape[:2]
        # Off-sun building windows, between the city foot and ordinary roofs.
        y0,y1 = round(h*(1/3-.060)),round(h*(1/3-.014))
        x0,x1 = round(w*.5+h*.17),round(w*.5+h*.38)
        region = frame[y0:y1,x0:x1]
        warm = (region[:,:,0] > region[:,:,2]*1.18) & (region[:,:,1] > region[:,:,2]*1.08)
        self.assertGreater(int(warm.sum()), 30, 'visible warm windows on right-hand city towers')
    def test_repeat_capture(self):
        if not (DIRECTORY / 'repeat.png').exists(): self.skipTest('repeat not captured')
        np.testing.assert_array_equal(rgb('magenta.png'), rgb('repeat.png'))
    def test_light_wrap_continuity(self):
        names = ['light-9998.png','light-9999.png','light-0001.png','light-0002.png']
        if not (DIRECTORY/names[0]).exists(): self.skipTest('phase matrix not captured')
        frames = [rgb(name) for name in names]
        differences = [np.abs(a-b).mean() for a,b in zip(frames,frames[1:])]
        self.assertLessEqual(differences[1], max(differences[0],differences[2])*2+.03)
    def test_stationary_sky_and_foreground(self):
        if not (DIRECTORY/'light-quarter.png').exists(): self.skipTest('phase matrix not captured')
        a,b = rgb('magenta.png'),rgb('light-quarter.png'); h,w = a.shape[:2]
        np.testing.assert_array_equal(a[:round(h*(1/3-.19))],b[:round(h*(1/3-.19))])
        np.testing.assert_array_equal(a[round(h*(1/3+.02)):],b[round(h*(1/3+.02)):])
        difference = np.abs(a-b).max(axis=2)
        self.assertGreater(int((difference>0).sum()), 10, 'selected window groups animate')
        self.assertLess(float((difference>0).mean()), .01, 'animation stays local to emission')
if __name__ == '__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--directory',type=Path,required=True)
    args,extra=parser.parse_known_args(); DIRECTORY=args.directory
    unittest.main(argv=['background-quality']+extra)
