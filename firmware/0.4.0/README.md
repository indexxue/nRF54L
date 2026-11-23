# firmware 0.4.0 — ink_panel 软休眠 + 页槽阶段

| 项 | 值 |
|----|-----|
| 版本 | **0.4.0** |
| 打包日 | 20261123 |
| 签名 | unsigned |
| 范围 | **仅 `ink_panel`**（本目录无其它工程） |
| 配套 PC 工具 | `tools/ink_panel_console` **0.4.0** |

## 本版要点

- **软休眠 v1**：约 120 s 无操作且无 VBUS → 关 EPD/触摸/Flash、停 BLE；USER 单击或插电唤醒；唤醒 LED 1 Hz×4 s
- **充电策略**：有 VBUS 不睡；拔线后再等约 20 s；LOGO + 电量叠层时约 5 min 刷电量至满
- **页槽 / 触摸**：IPS2、L 翻页 / R 切模 / HOME 回首页（既有能力保留）
- **健壮性**：进睡先关 EPD 轨；SLEEP 拒 job / 丢残留 CMD；CAP resume 失败关电

## 产物（本机已生成；二进制默认不入库）

| 文件 | 用途 |
|------|------|
| `nrf54l15_ink_panel_0.4.0_20261123_unsigned.hex` | **全新芯片 / 日常烧录首选**（优先为 `merged.hex`：MCUBoot + APP） |
| `nrf54l15_ink_panel_0.4.0_20261123_unsigned.elf` | 调试符号 |
| `nrf54l15_ink_panel_0.4.0_20261123_unsigned.bin` | 裸 APP 镜像（一般**不要**单独烧空白片） |

重新打包：

```powershell
.\nrf.cmd -Project ink_panel release 0.4.0
```

---

## 全新芯片烧哪份？怎么烧？

### 结论（空白 nRF54L15 / 未写过本仓固件）

烧 **`nrf54l15_ink_panel_0.4.0_20261123_unsigned.hex`**。

不要只烧 `.bin`：空白片需要 **MCUBoot + APP** 的合并 hex（`nrf.cmd release` 归档时优先拷 `build/merged.hex`）。

### 方式 A：仓库脚本（推荐）

接好 **J-Link SWD**，仓库根目录：

```powershell
# 若本机刚 release 过，构建目录已是 0.4.0 release 树，可直接：
.\nrf.cmd -Project ink_panel flash

# 或先再打一遍包再烧：
.\nrf.cmd -Project ink_panel release 0.4.0
.\nrf.cmd -Project ink_panel flash
```

期望 RTT：`ink_panel boot` / `ink_panel ok board=ink_panel`。

### 方式 B：只拿归档 hex（无源码树）

用 nRF Util / nRF Connect Programmer / J-Link 把该 **`.hex` 整片写入**（含 boot 区），目标板选 **nRF54L15**。

### 不要用

| 文件/操作 | 原因 |
|-----------|------|
| 只烧 `.bin` 到空白片 | 无 MCUBoot，无法正常启动本仓分区布局 |
| 其它工程的 factory/demo hex | 本版归档**只有** ink_panel |
| 旧版 `0.3.0` 全仓 hex 当本阶段桌牌 | 无本版软休眠与后续页槽/触摸改动 |

### 已有 MCUBoot、只更新 APP

仍可用 `.\nrf.cmd -Project ink_panel flash`（脚本按工程写 slot0）。若你确认板子已是本仓 boot 布局，用同版本 `.hex` 覆盖亦可。

---

## PC 联调工具

```powershell
cd tools\ink_panel_console
.\release\build.cmd
# 分发整个 release\dist\ink_panel_console\ 文件夹
```

或源码：`python main.py`（见该目录 README）。
