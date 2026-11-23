# ink_panel_console release build

用 **PyInstaller** 打 Windows 可分发目录（onedir）。内嵌 `nrf_console` 的 BLE/协议依赖。

## 前置

- Windows 10/11 + **本机蓝牙**
- Python 3.11+（推荐复用 `tools/nrf_console/.venv`）
- 同仓库已有 `tools/nrf_console`

## 一键编译

```powershell
cd tools\ink_panel_console
.\release\build.cmd
# 或
powershell -File .\release\build.ps1
```

产物：

```
release/dist/ink_panel_console/
  ink_panel_console.exe
  _internal/          # 依赖，须整夹拷贝
  VERSION.txt
  docs/               # 可选说明拷贝
```

**发布时拷贝整个 `ink_panel_console` 文件夹**，不要只拷 exe。

## 调试版（带控制台）

```powershell
powershell -File .\release\build.ps1 -Console
```

## 清理

```powershell
powershell -File .\release\build.ps1 -Clean
```

## 运行

双击 `ink_panel_console.exe`，或：

```powershell
.\release\dist\ink_panel_console\ink_panel_console.exe
```

「打开 DIY 设计器」在打包版中需另备 `nrf_console` 可执行包，或回到源码树 `python main.py`。

## 与固件版本

本工具 **0.4.0** 对齐固件归档 **`firmware/0.4.0`**（`ink_panel`）。
