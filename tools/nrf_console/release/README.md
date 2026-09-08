# nrf_console release build

用 **PyInstaller** 打 Windows 可分发目录（onedir）。

## 前置

- Windows 10/11
- Python 3.11+
- 本机已装 **WebView2**（设计器用）

## 一键编译

```powershell
cd tools\nrf_console
.\release\build.cmd
# 或
powershell -File .\release\build.ps1
```

产物：

```
release/dist/nrf_console/
  nrf_console.exe
  _internal/          # 依赖，须整夹拷贝
  VERSION.txt
```

**发布时拷贝整个 `nrf_console` 文件夹**，不要只拷 exe。

## 调试版（带控制台）

```powershell
powershell -File .\release\build.ps1 -Console
```

## 清理

```powershell
powershell -File .\release\build.ps1 -Clean
```

## 运行

双击 `nrf_console.exe`，或：

```powershell
.\release\dist\nrf_console\nrf_console.exe
.\release\dist\nrf_console\nrf_console.exe --classic
```
