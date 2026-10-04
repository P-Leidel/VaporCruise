import importlib.util
from pathlib import Path
import unittest
import numpy as np
spec=importlib.util.spec_from_file_location('comparisons',Path(__file__).with_name('shader-binding-comparisons.py'))
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class ConsensusTests(unittest.TestCase):
    def test_isolated_resolve_outlier(self):
        a=np.zeros((10,10,3),dtype=np.uint8)
        self.assertTrue(module.compare_arrays([a+1,a,a],[a,a,a])[0]['passed'])
    def test_persistent_binding_regression(self):
        a=np.zeros((10,10,3),dtype=np.uint8)
        self.assertFalse(module.compare_arrays([a,a,a],[a+1,a+1,a+1])[0]['passed'])
        self.assertFalse(module.compare_arrays([a,a,a],[a+10,a+10,a+10])[0]['passed'])
    def test_fixed_five_capture_reference(self):
        a=np.zeros((10,10,3),dtype=np.uint8)
        self.assertTrue(module.compare_arrays([a+1,a+1,a,a,a],[a]*5)[0]['passed'])
        self.assertFalse(module.compare_arrays([a]*5,[a+1]*5)[0]['passed'])
        with self.assertRaises(ValueError):module.compare_arrays([a]*4,[a]*4)
    def test_incomplete_and_mismatched_captures(self):
        a=np.zeros((10,10,3),dtype=np.uint8)
        with self.assertRaises(ValueError):module.compare_arrays([a],[a,a,a])
        with self.assertRaises(ValueError):module.compare_arrays([a,a,a],[a,a,a[:3]])
if __name__=='__main__': unittest.main()
