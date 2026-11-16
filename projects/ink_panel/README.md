# ink_panel

EVT 自有板 bring-up。脚手架来自 `zigbee_ed`。

**当前临时配置（无外部晶振 / 无射频）**：仅 UART + LED 心跳，HF=HFINT，LF=RC。串口通后再开 Zigbee/HFXO。

规格：[`docs/superpowers/specs/2026-11-12-ink-panel-design.md`](../../docs/superpowers/specs/2026-11-12-ink-panel-design.md)

| 层 | 路径 |
|----|------|
| 产品（按键/灯效/日志） | `src/app.c` |
| Zigbee 协议（本工程私有） | `lib/zigbee/` |

## 板级（EVT 网表）

| 功能 | 网名 | 引脚 |
|------|------|------|
| UART TX | `UART_TX` | **P2.02**（手册专用 UARTE TX；**不是**原理图误标的 P2.01） |
| UART RX | `UART_RX` | **P2.00**（UARTE RX；外接 TTL TX；只看日志可不接） |
| LED_STATUS | `LED_STATUS_N` | **P1.13** ACTIVE_LOW |
| BTN1 | `BTN_USER_N` | **P0.00** |
| BTN2 | — | **未接**（勿占用 `TOUCH_ALERT` P0.01） |

| Zigbee | 值 |
|--------|-----|
| 扫描掩码 | `0x02108800`（11/15/20/25） |
| Basic Model | `ink_panel` |
| OnOff EP1 | 联调探针，非产品语义 |

## 按键 / LED

| 操作 | 行为 |
|------|------|
| BTN1 短按 | 60s 配网窗 + Network Steering |
| BTN1 长按 ≈5s | `boot_mode=factory` + 冷复位（须 slot1 已烧 factory） |
| leave / 擦 NVRAM | 本轮无 BTN2；用调试器擦或后轮补命令 |

## 构建 / 烧录

```powershell
.\nrf.cmd -Project ink_panel fullclean
.\nrf.cmd -Project ink_panel build-all
.\nrf.cmd -Project ink_panel flash

# 日常只改 APP：
.\nrf.cmd -Project ink_panel build
.\nrf.cmd -Project ink_panel flash
```

依赖与 `zigbee_ed` 相同：`NCS_ZIGBEE_ROOT` → ncs-zigbee v1.4.0。

期望串口：`ink_panel boot` / `ink_panel ok` / `zigbee: steering`（BTN1 后）。

墨水屏：`prj.conf` 里 `CONFIG_INK_PANEL_EPD=n`（暂关，不编 epd、不 smoke）。要开屏改 `y` 后重编。

NFC（片内 NFCT T2T）：`CONFIG_INK_PANEL_NFC=y`。期望串口 `nfc: bringup ok`；手机靠近见 `nfc: field on` / `data read` / `field off`；读到 Text `ink_panel NFC ok`。关改 `n`。
