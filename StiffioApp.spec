# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['FrontEnd.py'],
    pathex=[],
    binaries=[],
    datas=[('Logo invertido.png', '.'), ('Logo.jpg', '.'), ('download-icon.png', '.'), ('delete-icon.png', '.'), ('datos_stiffio.csv', '.'), ('mediciones_pwv.csv', '.'), ('Imagenes', 'Imagenes')],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
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
    name='StiffioApp',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['stiffio_icon.ico'],
)
