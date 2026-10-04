from importlib.util import spec_from_file_location, module_from_spec
from pathlib import Path
_spec=spec_from_file_location('camera_projection_impl',Path(__file__).with_name('camera-projection.py'))
_impl=module_from_spec(_spec)
_spec.loader.exec_module(_impl)
load_camera=_impl.load_camera
validate_camera=_impl.validate_camera
project_world=_impl.project_world
unproject_ground=_impl.unproject_ground
legacy_fixture_camera=_impl.legacy_fixture_camera
