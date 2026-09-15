# 计划：factory → slot1 厂测旁路冒烟

> 前置已完成：`factory` 启用 MCUBoot；Boot → **slot0** chainload 已在 TY UART（P1.09/P1.10）验收。  
> 行为约定：[`flash_layout.md`](flash_layout.md)；共享配置：[`bootloader/`](../bootloader/README.md)。

## 目标

验证 **`boot_mode=factory`** 时 MCUBoot `boot_go_hook` 从 **slot1（`mcuboot_secondary` @ `0xB6000`）** 启动厂测镜像，**不 swap**；出厂测后清标志并无效化 slot1，再复位回到 slot0。

## 门禁（开干前对齐）

```text
【需求】串口/工具置 boot_mode=factory → 复位起 slot1 factory；清标志+擦 slot1 → 回 slot0
【上下文】Boot：boot_go_hook 读 boot_share；APP：串口 cmd / 产线 SWD；非 GATT
【能否阻塞】Boot 内可读 Flash；APP 写 boot_share 可阻塞短时；失败则仍起 slot0 或卡 Boot（须可观察）
【队列/锁】无新队列；写 boot_share 与 Settings 双写沿用 nvs_boot_mode_set
【Flash】事件驱动写 boot_share（末页 0x164000）；出厂测擦 secondary（664KB）或仅破坏 header
【共存】本里程碑不涉及 BLE/刷屏
【验收】串口见 Boot 日志含 factory→secondary；APP 可区分 slot0/slot1；出厂测后回 slot0
【合入】可合 main；调试 cmd 可留量产（厂测工程）
```

## 当前基线

| 项 | 状态 |
|----|------|
| 双槽 `pm_static` + `boot_share` | 已有（`bootloader/pm_static.yml` / factory） |
| `nvs_boot_mode_set` 双写 | 已有（`common/src/nvs.c`） |
| `boot_go_hook` | 已有；读 magic/CRC/`boot_mode==factory` → 校验 secondary → 起 slot1 |
| Boot → slot0 | **已板测通过**（现改为 demo@slot0） |
| slot1 有合法 factory 镜像 | **工具已做**（`factory flash` → slot1） |
| 串口出厂测 | **已做**：`ftmexit` |
| 主机置旁路 | **已做**：`nrf.cmd boot-mode factory\|app` |
| demo MCUBoot | **已接**（与 FLPR 同 sysbuild；须 `fullclean`） |

## 实施步骤

### A. 串口命令（factory） — 已完成（`ftmexit`）

- `ftmexit`：`nvs_boot_mode_set(app)` 后冷复位；Boot 走常规路径起 **image-0 / slot0**。
- 进厂测旁路仍靠产线/SWD 置 `boot_mode=factory`（或后续工具），不再提供串口 `bmode`。
- 写失败回 `ng`（`nvs` 已打 Settings / `boot_share` 错误日志）。

### B. 准备 slot1 镜像 — 工具已完成（待板测烧录）

产线等价最小路径（开发机）：

```powershell
.\nrf.cmd -Project factory build
.\nrf.cmd -Project factory flash          # zephyr.signed.bin → slot1 @ 0xB6000
```

1. 正常编 `factory` → `build/factory/zephyr/zephyr.signed.bin`。
2. `factory flash`：J-Link `LoadFile` 将该 bin 写到 **`0xB6000`**（不改写 boot/slot0）。
3. slot0 须已有量产 APP（如 `demo flash`）。

说明：factory 以 **Direct XIP secondary 变体**链接到 `0xB6000`（`mcuboot_secondary_app`）；设备上 Boot 仍为 demo 的 swap-move + `boot_go_hook`。`factory flash` 烧 secondary signed.bin。

## 验证步骤（demo@image-0 + factory@image-1）

```powershell
.\nrf.cmd -Project demo fullclean
.\nrf.cmd -Project demo build-all
.\nrf.cmd -Project demo flash
.\nrf.cmd -Project factory build-all
.\nrf.cmd -Project factory flash
.\nrf.cmd boot-mode factory
# 复位 → Boot: boot_mode=factory -> secondary；串口 factory
# 串口 ftmexit → 回 demo（image-0）
```

### C. 进厂测

1. `boot-mode factory`（或产线 SWD 等效写 `boot_share`）→ 复位。  
2. Boot 日志期望含：`boot_mode=factory -> secondary (no swap)`（或等价）。  
3. APP 起来后串口见 **factory**（非 demo heartbeat）。

### D. 出厂测

1. 串口：`ftmexit`（双写 `boot_mode=app` 并立刻冷复位）。  
2. 可选：擦除或无效化 slot1（至少破坏 `0xB6000` 处 image magic）。  
3. Boot 走常规路径 → **slot0 / image-0（demo）**。

### E. 文档回写

通过后更新 [`bootloader/README.md`](../bootloader/README.md)「This stage vs next」：旁路板测勾掉；[`flash_layout.md`](flash_layout.md) 实现状态表补一行。

## 验收清单

- [ ] `boot-mode factory` + 复位 → Boot 日志明确 secondary / factory 旁路；串口 factory  
- [ ] slot1 无合法镜像时旁路失败 → 回退 primary（不砖）  
- [ ] `ftmexit` → 回到 demo / image-0  
- [ ] UART 仍为 P1.09/P1.10 @ 115200；LFCLK RC / 无 GRTC Boot 配置保持  

## 非本计划范围

- `eink_tag` / `demo` 启用 MCUBoot  
- BLE OTA 写 slot1、延迟 confirm  
- 量产签名 / FPROTECT  

## 完成计划后删除本文档

本文件为**一次性执行计划**。当上述验收清单全部勾完，且 `bootloader/README.md` / `flash_layout.md` 状态已更新后：

**删除本文件：`doc/plan_factory_slot1_bypass.md`。**

长期约定只保留在 `flash_layout.md` 与 `bootloader/README.md`，避免计划文档过期残留。
