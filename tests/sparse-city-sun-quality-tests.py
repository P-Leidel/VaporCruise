"""Check subtle solar motion and dark city/foreground in actual native output."""
import argparse
import json
from pathlib import Path
import unittest

import numpy as np
from PIL import Image
from camera_projection import load_camera

DIRECTORY = Path()
BASELINE = Path()
PALETTES = ('magenta', 'cyan', 'amber')
PHASES = ('', '-light-0.25', '-light-0.5', '-light-0.75', '-light-0.95')


def rgb(directory, name):
    with Image.open(directory / (name + '.png')) as frame:
        return np.asarray(frame.convert('RGB'), dtype=np.int16)


class SparseCitySunQuality(unittest.TestCase):
    def test_complete_palette_phase_aspect_and_transition_evidence(self):
        metadata = json.loads((DIRECTORY/'capture-build.json').read_text(encoding='utf-8-sig'))
        self.assertIs(metadata['completion'], True)
        required = {p+s for p in PALETTES for s in
                    (*PHASES, '-light-0.9998', '-light-0.9999', '-light-0.0001',
                     '-light-0.0002', '-large', '-small', '-portrait', '-transition')}
        self.assertTrue(required.issubset({pose['Name'] for pose in metadata['frames']}))
        horizon = load_camera(DIRECTORY)['horizon_fraction']
        for pose in metadata['frames']:
            frame = rgb(DIRECTORY, pose['Name'])
            h,w = frame.shape[:2]
            with self.subTest(pose=pose['Name']):
                self.assertEqual((w,h), (pose['Width'],pose['Height']))
                boundary = int(np.ceil(h*horizon-.5))
                profile = frame[boundary-1:boundary+1, round(w*.48):round(w*.52)].mean(axis=1)
                self.assertLessEqual(float(np.abs(profile[1]-profile[0]).max()),3)

    def test_solar_color_and_glow_change_subtly_in_every_palette(self):
        horizon = load_camera(DIRECTORY)['horizon_fraction']
        for palette in PALETTES:
            frames = [rgb(DIRECTORY,palette+s) for s in PHASES]
            h,w = frames[0].shape[:2]
            yy,xx = np.indices((h,w))
            x=(xx+.5-w*.5)/h
            y=horizon-(yy+.5)/h
            distance=np.sqrt(x*x+(y-.16)**2)
            # Whole upper disc: no stripe boundaries, facade coverage or fog.
            disc=(distance<.10)&(y>.205)
            halo=(distance>.15)&(distance<.23)&(y>.12)
            disc_means=np.array([f[disc].mean(axis=0) for f in frames])
            halo_means=np.array([f[halo].mean(axis=0) for f in frames])
            disc_range=float(np.ptp(disc_means,axis=0).max())
            halo_range=float(np.ptp(halo_means,axis=0).max())
            print(f'{palette}: upper disc range {disc_range:.3f}, local halo {halo_range:.3f} RGB8')
            with self.subTest(palette=palette):
                self.assertGreater(disc_range,.20,'sun warmth must evolve visibly')
                self.assertGreater(halo_range,.15,'local solar glow must breathe')
                self.assertLess(disc_range,8,'disc variation must remain subtle')
                self.assertLess(halo_range,8,'glow variation must remain subtle')

    def test_outer_sky_and_foreground_keep_their_contrast(self):
        horizon=load_camera(DIRECTORY)['horizon_fraction']
        for palette in PALETTES:
            reference=rgb(BASELINE,palette)
            h,w=reference.shape[:2]
            yy,xx=np.indices((round(h*.15),w))
            outside=((xx+.5-w*.5)/h)**2+((yy+.5)/h-(horizon-.16))**2>.26**2
            for suffix in PHASES:
                frame=rgb(DIRECTORY,palette+suffix)
                for label,delta in (
                    ('outer sky',np.abs(frame[:outside.shape[0]]-reference[:outside.shape[0]])[outside]),
                    ('foreground',np.abs(frame[round(h*.65):]-reference[round(h*.65):]).reshape(-1,3))):
                    with self.subTest(palette=palette,phase=suffix,region=label):
                        self.assertLessEqual(int(delta.max()),1)
                        self.assertLessEqual(float(np.any(delta,axis=1).mean()),.001)
                self.assertLess(int(frame[208:212,664:667].max()),40,
                                'unlit opaque city core stays dark in front of the sun')

    def test_cycle_wrap_and_repeat_are_stable(self):
        horizon=load_camera(DIRECTORY)['horizon_fraction']
        for palette in PALETTES:
            frames=[rgb(DIRECTORY,palette+'-light-'+phase) for phase in
                    ('0.9998','0.9999','0.0001','0.0002')]
            deltas=[np.abs(a-b) for a,b in zip(frames,frames[1:])]
            # The middle pair spans twice the elapsed time. Moving fog below
            # the horizon already changes by two RGB8 levels in the retained
            # baseline; compare its rate with the adjacent phase intervals.
            means=[float(delta.mean()) for delta in deltas]
            solar_end=round(frames[0].shape[0]*(horizon-.10))
            with self.subTest(palette=palette):
                self.assertLessEqual(means[1],2*max(means[0],means[2])+.0005)
                self.assertLess(means[1],.03)
                self.assertLessEqual(int(deltas[1][:solar_end].max()),1,
                                     'local solar cycle cannot jump at the wrap')
        delta=np.abs(rgb(DIRECTORY,'magenta')-rgb(DIRECTORY,'repeat'))
        self.assertLessEqual(int(delta.max()),1)
        self.assertLessEqual(float(np.any(delta,axis=2).mean()),.001)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--baseline-directory',type=Path,required=True)
    args,extra=parser.parse_known_args()
    DIRECTORY,BASELINE=args.directory,args.baseline_directory
    unittest.main(argv=[__file__,*extra],verbosity=2)
