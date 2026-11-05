# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['te_ev1_viewer.py'],
    pathex=[],
    binaries=[
        ('windows_SDK_20260813\\c#\\x64\\i3system_te_cs_x64.dll', '.'),
        ('windows_SDK_20260813\\c#\\x64\\i3system_TE_x64.dll', '.'),
        ('windows_SDK_20260813\\c#\\x64\\i3system_USB_DLL_V2_2_x64.dll', '.'),
        ('windows_SDK_20260813\\c#\\x64\\i3system_imgproc_impl_x64.dll', '.'),
        ('bin_bridge\\I3CameraBridge.dll', '.'),
    ],
    datas=[],
    hiddenimports=['clr', 'System', 'pythonnet'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        'matplotlib', 'scipy', 'pandas', 'torch', 'torchvision', 'PyQt5',
        'IPython', 'streamlit', 'tkinter', 'PIL', 'openpyxl', 'pyarrow',
        'polars', 'bokeh', 'altair', 'plotly', 'mediapipe', 'ultralytics'
    ],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='ThermalExpertViewer',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
