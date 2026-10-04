from camera_projection import load_camera
"""Filtering gates on actual flat shoulders; report moving slope changes."""
import json
import re
from pathlib import Path
import numpy as np
from PIL import Image

MOTION_NAME = re.compile(r"motion-p([0-2])-([0-9]+)\.png")
STATIC_NAMES = ("magenta", "cyan", "amber", "small", "portrait", "ultrawide")

def _records(path):
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if isinstance(value, dict):
        value = value.get("Frames", value.get("frames"))
    if not isinstance(value, list):
        raise ValueError(f"{path.name}: expected pose records")
    return value

def _dimensions(path):
    with Image.open(path) as image:
        image.load()
        return image.size

def validate_evidence(directory, extras):
    """Return (root, palette, numeric-order paths) only after all evidence is valid."""
    sequences = []
    for root in [Path(directory), *map(Path, extras)]:
        if not root.is_dir():
            raise ValueError(f"{root}: missing evidence directory")
        groups = {}
        for path in root.iterdir():
            if not path.name.lower().startswith("motion-p") or path.suffix.lower() == ".json":
                continue
            match = MOTION_NAME.fullmatch(path.name)
            if not match or not path.is_file():
                raise ValueError(f"{root.name}/{path.name}: malformed motion filename")
            palette, index = map(int, match.groups())
            group = groups.setdefault(palette, {})
            if index in group:
                raise ValueError(f"{root.name}/p{palette}: duplicate numeric index {index}")
            group[index] = path
        manifests = list(root.glob("motion-p*.json"))
        capture_manifest = root / "captures.json"
        if capture_manifest.exists():
            manifests.append(capture_manifest)
        declared = {}
        for manifest in manifests:
            records = _records(manifest)
            seen = set()
            manifest_names = set()
            for record in records:
                name = record.get("Name") if isinstance(record, dict) else None
                if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*",name) or name in seen:
                    raise ValueError(f"{manifest.name}: missing or duplicate pose name")
                seen.add(name)
                if not name.startswith("motion-p"):
                    if manifest.name != "captures.json":
                        raise ValueError(f"{manifest.name}: invalid motion pose {name}")
                    path = root / (name + ".png")
                    if not path.is_file() or _dimensions(path) != (record.get("Width"), record.get("Height")):
                        raise ValueError(f"{manifest.name}/{name}: missing image or dimension mismatch")
                    continue
                match = MOTION_NAME.fullmatch(name + ".png")
                if not match:
                    raise ValueError(f"{manifest.name}: malformed motion pose {name}")
                palette, index = map(int, match.groups())
                if "Palette" in record and record["Palette"] != palette:
                    raise ValueError(f"{manifest.name}/{name}: palette mismatch")
                if manifest.name != "captures.json" and manifest.name != f"motion-p{palette}.json":
                    raise ValueError(f"{manifest.name}/{name}: manifest palette mismatch")
                path = root / (name + ".png")
                if not path.is_file() or _dimensions(path) != (record.get("Width"), record.get("Height")):
                    raise ValueError(f"{manifest.name}/{name}: missing image or dimension mismatch")
                manifest_names.add(path.name)
                declared.setdefault(palette, set()).add(path.name)
            if manifest.name.startswith("motion-p"):
                palette_text = manifest.stem.removeprefix("motion-p")
                if palette_text not in ("0","1","2") or not manifest_names:
                    raise ValueError(f"{manifest.name}: empty or invalid motion manifest")
                actual = {p.name for p in groups.get(int(palette_text), {}).values()}
                if actual != manifest_names:
                    raise ValueError(f"{manifest.name}: manifest/file disagreement")
            elif manifest_names:
                actual = {p.name for g in groups.values() for p in g.values()}
                if actual != manifest_names:
                    raise ValueError(f"{manifest.name}: manifest/file disagreement")
        for palette, names in declared.items():
            if names != {p.name for p in groups.get(palette, {}).values()}:
                raise ValueError(f"{root.name}/p{palette}: manifest/file disagreement")
        build_manifest = root / "capture-build.json"
        if build_manifest.exists():
            metadata = json.loads(build_manifest.read_text(encoding="utf-8-sig"))
            if "schema_version" in metadata:
                if metadata.get("completion") is not True:
                    raise ValueError(f"{root.name}: incomplete capture manifest")
                if not isinstance(metadata.get("frames"),list) or not isinstance(metadata.get("expected_sequences"),list):
                    raise ValueError(f"{root.name}: capture manifest lacks complete pose/sequence requests")
                metadata_names=set()
                for pose in metadata["frames"]:
                    name=pose.get("Name")
                    if not isinstance(name,str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*",name) or name in metadata_names:
                        raise ValueError(f"{root.name}: invalid or duplicate requested pose")
                    metadata_names.add(name)
                    if _dimensions(root/(name+".png")) != (pose.get("Width"),pose.get("Height")):
                        raise ValueError(f"{root.name}/{name}: requested dimension mismatch")
                for expected in metadata["expected_sequences"]:
                    palette = expected["palette"]
                    paths = groups.get(palette, {})
                    if len(paths) != expected["count"]:
                        raise ValueError(f"{root.name}/p{palette}: expected sequence incomplete")
                    if any(_dimensions(p) != (expected["width"], expected["height"]) for p in paths.values()):
                        raise ValueError(f"{root.name}/p{palette}: expected dimension mismatch")
        required = {0,1,2} if root == Path(directory) else set(declared) or set(groups)
        if not required:
            raise ValueError(f"{root.name}: no motion evidence")
        if not required.issubset(groups):
            raise ValueError(f"{root.name}: missing palette {sorted(required-set(groups))}")
        for palette, group in sorted(groups.items()):
            indices = sorted(group)
            if len(indices) < 24 or indices != list(range(len(indices))):
                raise ValueError(f"{root.name}/p{palette}: need at least 24 consecutive frames starting at zero")
            paths = [group[i] for i in indices]
            sizes = {_dimensions(p) for p in paths}
            if len(sizes) != 1:
                raise ValueError(f"{root.name}/p{palette}: inconsistent dimensions")
            sequences.append((root, palette, paths))
    for name in STATIC_NAMES:
        _dimensions(Path(directory) / (name + ".png"))
    return sequences

def frames(directory, palette, paths=None):
    if paths is None:
        paths = sorted(directory.glob(f"motion-p{palette}-*.png"), key=lambda p: int(p.stem.rsplit("-",1)[1]))
    values = []
    for path in paths:
        with Image.open(path) as image:
            values.append(np.asarray(image.convert("RGB")).max(axis=2).astype(np.float32))
    return np.stack(values)

def horizon_jump(rgb, horizon_fraction=1/3):
    # Pixel centers straddle the projected one-third-height boundary here.
    # A wider band includes intended road/grid light below the join.
    h,w=rgb.shape[:2]
    boundary=int(np.ceil(h*horizon_fraction-.5))
    if boundary < 1 or boundary >= h:
        raise ValueError("Horizon crop is empty")
    profile=rgb[boundary-1:boundary+1,round(w*.48):round(w*.52)].mean(axis=1)
    return float(np.abs(profile[1]-profile[0]).max())

def run(directory, extras):
    try:
        sequences = validate_evidence(directory, extras)
        cameras={root:load_camera(root) for root in {directory,*(entry[0] for entry in sequences)}}
    except (ValueError, AssertionError, OSError, KeyError, TypeError) as error:
        print(f"FAIL: motion evidence: {error}")
        return 1
    failures = []
    for root, palette, paths in sequences:
        value = frames(root,palette,paths); _,h,w = value.shape
        yy,xx = np.mgrid[:h,:w]
        camera=cameras[root]
        distance = (yy+.5)/h-camera["horizon_fraction"]
        xworld = (xx+.5-w*.5)/h * camera["height"] / np.maximum(distance,.0001)
        scale = (360/h)**.5
        mask = (distance>.04*scale)&(distance<.08*scale)&(np.abs(xworld)>3.5)&(np.abs(xworld)<5.5)
        if not mask.any():
            print(f"FAIL: {root.name}/p{palette}: flat-shoulder filter crop is empty")
            return 1
        fluctuation = float(np.quantile(value[:,mask].std(axis=0),.95))
        if fluctuation>4: failures.append(f"{root.name}/p{palette}: flat shoulder flashing {fluctuation:.3f}>4")
        foreground = value[:,round(h*.65):round(h*.90),:round(w*.22)]
        rms = float(np.sqrt(np.mean(np.diff(foreground,axis=0)**2)))
        if rms<.5: failures.append(f"{root.name}/p{palette}: slope motion missing")
        print(f"{root.name}/p{palette}: flat shoulder std p95={fluctuation:.3f}; adjacent slope RMS={rms:.3f}")
    for name in STATIC_NAMES:
        with Image.open(directory/f"{name}.png") as image: rgb=np.asarray(image.convert("RGB")).astype(np.float32)
        h,w=rgb.shape[:2]
        jump=horizon_jump(rgb,cameras[directory]["horizon_fraction"])
        print(f"{name}: open horizon jump={jump:.3f}")
        if jump>3: failures.append(f"{name}: horizon seam {jump:.3f}>3")
    for failure in failures: print("FAIL:",failure)
    print(f'{"FAIL" if failures else "PASS"}: terrain filtering gates')
    return 1 if failures else 0
