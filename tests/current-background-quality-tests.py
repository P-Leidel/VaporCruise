"""Current city contracts; earlier warmth/landmark thresholds remain historical."""
import argparse
import json
from pathlib import Path
import unittest
import numpy as np
from PIL import Image

DIRECTORY = Path()

def rgb(name):
    with Image.open(DIRECTORY / name) as image:
        image.load()
        return np.asarray(image.convert("RGB"),dtype=np.int16)

def median_reference(captures):
    if len(captures) != 5:
        raise ValueError("Exactly five retained captures are required")
    if any(not isinstance(frame, np.ndarray) or frame.dtype != np.int16
           or frame.ndim != 3 or frame.shape[2] != 3 for frame in captures):
        raise ValueError("References must be signed RGB8 capture arrays")
    if any(frame.shape != captures[0].shape for frame in captures):
        raise ValueError("Reference capture dimensions disagree")
    reference=np.median(np.stack(captures),axis=0).astype(np.int16)
    if any(int(np.abs(frame-reference).max()) > 1 for frame in captures):
        raise ValueError("Raw captures exceed one-level rounding against their median")
    return reference

def consensus_rgb(stem):
    # Retain every raw capture. A fixed five-image median removes isolated
    # one-level resolve outliers while persistent differences keep both gates.
    names = [stem] + [f"{stem}-reference-{index}" for index in range(1,5)]
    return median_reference([rgb(name+".png") for name in names])

def assert_resolve_equal(test, a, b):
    test.assertEqual(a.shape,b.shape)
    delta=np.abs(a-b)
    test.assertLessEqual(int(delta.max()),1,"only one-level MSAA resolve rounding is allowed")
    test.assertLessEqual(float(np.count_nonzero(delta.max(axis=2))/delta.shape[0]/delta.shape[1]),.001)

class CurrentBackground(unittest.TestCase):
    def test_complete_aspect_and_phase_evidence(self):
        for name,w,h in (("magenta",1280,720),("repeat",1280,720),
                         ("tall",480,2560),("minimum-aspect",240,7680),
                         ("superwide",5120,1440),("light-quarter",1280,720),
                         ("light-9998",1280,720),("light-9999",1280,720),
                         ("light-0001",1280,720),("light-0002",1280,720)):
            self.assertEqual(rgb(name+".png").shape,(h,w,3))
        metadata=json.loads((DIRECTORY/"capture-build.json").read_text(encoding="utf-8-sig"))
        self.assertIs(metadata["completion"],True)
        self.assertGreater(metadata["TextureBytes"],0)

    def test_repeat_capture_with_sparse_resolve_rounding(self):
        a,b=consensus_rgb("magenta"),consensus_rgb("repeat")
        delta=np.abs(a-b)
        print(f"repeat median of five per path: max delta {delta.max()}, "
              f"changed fraction {np.any(delta,axis=2).mean():.8f}")
        assert_resolve_equal(self,a,b)

    def test_light_wrap_continuity(self):
        values=[rgb(name+".png") for name in ("light-9998","light-9999","light-0001","light-0002")]
        differences=[float(np.abs(a-b).mean()) for a,b in zip(values,values[1:])]
        self.assertLessEqual(differences[1],max(differences[0],differences[2])*2+.03)

    def test_stationary_sky_outside_sun_and_foreground(self):
        a,b=rgb("magenta.png"),rgb("light-quarter.png")
        h,w=a.shape[:2]
        # The sun now breathes on this clock. Continue protecting the sky
        # outside its local angular footprint, including the baked stars.
        yy,xx=np.indices((round(h*(1/3-.19)),w))
        outside_sun=((xx+.5-w*.5)/h)**2+((yy+.5)/h-(1/3-.16))**2>.26**2
        sky_delta=np.abs(a[:outside_sun.shape[0]]-b[:outside_sun.shape[0]])[outside_sun]
        self.assertLessEqual(int(sky_delta.max()),1)
        self.assertLessEqual(float(np.any(sky_delta,axis=1).mean()),.001)
        # The distant panorama and ground share animated fog. Upper sky and nearby
        # road retain their original stationary-phase contract.
        assert_resolve_equal(self,a[round(h*.45):],b[round(h*.45):])
        changed=np.abs(a-b).max(axis=2)
        self.assertGreater(int((changed>1).sum()),10,"selected window groups must animate")
        self.assertLess(float((changed>1).mean()),.22,"animation stays near the distant panorama")

    def test_superwide_atlas_join(self):
        frame=rgb("superwide.png"); h,w=frame.shape[:2]
        y0,y1=round(h*(1/3-.055)),round(h*(1/3-.035))
        boundary=round(w/2+h*1.5)
        jump=np.abs(frame[y0:y1,boundary-2]-frame[y0:y1,boundary+2]).mean()
        self.assertLess(float(jump),3,"exposed mountain atlas edge remains continuous")

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--directory",type=Path,required=True)
    args=parser.parse_args()
    DIRECTORY=args.directory
    unittest.main(argv=[__file__],verbosity=2)
