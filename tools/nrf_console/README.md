# EPD Tag Designer (`nrf_console`)

PC 端墨水屏 **DIY 设计台**（图层文字/价格/图形，多尺寸）+ **BLE 上传**到 `eink_tag`。  
设计器来自 `tmp/epd` 浏览器工具；上传协议见 [`../../ble/docs/display_plane_upload.md`](../../ble/docs/display_plane_upload.md)。

## 环境

- Windows 10/11，本机蓝牙已开，**WebView2** 可用（Win10/11 一般自带）
- Python 3.11+

```powershell
cd tools\nrf_console
py -3 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python main.py
```

- 默认：HTML DIY 设计器（pywebview）
- 旧版 Tk 控制台：`python main.py --classic`

## DIY 设计器用法

1. 顶栏 **BLE**：Scan → 选 `eink_tag` → Connect  
2. 左侧加组件（文字 / 价格组 / 标签 / 矩形…），画布拖拽改位置  
3. **屏幕尺寸**默认 `128×296`（固件上传尺寸）；可改其它尺寸做设计，但上传须改回 128×296  
4. **上传到标签**：导出 BWR 平面（MSB、BW 1=白、RED 1=红，并做 `flip_y`）→ `BEGIN/CHUNK/REFRESH`  
5. 仍可「生成输出」下载 `.bin` / `.h`，或串口发送（原工具能力）

## CLI 冒烟

```powershell
python smoke_upload.py
python smoke_upload.py --page tag
```

## 布局

```
nrf_console/
  designer/          # HTML DIY + pywebview BLE bridge
    static/index.html
    bridge.py
    planes.py        # flip_y for panel RAM
  app/               # --classic Tk UI + BleController
  epd/               # plane helpers / templates / image import
```
