# factory

厂测（Factory）工程骨架。生命周期在 [`src/`](src/README.md)；串口命令在 [`lib/cmd/`](lib/cmd/)。

MCUBoot 已启用（共享配置见 [`bootloader/`](../../bootloader/README.md)）：双槽 + `boot_share`，开发阶段不验签。TY 板 UART：P1.09 / P1.10 @ 115200。仓库总览烧录表：[`README.md`](../../README.md)#烧录指令日常--厂测--量产。

**烧录约定**：`.\nrf.cmd -Project factory flash` **只写 slot1**（`0xB6000`），烧的是 **Direct XIP secondary 变体**（`mcuboot_secondary_app`，链到 slot1），不改 MCUBoot / slot0。板上须先有量产 APP 的 `flash`（Boot + image-0）。

改过 `sysbuild` / `pm_static` 后务必：

```powershell
.\nrf.cmd -Project factory fullclean
.\nrf.cmd -Project factory build-all
.\nrf.cmd -Project factory flash
```

## 烧录指令

均在仓库根目录执行。

### 1. 平时厂测调试烧录

slot0 已是合法 APP 时：

```powershell
.\nrf.cmd -Project factory build          # 快：只编 app（--domain）
.\nrf.cmd -Project factory flash          # signed.bin → slot1

# 改过 bootloader / sysbuild / pm 后：
.\nrf.cmd -Project factory build-all
.\nrf.cmd -Project factory flash
.\nrf.cmd boot-mode factory               # 下次起 slot1
```

### 2. 与量产 APP 联调（APP@slot0 + factory@slot1）

```powershell
.\nrf.cmd -Project demo build-all
.\nrf.cmd -Project demo flash                 # Boot + demo@slot0

.\nrf.cmd -Project factory build-all
.\nrf.cmd -Project factory flash              # factory → 0xB6000

.\nrf.cmd boot-mode factory                  # 下次起 slot1
# 复位 → Boot: factory→secondary；串口见 factory
```

期望串口：MCUBoot 横幅 → chainload → `factory printk ok ... slot=N bmode=M` / `factory heartbeat`。

### 3. 量产烧录

```powershell
.\nrf.cmd -Project factory release 0.0.1
# → firmware/0.0.1/nrf54l15_factory_0.0.1_YYYYMMDD_unsigned.{hex,bin,elf}

# 产线预置厂测到 slot1（slot0 已是量产 APP）：
.\nrf.cmd -Project factory flash
.\nrf.cmd boot-mode app                      # 出厂默认走 image-0
```

归档约定见 [`firmware/README.md`](../../firmware/README.md)。OTA 才擦/覆盖 slot1。

## 串口与按键

串口命令：`help` / `ftmexit`（清旁路并复位进 image-0，**不擦 slot1**）/ `reboot` / `sn` / `version`。

按键 P0.00（`BTN_PERMISSION_FTM`）：连击 **5 → 4 → 3 → 2** 出厂测（同 `ftmexit`，不擦 slot1）。任一步次数不对或步间超过 8s，从 5 连击重来。序列实现在 `common` 的 `button_ftm_seq_poll()`（经 `start_poll`）。

`app_init` 按 NVS `did` 分支硬件初始化（`NVS_PROJECT_ID_EINK_TAG` / `NVS_PROJECT_ID_DEMO` / 其它走 default stub）。

厂测旁路步骤见 [`doc/plan_factory_slot1_bypass.md`](../../doc/plan_factory_slot1_bypass.md)。
