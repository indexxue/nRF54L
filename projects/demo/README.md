# demo

工程模板 + **TY rev_a 冒烟**（UART + `led_scene` bootup + 内部 LFCLK RC）+ **FLPR（RISC-V）双核** + **MCUBoot（slot0）**。

| 功能 | 引脚 / 核 |
|------|-----------|
| UART TX / RX（应用核控制台） | **P1.04 / P1.05**（uart20，115200） |
| LED0 / LED1 | **P2.08 / P2.09**（ACTIVE_LOW） |
| FTM 按键 | P0.00（`BTN_PERMISSION_FTM`；连击 5→4→3→2 进厂测） |
| 时钟 | LF=内部 RC；**HF=外部 32 MHz HFXO**（`CONFIG_START_REQUEST_HFXO=y`） |
| **MCUBoot** | 本工程跑在 **image-0 / slot0**；厂测镜像另烧 **image-1** |
| **FLPR（RISC-V）** | 片内协处理器 `cpuflpr`；镜像存 RRAM `0x165000`，在 SRAM 执行；控制台默认 **uart30** |

板级：[`board/app.overlay`](board/app.overlay) + [`board/board_config.c`](board/board_config.c)。MCUBoot 共享配置：[`bootloader/`](../../bootloader/README.md)。仓库总览烧录表：[`README.md`](../../README.md)#烧录指令日常--厂测--量产。

## 烧录指令

均在仓库根目录执行。

### 1. 平时项目调试烧录

```powershell
.\nrf.cmd -Project demo build          # 快编：只编 app domain
.\nrf.cmd -Project demo flash          # 编 app → merged.hex → SWD（MCUBoot + demo@slot0）

# 改过 bootloader / sysbuild / pm_static 后：
.\nrf.cmd -Project demo fullclean
.\nrf.cmd -Project demo build-all
.\nrf.cmd -Project demo flash
```

期望串口：`demo printk ok ... image-0` / `demo heartbeat`。

### 2. 厂测调试烧录（demo@slot0 + factory@slot1）

```powershell
.\nrf.cmd -Project demo fullclean
.\nrf.cmd -Project demo build-all
.\nrf.cmd -Project demo flash                 # MCUBoot + demo@slot0（M33；不刷 FLPR RV32）

.\nrf.cmd -Project factory build-all
.\nrf.cmd -Project factory flash              # Direct XIP secondary → slot1（不动 slot0）

.\nrf.cmd boot-mode factory                   # 写 boot_share，下次起 slot1
# 复位：Boot factory→secondary 后应见 factory printk（非 MCUBoot 死循环）
```

仅改厂测、slot0 已是 demo 时：`factory build` → `factory flash` → `boot-mode factory`。

### 3. 量产烧录

```powershell
.\nrf.cmd -Project demo release 0.0.1
# → firmware/0.0.1/nrf54l15_demo_0.0.1_YYYYMMDD_unsigned.{hex,bin,elf}

.\nrf.cmd -Project demo flash                 # 产线 SWD 等价：Boot + APP@slot0
# 若产线预置厂测：再 factory release + factory flash，最后 boot-mode app
```

归档约定见 [`firmware/README.md`](../../firmware/README.md)。

## 这和「换成 RISC-V 主核」不是一回事

nRF54L15 **主应用核仍是 Cortex-M33**（`cpuapp`）。片内还有一颗很小的 **RISC-V VPR**，Nordic 叫它 **FLPR**。

| 概念 | 说明 |
|------|------|
| **代码放哪** | 主核：`src/` + `lib/` + 仓库 `start/`/`common/`；协核：[`remote/`](remote/) |
| **怎么编** | [`sysbuild.cmake`](sysbuild.cmake)：MCUBoot + cpuapp + remote；[`SNIPPET`](SNIPPET)=`nordic-flpr` |
| **Flash 布局** | 双槽 + settings + boot_share；其后 **96 KB** FLPR（[`doc/flash_layout.md`](../../doc/flash_layout.md)） |

## 必含文件

| 文件 | 作用 |
|------|------|
| `CMakeLists.txt` | `EXTRA_ZEPHYR_MODULES`（common + start）+ overlay |
| `prj.conf` | UART / GPIO / RC / Settings（32 KiB） |
| `pm_static.yml` | 双槽 + boot_share（与 bootloader 一致） |
| `sysbuild.conf` / `sysbuild.cmake` | MCUBoot + FLPR remote |
| `SNIPPET` | `nordic-flpr` |
| `remote/` | FLPR 侧固件 |

不要提交 `build/`。从预 MCUBoot 切过来务必 `fullclean` 一次。
