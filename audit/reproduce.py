"""Run the source-based regression checks replacing the original bug diagnostics.

Requires the PlatformIO libraries from pio run, g++, gcc, Node.js, curl, jq,
and Pillow for the real JPEG fixtures. No device is contacted.
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
for name in (
    'test-auth.py',
    'test-settings.py',
    'test-web-assets.py',
    'test-display.py',
    'test-wrapped-display.py',
    'test-display-heap.py',
    'test-upload.py',
    'test-jpeg.py',
    'regression-dashboard.py',
    'feed-regressions.py',
    'regression-network.py',
    'regression-webserver.py',
    'test-api-script.py',
):
    subprocess.run([sys.executable, str(root / 'audit' / name)], cwd=root, check=True)
subprocess.run(['node', str(root / 'audit' / 'regression-ui.cjs')], cwd=root, check=True)
subprocess.run(['node', str(root / 'audit' / 'regression-ui-errors.cjs')], cwd=root, check=True)
print('All host regression checks passed. Hardware validation remains separate.')
