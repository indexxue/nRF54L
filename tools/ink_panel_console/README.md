# ink_panel_console

**ink_panel** 可视化联调台（CustomTkinter）：4.2" 预览、模/页选择、BLE 上传；DIY 图层设计关联 sibling [`../nrf_console`](../nrf_console)。

| 项 | 值 |
|----|-----|
| 默认入口 | **GUI**（`python main.py`） |
| 设备名 | `ink_panel` |
| 面板 | 400×300 BWR |
| α 上传 | `BEGIN/CHUNK/REFRESH` → 固件写入 **Flash home/0** |

产品：[`../../tmp/ink_panel/PRODUCT.md`](../../tmp/ink_panel/PRODUCT.md)  
固件：[`../../projects/ink_panel/README.md`](../../projects/ink_panel/README.md)

## 界面能力

- **Scan / Connect / PING / Disconnect**
- 左侧选 **首页 / 记事 / 信息** + `page_id`
- 内置模板预览（三色烟测 / 首页 / 记事 / 信息牌）
- **导入图片** → 量化为 400×300 BWR 并预览
- **上传到设备**
- **打开 DIY 设计器**：启动 `nrf_console`，并写入桥接配置（filter=`ink_panel`）；画布请用 400×300

## 环境

```powershell
# 推荐复用 nrf_console venv
cd tools\nrf_console
py -3 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt

cd ..\ink_panel_console
python main.py
```

或本目录：`pip install -r requirements.txt` 后同样 `python main.py`。

## 断连 / 重连注意（Windows）

板端 **BTN1 单击** 会断 BLE；PC 须等约 1s 再 **Scan → Connect**。  
旧版若 Scan=0 或 `service not found: 53530120-…`：已修（远端断连释放 WinRT + SN UUID 自动发现）。请重启 `python main.py`。

## CLI（机头 / 脚本）

```powershell
python main.py cli scan
python main.py cli ping
python main.py cli upload --page bands
python main.py cli upload --image ..\..\tmp\ink_panel\logo_bwr.png
python main.py cli designer
```

## 与 nrf_console

| 需求 | 工具 |
|------|------|
| 产品联调（模/页/预览/推槽） | **本 GUI** |
| 自由图层 DIY | 本 GUI「打开 DIY 设计器」→ **nrf_console** |
| 共享 BLE 编解码 | 只维护 `tools/nrf_console` |
