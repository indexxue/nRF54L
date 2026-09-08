# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for nrf_console (EPD Tag Designer).

Build from tools/nrf_console:
  .\\release\\build.cmd
"""

from pathlib import Path

from PyInstaller.utils.hooks import collect_all, collect_data_files

SPECDIR = Path(SPEC).resolve().parent
ROOT = SPECDIR.parent
PKG = ROOT / "nrf_console"

# build.ps1 may set NRF_CONSOLE_CONSOLE=1 for a console debug build
import os

_console = os.environ.get("NRF_CONSOLE_CONSOLE", "").strip() in ("1", "true", "True", "yes")

datas = [
    (str(PKG / "designer" / "static"), "nrf_console/designer/static"),
]
binaries = []
hiddenimports = [
    "bleak",
    "bleak.backends.winrt",
    "bleak.backends.winrt.scanner",
    "bleak.backends.winrt.client",
    "webview",
    "webview.platforms.edgechromium",
    "clr_loader",
    "customtkinter",
    "PIL",
    "PIL.Image",
]

for mod in ("bleak", "webview", "customtkinter", "clr_loader"):
    try:
        d, b, h = collect_all(mod)
        datas += d
        binaries += b
        hiddenimports += h
    except Exception:
        try:
            datas += collect_data_files(mod)
        except Exception:
            pass

a = Analysis(
    [str(ROOT / "main.py")],
    pathex=[str(ROOT)],
    binaries=binaries,
    datas=datas,
    hiddenimports=[x for x in dict.fromkeys(hiddenimports) if isinstance(x, str)],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name="nrf_console",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=_console,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="nrf_console",
)
