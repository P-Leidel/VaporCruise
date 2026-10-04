"""Camera mapping from native capture metadata; coordinates address pixel centers."""
import json
import math
from pathlib import Path

def validate_camera(camera):
    fields=('height','focal_scale','horizon_fraction','near','far','cell_size','minimum_terrain_reach')
    if not isinstance(camera,dict): raise ValueError('Missing camera metadata')
    for field in fields:
        value=camera.get(field)
        if isinstance(value,bool) or not isinstance(value,(float,int)) or not math.isfinite(value) or value<=0:
            raise ValueError(f'Invalid camera {field}')
    if camera['horizon_fraction']>=1 or camera['near']>=camera['far']: raise ValueError('Invalid camera ranges')
    return camera

def load_camera(directory):
    try:
        metadata=json.loads((Path(directory)/'capture-build.json').read_text(encoding='utf-8-sig'))
        if not isinstance(metadata,dict): raise ValueError("Malformed capture metadata object")
        return validate_camera(metadata.get('camera'))
    except (OSError,TypeError,KeyError,json.JSONDecodeError) as error:
        raise ValueError(f'Missing or malformed capture camera: {directory}') from error

def legacy_fixture_camera():
    """Explicit adapter for historical fixtures made before camera metadata existed."""
    return dict(height=3.2,focal_scale=.8,horizon_fraction=1/3,near=.1,far=640,cell_size=2,minimum_terrain_reach=512)

def _validate(camera,width,height,*point):
    validate_camera(camera)
    if not all(math.isfinite(v) for v in (width,height,*point)) or width<=0 or height<=0: raise ValueError('Invalid dimensions/point')

def project_world(camera,width,height,x,y,z):
    _validate(camera,width,height,x,y,z)
    if z<=0: raise ValueError('World point must be in front of camera')
    n,f=camera['near'],camera['far']
    return (width*.5+height*camera['focal_scale']*x/z-.5,
            height*camera['horizon_fraction']+height*camera['focal_scale']*(camera['height']-y)/z-.5,
            f/(f-n)-n*f/((f-n)*z))

def unproject_ground(camera,width,height,pixel_x,pixel_y):
    _validate(camera,width,height,pixel_x,pixel_y)
    below=(pixel_y+.5)/height-camera['horizon_fraction']
    if below<=0: return None
    z=camera['focal_scale']*camera['height']/below
    return ((pixel_x+.5-width*.5)*z/(height*camera['focal_scale']),z)
