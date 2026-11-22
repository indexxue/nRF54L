# ink_panel_console

**ink_panel 桌牌助手**（CustomTkinter）：面向运营的推送向导；协议联调收进「高级」。

设计：[`../../docs/superpowers/specs/2026-11-22-ink-panel-console-ux-design.md`](../../docs/superpowers/specs/2026-11-22-ink-panel-console-ux-design.md)

| 项 | 值 |
|----|-----|
| 默认入口 | **GUI**（`python main.py`） |
| 设备名 | `ink_panel` |
| 面板 | 400×300 BWR |
| 主路径 | 连接桌牌 → 选页 → 填内容/导入图 → 推送并显示 |

产品：[`../../tmp/ink_panel/PRODUCT.md`](../../tmp/ink_panel/PRODUCT.md)  
固件：[`../../projects/ink_panel/README.md`](../../projects/ink_panel/README.md)

## 界面能力（运营默认）

- **一键连接桌牌**（失败可在高级里手动扫/选）
- 左侧选 **首页 / 记事第 N 页 / 信息第 N 页**（按设备 `content_count`）
- 侧栏 **今日三件事 / 门牌状态** 表单，实时预览
- **推送并显示** / **仅保存到设备**
- 空组：**启用记事（3 页）/ 启用信息（2 页）**
- **高级**：PING、拉槽状态、组 meta、未启用页、DIY 设计器、协议日志

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
python main.py cli upload --page notes --mode notes --page-id 1
python main.py cli upload --image ..\..\tmp\ink_panel\logo_bwr.png --mode home --page-id 0
python main.py cli upload --page bands --no-show   # 只落槽不切显
python main.py cli designer
```

## 与 nrf_console

| 需求 | 工具 |
|------|------|
| 产品联调（模/页/预览/推槽） | **本 GUI** |
| 自由图层 DIY | 本 GUI「打开 DIY 设计器」→ **nrf_console** |
| 共享 BLE 编解码 | 只维护 `tools/nrf_console` |
