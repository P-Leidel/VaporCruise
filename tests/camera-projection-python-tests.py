import importlib.util
import json
import math
from pathlib import Path
import tempfile
import unittest
spec=importlib.util.spec_from_file_location('camera',Path(__file__).with_name('camera-projection.py'))
camera=importlib.util.module_from_spec(spec)
spec.loader.exec_module(camera)

class CameraTests(unittest.TestCase):
    def setUp(self):
        self.c=dict(height=5,focal_scale=.6,horizon_fraction=.4,near=.1,far=640,cell_size=2,minimum_terrain_reach=512)
    def test_round_trips_and_centers(self):
        for w,h in [(1280,720),(480,320),(720,1280),(2560,1080)]:
            for z in [.1,2,400,480,512,640]:
                p=camera.project_world(self.c,w,h,7,0,z)
                q=camera.unproject_ground(self.c,w,h,*p[:2])
                self.assertAlmostEqual(q[0],7)
                self.assertAlmostEqual(q[1],z)
                self.assertAlmostEqual(p[1],h*.4+h*.6*5/z-.5)
            self.assertIsNone(camera.unproject_ground(self.c,w,h,0,h*.4-.5))
    def test_metadata_validation(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            with self.assertRaises(ValueError): camera.load_camera(root)
            p=root/'capture-build.json'
            p.write_text(json.dumps({'camera':self.c}))
            self.assertEqual(camera.load_camera(root),self.c)
            for field in self.c:
                for value in [None,float('nan'),float('inf'),-1,0]:
                    invalid=dict(self.c);invalid[field]=value
                    p.write_text(json.dumps({'camera':invalid}))
                    with self.assertRaises(ValueError): camera.load_camera(root)
            invalid=dict(self.c);invalid['near']=700
            p.write_text(json.dumps({'camera':invalid}))
            with self.assertRaises(ValueError): camera.load_camera(root)
    def test_invalid_points_and_dimensions(self):
        for bad in [0,-1,float('inf'),float('nan')]:
            with self.assertRaises(ValueError): camera.project_world(self.c,bad,720,0,0,2)
        with self.assertRaises(ValueError): camera.project_world(self.c,640,360,0,0,0)

if __name__=='__main__': unittest.main()
