"""Compare fixed GPU poses, allowing sparse 8-bit MSAA resolve rounding only."""
import sys
import numpy as np
from PIL import Image

first, second = [np.asarray(Image.open(path).convert('RGB'), dtype=np.int16)
                 for path in sys.argv[1:]]
if first.shape != second.shape:
    raise SystemExit('FAIL: capture dimensions changed')
delta = np.abs(first-second)
changed = np.count_nonzero(delta.max(axis=2))
maximum = int(delta.max())
fraction = changed / (first.shape[0] * first.shape[1])
# A resolve can round an exact half-level differently after resource recreation.
# Geometry, lighting, pose and broad color differences must still fail.
if maximum > 1 or fraction > .001:
    raise SystemExit(f'FAIL: max channel delta {maximum}; changed pixels {fraction:.6%}')
print(f'PASS: max channel delta {maximum}; changed pixels {fraction:.6%}')
