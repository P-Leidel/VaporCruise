"""CPU regressions for evidence completeness; synthetic images are not GPU acceptance."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import numpy as np
from PIL import Image

spec = importlib.util.spec_from_file_location("filters", Path(__file__).with_name("terrain-filter-quality.py"))
filters = importlib.util.module_from_spec(spec)
spec.loader.exec_module(filters)

class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.main = self.root / "main"
        self.main.mkdir()
        for name in ("magenta", "cyan", "amber", "small", "portrait", "ultrawide"):
            Image.new("RGB", (160, 120)).save(self.main / (name + ".png"))

    def sequence(self, directory, palette=0, count=24, manifest=False):
        directory.mkdir(exist_ok=True)
        records = []
        for i in range(count):
            name = f"motion-p{palette}-{i:02}"
            Image.new("RGB", (160, 120), (0, 0, 0)).save(directory / (name + ".png"))
            records.append(dict(Name=name, Palette=palette, Width=160, Height=120,
                                Seed="6215337262362608466", Cell="31", Phase=str(i/24)))
        if manifest:
            (directory / f"motion-p{palette}.json").write_text(json.dumps(records))
        return records

    def matrix(self):
        for p in range(3):
            self.sequence(self.main, p)

    def rejects(self, directory=None, extras=None):
        output = io.StringIO()
        # If validation is missing, metrics will reach Image.open on the static files.
        with contextlib.redirect_stdout(output), patch.object(filters.np, "quantile", wraps=np.quantile) as metric:
            try:
                result = filters.run(directory or self.main, extras or [])
            except (AssertionError, ValueError, OSError):
                return
        self.assertFalse(metric.called, "Image metrics reached before inventory rejection")
        self.assertNotEqual(result, 0, output.getvalue())
        self.assertNotIn("PASS:", output.getvalue())

    def test_static_only_rejected_before_metrics(self):
        self.rejects()

    def test_missing_palette_rejected(self):
        for p in (0, 1):
            self.sequence(self.main, p)
        self.rejects()

    def test_empty_extra_rejected(self):
        self.matrix()
        extra = self.root / "empty"
        extra.mkdir()
        self.rejects(extras=[extra])

    def test_gap_and_nonzero_start_rejected(self):
        for kind in ("gap", "start"):
            with self.subTest(kind=kind):
                self.matrix()
                path = self.main / ("motion-p0-12.png" if kind == "gap" else "motion-p0-00.png")
                path.rename(self.main / "motion-p0-25.png")
                self.rejects()
                (self.main / "motion-p0-25.png").unlink()

    def test_duplicate_numeric_index_rejected(self):
        self.matrix()
        Image.new("RGB", (160,120)).save(self.main / "motion-p0-001.png")
        self.rejects()

    def test_malformed_name_rejected(self):
        self.matrix()
        Image.new("RGB",(160,120)).save(self.main / "motion-p0-invalid.png")
        self.rejects()

    def test_short_sequence_rejected(self):
        self.matrix()
        (self.main / "motion-p0-23.png").unlink()
        self.rejects()

    def test_inconsistent_dimensions_rejected(self):
        self.matrix()
        Image.new("RGB",(161,120)).save(self.main / "motion-p1-01.png")
        self.rejects()

    def test_corrupt_image_rejected(self):
        self.matrix()
        (self.main / "motion-p1-01.png").write_bytes(b"not png")
        self.rejects()

    def test_manifest_missing_extra_duplicate_and_wrong_dimensions(self):
        self.matrix()
        records = self.sequence(self.main, 0, manifest=True)
        for change in ("missing", "extra", "duplicate", "dimensions"):
            with self.subTest(change=change):
                edited = [dict(r) for r in records]
                if change == "missing":
                    edited.append(dict(edited[-1], Name="motion-p0-24"))
                elif change == "extra":
                    edited.pop()
                elif change == "duplicate":
                    edited.append(edited[0])
                else:
                    edited[0]["Width"] = 200
                (self.main / "motion-p0.json").write_text(json.dumps(edited))
                self.rejects()

    def test_numeric_order_through_120_frames(self):
        self.matrix()
        self.sequence(self.main, 0, count=120)
        ordered = filters.validate_evidence(self.main, [])
        paths = ordered[0][2]
        self.assertEqual([int(p.stem.rsplit("-",1)[1]) for p in paths], list(range(120)))

    def test_single_palette_extras_and_legacy_main_manifest(self):
        self.matrix()
        records = []
        for p in range(3):
            records.extend(self.sequence(self.main, p))
        (self.main / "captures.json").write_text(json.dumps(records))
        for manifested in (False, True):
            extra = self.root / str(manifested)
            self.sequence(extra, 2, manifest=manifested)
            self.assertEqual(len(filters.validate_evidence(self.main, [extra])), 4)

    def test_declared_absent_palette_rejected(self):
        self.matrix()
        extra = self.root / "extra"
        records = self.sequence(extra, 2, manifest=True)
        (extra / "motion-p0.json").write_text(json.dumps([dict(r,Name=r["Name"].replace("p2","p0"),Palette=0) for r in records]))
        self.rejects(extras=[extra])

    def test_complete_inventory_can_fail_motion_metric(self):
        self.matrix()
        from camera_projection import legacy_fixture_camera
        (self.main/"capture-build.json").write_text(json.dumps({"camera":legacy_fixture_camera()}))
        with contextlib.redirect_stdout(io.StringIO()) as output:
            result = filters.run(self.main, [])
        self.assertEqual(result, 1)
        self.assertIn("slope motion missing", output.getvalue())

    def test_camera_sidecars_do_not_shadow_motion_manifests(self):
        self.matrix()
        from camera_projection import legacy_fixture_camera
        (self.main/"capture-build.json").write_text(json.dumps({"camera":legacy_fixture_camera()}))
        for p in range(3):
            (self.main/f"camera-motion-p{p}-00.json").write_text(json.dumps({"camera":legacy_fixture_camera(),"width":64,"height":48}))
        filters.validate_evidence(self.main,[])

    def test_static_only_grid_entrypoint_fails(self):
        import subprocess, sys
        (self.main / "captures.json").write_text("[]")
        result = subprocess.run([sys.executable,str(Path(__file__).with_name("grid-quality-tests.py")),
                                 "--directory",str(self.main)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertNotIn("PASS:",result.stdout)

    def test_static_manifest_dimensions_and_missing_files(self):
        self.matrix()
        records=[]
        for p in range(3):
            records.extend(self.sequence(self.main,p))
        for name, width in (("magenta",161),("missing-static",160)):
            with self.subTest(name=name):
                (self.main/"captures.json").write_text(json.dumps(records+[dict(Name=name,Width=width,Height=120)]))
                self.rejects()

    def test_incomplete_versioned_capture_manifest_rejected(self):
        self.matrix()
        (self.main/"capture-build.json").write_text(json.dumps(dict(schema_version=1,completion=False,expected_sequences=[])))
        self.rejects()


    def test_horizon_gate_ignores_road_glow_below_boundary(self):
        for height in (320,720,1080,1280):
            with self.subTest(height=height):
                rgb=np.full((height,640,3),40,dtype=np.float32)
                boundary=int(np.ceil(height/3-.5))
                rgb[boundary+2:boundary+6]=200
                self.assertEqual(filters.horizon_jump(rgb),0)

    def test_horizon_gate_detects_actual_join_at_fractional_pixel_boundary(self):
        for height in (320,720,1080,1280):
            with self.subTest(height=height):
                rgb=np.zeros((height,640,3),dtype=np.float32)
                boundary=int(np.ceil(height/3-.5))
                rgb[boundary:]=100
                self.assertEqual(filters.horizon_jump(rgb),100)


if __name__ == "__main__":
    unittest.main(verbosity=2)
