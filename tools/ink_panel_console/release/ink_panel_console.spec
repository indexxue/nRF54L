# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller spec for ink_panel_console (桌牌助手).

Build from tools/ink_panel_console:
  .\\release\\build.cmd
"""

from pathlib import Path
import os
import sys

from PyInstaller.utils.hooks import collect_all, collect_data_files

SPECDIR = Path(SPEC).resolve().parent
ROOT = SPECDIR.parent
NRF_ROOT = ROOT.parent / "nrf_console"
PKG = ROOT / "ink_panel_console"

_console = os.environ.get("INK_PANEL_CONSOLE_CONSOLE", "").strip() in (
    "1",
    "true",
    "True",
    "yes",
)

# Dev imports need sibling nrf_console on path during Analysis
sys.path.insert(0, str(NRF_ROOT))
sys.path.insert(0, str(ROOT))

datas = []
binaries = []
hiddenimports = [
    "bleak",
    "bleak.backends.winrt",
    "bleak.backends.winrt.scanner",
    "bleak.backends.winrt.client",
    "customtkinter",
    "PIL",
    "PIL.Image",
    "ink_panel_console",
    "ink_panel_console.app",
    "ink_panel_console.app.ui",
    "ink_panel_console.app.ble_bridge",
    "ink_panel_console.app.flow",
    "ink_panel_console.app.preview",
    "ink_panel_console.templates",
    "ink_panel_console.templates.render",
    "nrf_console",
    "nrf_console.app.controller",
    "nrf_console.config",
    "nrf_console.protocol.codec",
    "nrf_console.protocol.cmds",
    "nrf_console.transport",
    "nrf_console.epd.plane",
    "nrf_console.epd.session",
]

for mod in ("bleak", "customtkinter", "nrf_console", "ink_panel_console"):
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
    pathex=[str(ROOT), str(NRF_ROOT)],
    binaries=binaries,
    datas=datas,
    hiddenimports=[x for x in dict.fromkeys(hiddenimports) if isinstance(x, str)],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=["webview", "clr_loader"],
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
    name="ink_panel_console",
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
    name="ink_panel_console",
)
