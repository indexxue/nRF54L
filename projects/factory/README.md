# factory

厂测（Factory）工程。生命周期在 [`src/`](src/README.md)；串口命令在 [`lib/cmd/`](lib/cmd/)；Zigbee 临时双机通道在 [`lib/zigbee/`](lib/zigbee/)（`zbft`）。

MCUBoot 已启用（共享配置见 [`bootloader/`](../../bootloader/README.md)）：双槽 + `boot_share`，开发阶段不验签。TY 板 UART：**P1.04 / P1.05** @ 115200。仓库总览烧录表：[`README.md`](../../README.md)#烧录指令日常--厂测--量产。

**烧录约定**：`.\nrf.cmd -Project factory flash` **只写 slot1**（`0xB6000`），烧的是 **Direct XIP secondary 变体**（`mcuboot_secondary_app`，链到 slot1），不改 MCUBoot / slot0。板上须先有量产 APP 的 `flash`（Boot + image-0）。

改过 `sysbuild` / `pm_static` 后务必：

```powershell
.\nrf.cmd -Project factory fullclean
.\nrf.cmd -Project factory build-all
.\nrf.cmd -Project factory flash
```

计划与门禁：[`doc/plan_zigbee_factory_temp_coord.md`](../../doc/plan_zigbee_factory_temp_coord.md)。

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

期望串口：MCUBoot 横幅 → chainload → `factory printk ok ... slot=N bmode=M`；心跳约 **60s** 一次。

### 3. 量产烧录（现阶段）

```powershell
.\nrf.cmd -Project factory release 0.0.1
# → firmware/0.0.1/nrf54l15_factory_0.0.1_YYYYMMDD_unsigned.{hex,bin,elf}

# 产线预置厂测到 slot1（slot0 已是量产 APP）：
.\nrf.cmd -Project factory flash
.\nrf.cmd boot-mode app                      # 出厂默认走 image-0
```

**量产目标形态（后续）**：整包 hex = **bootloader + app + factory**；现阶段仍按上面「slot0 APP + slot1 factory」分别烧，不阻塞厂测 Zigbee 通道。

归档约定见 [`firmware/README.md`](../../firmware/README.md)。OTA 才擦/覆盖 slot1。

---

## Zigbee 临时双机通道（TOOL / UUT）

**目的**：两台都烧**同一份** factory hex，不拆机、不靠外部协调器，临时组网后互读厂测命令（`zbcmd`）。

**边界（重要）**：

| 项 | 约定 |
|----|------|
| 谁可以 | **仅** `projects/factory` 厂测镜像 |
| 量产 | `zigbee_ed` / `zigbee_router` **禁止**本机 Coord / 本通道 |
| 产物 | **单 hex**；禁止 `_coord` / `_dut` 双包；不新建 `zigbee_factory` |
| 角色 | TOOL=临时 Coord；UUT=被测（实现为 **Router**，因单 hex 非 ED 库） |
| 上电 | 清 zboss 网 NV；**保留** Settings（sn 等）；默认 idle（无 RF） |
| 重启 | 临时网与 allow 会话**不**恢复；须重新组网 |

### 最简双机步骤

两板都已 `factory flash` 且 boot-mode=factory：

```text
B: zb uut
   → 回 ieee=<16hex>（记下）

A: zb tool
   → formed（首次无复位）

A: zb join <B的ieee>
   → allow + 开窗 60s
   → A: dut joined；B: uut joined

A: zbcmd version
A: zbcmd sn
A: zbcmd log
```

可选：起栈前 `zb ch 11..26`（默认 15）；`zb pwr <-40..+8>`（默认 +8，不写 Settings）；组网并有包之后 `zb rssi`；`zb allow sn <v>` 后 `zbcmd sn` 不符只告警。

### 相关命令

| 命令 | 说明 |
|------|------|
| `zb tool` / `zb coord` | 进 TOOL，form；不自动开窗 |
| `zb uut` / `zb dut` | 进 UUT；直接回 ieee |
| `zb status` | mode / ch / pwr / pan / nwk / ieee / allow / joined / sn |
| `zb ch [11..26]` | 起栈前改信道；已起栈 → busy |
| `zb pwr [<-40..+8>]` | 读/设 TX 功率 dBm（会话默认 +8；起栈前后均可设） |
| `zb rssi` | 对端最近 RSSI/LQI（TOOL→DUT，UUT→parent；无包 → ng） |
| `pwr` / `rssi` | 同上，**可空中代理**（`zbcmd pwr` / `zbcmd rssi`） |
| `zb allow ieee\|sn\|clear` | 白名单（容量=1）；ieee 在 formed 后自动开窗 |
| `zb join <ieee>` / `zb join [sec]` | 最简 allow+开窗；或重开秒数 |
| `zbcmd <factory-line>` | 代理到已 allow 且 joined 的 DUT；失败 `zbcmd:ng` |

`zb` / `zbcmd` / `ccfg` 为 UART `[local]`，不可被空中代理递归。

---

## 串口与按键

本地厂测：`help` / `version` / `sn` / `mac` / `led` / `btn` / `log` / `reboot` / `pwr` / `rssi` / `ftmexit`（清旁路并复位进 image-0，**不擦 slot1**）/ `ccfg`。

按键 P0.00（`BTN_PERMISSION_FTM`）：连击 **5 → 4 → 3 → 2** 出厂测（同 `ftmexit`，不擦 slot1）。任一步次数不对或步间超过 8s，从 5 连击重来。序列实现在 `common` 的 `button_ftm_seq_poll()`（经 `start_poll`）。

`app_init` 按 NVS `did` 分支硬件初始化（`NVS_PROJECT_ID_EINK_TAG` / `NVS_PROJECT_ID_DEMO` / 其它走 default stub）。

厂测旁路步骤见 [`doc/plan_factory_slot1_bypass.md`](../../doc/plan_factory_slot1_bypass.md)。
