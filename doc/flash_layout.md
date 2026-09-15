# nRF54L15 RRAM 分区布局（已锁定）

应用核可用 NVM：**1428 KB RRAM**（片内合计 1524 KB，尾部 **96 KB** 预留给 FLPR，应用不占用）。  
擦除粒度 4 KB，写入粒度 16 B。地址为应用核 RRAM 内偏移（与 Nordic `nrf54l15_partition.dtsi` 默认一致）。

## 分区表

| 偏移 | 结束前 | 大小 | 标签 | 用途 |
|------|--------|------|------|------|
| `0x000000` | `0x00FFFF` | 64 KB | `mcuboot` | Bootloader（MCUBoot） |
| `0x010000` | `0x0B5FFF` | 664 KB | `image-0` | 量产 APP 运行槽（slot0） |
| `0x0B6000` | `0x15BFFF` | 664 KB | `image-1` | OTA 暂存 ∪ 厂测（slot1，共用） |
| `0x15C000` | `0x163FFF` | 32 KB | `settings_storage` | Settings / NVS（应用键值） |
| `0x164000` | `0x164FFF` | 4 KB | `boot_share` | Boot ↔ APP 旁路（`boot_mode` 等） |
| `0x165000` | `0x17CFFF` | 96 KB | FLPR | 协核预留，**本布局不并入应用** |

`storage` 在布局上仍占 `0x15C000`–`0x164FFF`（36 KB），内部分为 Settings 32 KB + 末页旁路 4 KB。

```
0x000000  mcuboot          64 KB
0x010000  image-0         664 KB   ← 量产 APP（swap 后运行槽）
0x0B6000  image-1         664 KB   ← OTA staging ∪ factory
0x15C000  settings_storage 32 KB
0x164000  boot_share        4 KB   ← 末页旁路（Boot 只读此页判厂测）
0x165000  FLPR             96 KB   ← reserved
```

## 写权限与激活

| 角色 | 允许写 | 禁止 |
|------|--------|------|
| 运行中 APP | `image-1`、`settings_storage`、`boot_share` | Boot、`image-0` |
| Bootloader | 槽切换 / 激活（swap）、读 `boot_share`；按 MCUBoot 规则写 trailer | — |
| 产线 SWD | 可整片或按槽烧录 | — |

## Bootloader 行为（设计已拍板，实现后置）

与 [`decisions.md`](decisions.md) 一致；细节以本节为准。MCUBoot **尚未**在 v1 构建中启用。

### 运行与激活

- **Swap move**：OTA 完成后 Boot 搬迁激活；量产运行地址固定在 **slot0**。无独立 `scratch`。
- **必须 confirm**：新镜像首次为 test；APP 自检通过后 confirm；未 confirm 则下次复位 **回滚**到旧镜像。厂测旁路路径不做 OTA confirm/swap。
- **验签**：开发可不强制；量产开 ECDSA（或 NCS 默认）；密钥不进 Git。Boot 64 KB 按可开验签预留。
- **OTA 写入**：运行中 APP 经 **自定义 BLE CMD** 写 `image-1`，完成后请求升级并复位；不上 SMP/MCUMgr 作量产通道。研发可用 SWD 烧 slot1 单测 Boot。

### `boot_mode` 与厂测

- APP：`nvs_boot_mode_set/get` 双写 Settings + `boot_share`（`magic + version + boot_mode + crc`，16 B 对齐）。
- Boot：只读 `boot_share`，不解析 Zephyr Settings。
- **硬互斥**：
  - `boot_mode=factory` → 从 **slot1** 启动（不 swap）；禁止 OTA 写 slot1 / pending-swap。
  - `boot_mode=app` → 走 MCUBoot 正常 swap/slot0；允许 OTA。
- **产线（主路径）**：SWD 一次烧 `mcuboot` + slot0(APP) + slot1(factory) + 置 `boot_mode=factory` → 厂测 → 出厂测。
- **返修/调试（预留）**：量产 APP 提供入口（按键或其他）置 `boot_mode=factory` 并复位；调用方须保证 slot1 已是合法 factory。
- **出厂测**：清 `boot_mode=app`，并 **擦除或无效化 slot1**；再进厂测须重新准备 slot1。

### OTA 失败 / 重启环备案

目标：坏包或「能起来但功能残缺、无法再 OTA」时，仍能回到可升级状态。

| 场景 | 备案 |
|------|------|
| Swap 后反复复位，**尚未 confirm** | MCUBoot **自动回滚**到旧 slot0 → 可再 OTA。主备案。 |
| APP **过早 confirm**，随后重启环 / BLE 挂死 | 单靠 confirm 无效。需 **延迟 confirm**（见下）+ 可选开机计数救援。 |
| 已 confirm 且旧镜像已被 move 换掉 | 无第二好包可回。依赖：SWD；或 APP/按键进厂测（须先有 factory 在 slot1）；或 Boot 层救援策略（实现时可选）。 |

**延迟 confirm（量产必做）**：不要在 `main` 入口立刻 confirm。至少等到：BLE 协议栈就绪、可连接、OTA/CMD 通道可用（可再加短稳定窗口）。自检失败则故意不 confirm，靠复位回滚。

**可选加强（实现 MCUBoot 时再定是否做）**：在 `boot_share` 增加开机计数；连续 N 次异常复位且未完成「健康确认」时，Boot 强制回滚或进入救援（例如要求 slot1 为 factory）。与「出厂测擦 slot1」不冲突：字段救援优先靠 **未 confirm 回滚**；计数救援是 confirm 之后的第二道闸。

## 实现状态

| 项 | 状态 |
|----|------|
| 布局决策 | **已锁定**（见 [`decisions.md`](decisions.md)） |
| Boot 行为决策 | **已锁定**（本节）；`factory` 已接 MCUBoot + `boot_go_hook`（读 `boot_share`，factory → slot1） |
| 共享配置目录 | [`bootloader/`](../bootloader/)；**`factory` 已启用**；`demo` / `eink_tag` 仍预 MCUBoot |
| C 常量头 | [`common/include/flash_partition.h`](../common/include/flash_partition.h)；旁路见 [`bootloader/include/boot_share.h`](../bootloader/include/boot_share.h) |
| Settings / NVS | `nvs_boot_mode_set` **双写** Settings + `boot_share`（无 `boot_share` 分区时旁路 no-op） |
| `pm_static.yml` / 启用 MCUBoot | **`factory` 已接双槽**；其余工程未接 |
| 板级硬件烧录验证 | 仍按 [`plan.md`](plan.md) 暂缓 |

参考：NCS Zephyr `dts/vendor/nordic/nrf54l15_partition.dtsi`。
