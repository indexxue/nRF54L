# nRF54L15 RRAM 分区布局（已锁定）

应用核可用 NVM：**1428 KB RRAM**（片内合计 1524 KB，尾部 **96 KB** 预留给 FLPR，应用不占用）。  
擦除粒度 4 KB，写入粒度 16 B。地址为应用核 RRAM 内偏移（与 Nordic `nrf54l15_partition.dtsi` 默认一致）。

## 分区表

| 偏移 | 结束前 | 大小 | 标签 | 用途 |
|------|--------|------|------|------|
| `0x000000` | `0x00FFFF` | 64 KB | `mcuboot` | Bootloader（MCUBoot） |
| `0x010000` | `0x0B5FFF` | 664 KB | `image-0` | 量产 APP 运行槽（slot0） |
| `0x0B6000` | `0x15BFFF` | 664 KB | `image-1` | OTA 暂存 ∪ 厂测（slot1，共用） |
| `0x15C000` | `0x164FFF` | 36 KB | `storage` | Settings / NVS（含 `boot_mode` 等） |
| `0x165000` | `0x17CFFF` | 96 KB | FLPR | 协核预留，**本布局不并入应用** |

```
0x000000  mcuboot     64 KB
0x010000  image-0     664 KB   ← 量产 APP
0x0B6000  image-1     664 KB   ← OTA staging ∪ factory
0x15C000  storage      36 KB
0x165000  FLPR         96 KB   ← reserved
```

## 写权限与激活

| 角色 | 允许写 | 禁止 |
|------|--------|------|
| 运行中 APP | `image-1`、`storage` | Boot、`image-0` |
| Bootloader | 槽切换 / 激活（swap）、按需读 `storage` | — |
| 产线 SWD | 可整片或按槽烧录 | — |

- OTA 激活：**swap move / offset**，**无**独立 `scratch` 分区。
- 进厂测：Boot 读 `storage` 中标志（如 `boot_mode=factory`）→ 从 `image-1` 启动；与 OTA 暂存互斥，产线/厂测结束需清标志并保证 slot1 内容符合预期。
- 镜像验签：开发阶段可不强制；量产再开。Boot 64 KB 按可开 ECDSA 验签预留。

## 实现状态

| 项 | 状态 |
|----|------|
| 布局决策 | **已锁定**（见 [`decisions.md`](decisions.md)） |
| C 常量头 | [`common/include/flash_partition.h`](../common/include/flash_partition.h) |
| Settings / NVS | 应用层 `common` nvs → Zephyr Settings；预 MCUBoot 用工程 `pm_static.yml`（`settings_storage` @ `0x15C000`，36 KB） |
| `pm_static.yml` / 启用 MCUBoot | **未做双槽** — 当前仅 `app` + `settings_storage`；接 OTA 时换成完整双槽表 |
| 板级硬件烧录验证 | 仍按 [`plan.md`](plan.md) 暂缓 |

参考：NCS Zephyr `dts/vendor/nordic/nrf54l15_partition.dtsi`。
