#!/usr/bin/env python3
"""Render before/after comparison of units resolution using UsdImagingGL."""
import os, sys

USD = '/home/horde/.openclaw/workspace-units/usd-install'
sys.path.insert(0, f'{USD}/lib/python')
os.environ['LD_LIBRARY_PATH'] = f'{USD}/lib:' + os.environ.get('LD_LIBRARY_PATH', '')
os.environ['DISPLAY'] = ':99'

from pxr import Usd, UsdGeom, UsdAppUtils, Sdf, Gf, Tf

os.chdir('/home/horde/.openclaw/workspace-units/OpenUSD/extras/exec/examples/unitsDemo')

# Render BEFORE (no GeomMetricsAPI)
print("=== Rendering BEFORE ===")
from PySide6.QtWidgets import QApplication
from PySide6.QtOpenGLWidgets import QOpenGLWidget
app = QApplication(sys.argv[:1])
widget = QOpenGLWidget()
widget.setFixedSize(1920, 1280)
widget.show()
app.processEvents()

stage_before = Usd.Stage.Open('factory_demo_before.usda')
recorder_before = UsdAppUtils.FrameRecorder()
recorder_before.SetImageWidth(1920)
cam_before = UsdGeom.Camera(stage_before.GetPrimAtPath('/World/Camera'))
recorder_before.SetActiveCamera(cam_before)
recorder_before.Record(stage_before, Usd.TimeCode.Default(), 'factory_demo_before_render.png')
print("Before render done")

# Render AFTER (with GeomMetricsAPI - units correction via auto-bootstrap)
print("=== Rendering AFTER ===")
stage_after = Usd.Stage.Open('factory_demo.usda')
recorder_after = UsdAppUtils.FrameRecorder()
recorder_after.SetImageWidth(1920)
cam_after = UsdGeom.Camera(stage_after.GetPrimAtPath('/World/Camera'))
recorder_after.SetActiveCamera(cam_after)
recorder_after.Record(stage_after, Usd.TimeCode.Default(), 'factory_demo_render.png')
print("After render done")

import hashlib
for f in ['factory_demo_before_render.png', 'factory_demo_render.png']:
    h = hashlib.md5(open(f, 'rb').read()).hexdigest()
    s = os.path.getsize(f)
    print(f'{f}: {s} bytes, md5={h}')
