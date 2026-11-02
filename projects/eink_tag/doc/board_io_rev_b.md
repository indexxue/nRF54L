# eink_tag — 整板 IO 配置（rev_b）

状态：**自有 PCB 脚表（rev_b）**；在现有 UART / LED / 按键 / EPD / BLE 基础上 **重排 IO 并加 PN5180**，便于分区布线。  
**`app.overlay` / `board_config.c` / README 已按本表合入**；PN5180 / ADC / IRQ **业务未挂**。现网冒烟板（rev_a）**勿烧**本 overlay。  
对照：[`cbb/pn5180`](../../../cbb/pn5180/README.md)、STM32 [`pn5180_nfc_interface.md`](../../../../STM32F205VCT6/doc/pn5180_nfc_interface.md)、[`doc/dev_norms.md`](../../../doc/dev_norms.md)。  
封装 **nRF54L15 QFN48**。可用 [nRF Pin Planner](https://pinplanner.app/)（非官方；MCU=`nRF54L15` / Package=`QFN48` / Devkit=`Custom Board`）；终值以 Nordic **PS → Pin assignments** 为准。

原文件名 `pn5180_io.md` 已并入本文；PN5180 仅为新增器件，非唯一主题。

---

## 1. 目标与边界

| 做 | 不做（本阶段） |
|----|----------------|
| 整板脚表：既有外设重排 + 新增 PN5180 | 改 `ble_gatt.c` / BLE 协议 |
| 按 QFN48 簇脚方便 PCB 分区 | 量产天线调谐终值 |
| 评估 PN5180 与 EPD / BLE 共存及软件分层 | ISO15693/14443 完整协议栈 |
| 新板 overlay 已合入 | 把本表烧到现网冒烟板 |

本工程 **不用片上 NFCT Tag**；读卡只走外挂 **PN5180**。P1.02 / P1.03 不作天线、不作 SPI。

---

## 2. IO 配置（rev_b）

布线原则（QFN48 按 PS 脚号簇在同一边）：

| 簇 | QFN 大致边 | 用途 |
|----|------------|------|
| P1.00–P1.01 | pin 1–2 | **只留 LFXO**，不接 SPI |
| P2.00–P2.05 | pin 11–16 | **EPD FPC**：`spi00` 专用脚，短等长 |
| P1.06 | pin 7 | **ADC AIN2**（电池分压等） |
| P1.07–P1.08 + P1.11–P1.14 | pin 8–9、39–42 | **PN5180**：SPI 用相邻 clock 脚 P1.11 |
| P0.00–P0.02 | pin 23–27 | 按键 + NFC **IRQ**（GPIOTE30，可 System OFF 唤醒） |
| P1.04–P1.05 | pin 5–6 | UART 调试座 |
| P2.08–P2.09 | pin 19–20 | LED（慢速 GPIO，P2 无中断无妨） |
| 2.4G ANT / HFXO / SWD | 专用脚 | 不占用 GPIO |

Pin Planner 加外设时：`UARTE20`、`SPIM00`、`SPIM22`；SCK 必须落到工具标成 **Clock Pin** 的脚（本表：P2.01、P1.11）。

### 2.1 整板信号表

| 功能 | 信号 | nRF54L15 | 方向 | 极性 / 备注 |
|------|------|----------|------|-------------|
| 时钟 | HFXO | 专用 | — | 外部 32 MHz；片内负载 15 pF |
| 时钟 | LFXO XL1/XL2 | **P1.00 / P1.01** | — | **预留晶振焊盘，默认 NC**；软件 LFCLK=RC |
| 调试 | SWDCLK / SWDIO / nRESET | 专用 | — | 测试点 |
| 射频 | BLE ANT | RADIO | — | 与 13.56 MHz 线圈分区、正交或远离 |
| UART20 | TX | **P1.04** | O | 115200；自有板调试座（不再迁就 DK） |
| UART20 | RX | **P1.05** | I | 内部上拉 |
| LED | LED_G | **P2.08** | O | ACTIVE_LOW |
| LED | LED_B | **P2.09** | O | ACTIVE_LOW |
| 按键 | BTN1 | **P0.00** | I | ACTIVE_LOW；WAKE |
| 按键 | BTN2 | **P0.01** | I | ACTIVE_LOW；WAKE |
| EPD `spi00` | SCK | **P2.01** | O | SPIM00 **clock pin**；E0E1 |
| EPD | MOSI | **P2.02** | O | |
| EPD | MISO | **P2.04** | I | 屏可不接；DT 占位防浮空 |
| EPD | CS | **P2.05** | O | ACTIVE_LOW，`cs-gpios` |
| EPD | DC | **P2.00** | O | 同簇；脚功能含 SPI.DCX |
| EPD | RST | **P2.03** | O | |
| EPD | BUSY | **P2.07** | I | 轮询；P2 无 GPIOTE |
| PN5180 `spi22` | SCK | **P1.11** | O | SPIM22；P1 **clock pin** |
| PN5180 | MOSI | **P1.12** | O | 紧贴 SCK（PS §8.8.5） |
| PN5180 | MISO | **P1.13** | I | 全双工必接 |
| PN5180 | NSS | **P1.14** | O | ACTIVE_LOW，`cs-gpios` |
| PN5180 | IRQ | **P0.02** | I | 下降沿；**禁止 P2** |
| PN5180 | BUSY | **P1.08** | I | 高=忙；上拉；可轮询（P1 亦可中断） |
| PN5180 | RST | **P1.07** | O | ACTIVE_LOW，≥ 10 μs |
| PN5180 | 电源 | — | — | **常供电**（无 MCU PWR_ON）；睡眠靠软件 `rf_off` |
| ADC | AIN2 | **P1.06** | I | SAADC；建议电池分压 + 滤波；勿超 VDD |

总线：**EPD=`spi00`（P2）**，**PN5180=`spi22`（P1）**，禁止共线。

### 2.2 空闲脚（rev_b）

| 脚 | 建议 |
|----|------|
| P1.02 / P1.03 | 原 NFCT；**禁止接 13.56 天线**；可测点 / 备用 GPIO |
| P1.09 / P1.10 | 备用（I2C/第二 UART 等） |
| P0.03 / P0.04 | 备用唤醒；P0.04 兼 GRTC 32k 输出，默认当 GPIO |
| P2.06 / P2.10 | 备用；P2.06 为 TRACECLK，量产勿开 TRACE |

### 2.2.1 相对 rev_a / 现网冒烟板

现网 `app.overlay` **不要**按本表烧到旧板。差异：UART 从 P1.09/10 改到 P1.04/05；LED 改到 P2；EPD DC/RST/BUSY 从 P1 收到 P2 同边；PN5180 不占 XL1/XL2；**P1.06 = ADC**，PN5180 常供电。

### 2.3 Overlay 落点（已合入；勿烧旧板）

`board/app.overlay` + README + `board_config.c` 已按 §2.1；C 侧 `GPIO_DT_SPEC_GET` / `cs-gpios`，禁止业务硬编码脚号。

```dts
/* 与 board/app.overlay 对齐；整板 rev_b */
&pinctrl {
	uart20_default: uart20_default {
		group1 {
			psels = <NRF_PSEL(UART_TX, 1, 4)>;
		};
		group2 {
			psels = <NRF_PSEL(UART_RX, 1, 5)>;
			bias-pull-up;
		};
	};

	/* EPD：SCK=P2.01 MOSI=P2.02 MISO=P2.04 */
	spi00_default: spi00_default {
		group1 {
			psels = <NRF_PSEL(SPIM_SCK, 2, 1)>,
				<NRF_PSEL(SPIM_MOSI, 2, 2)>,
				<NRF_PSEL(SPIM_MISO, 2, 4)>;
			nordic,drive-mode = <NRF_DRIVE_E0E1>;
		};
	};

	spi22_default: spi22_default {
		group1 {
			psels = <NRF_PSEL(SPIM_SCK, 1, 11)>,
				<NRF_PSEL(SPIM_MOSI, 1, 12)>,
				<NRF_PSEL(SPIM_MISO, 1, 13)>;
		};
	};
	spi22_sleep: spi22_sleep {
		group1 {
			psels = <NRF_PSEL(SPIM_SCK, 1, 11)>,
				<NRF_PSEL(SPIM_MOSI, 1, 12)>,
				<NRF_PSEL(SPIM_MISO, 1, 13)>;
			low-power-enable;
		};
	};
};

&spi00 {
	cs-gpios = <&gpio2 5 GPIO_ACTIVE_LOW>;
};

&spi22 {
	status = "okay";
	cs-gpios = <&gpio1 14 GPIO_ACTIVE_LOW>;
	pinctrl-0 = <&spi22_default>;
	pinctrl-1 = <&spi22_sleep>;
	pinctrl-names = "default", "sleep";
};

&led0 {
	gpios = <&gpio2 8 GPIO_ACTIVE_LOW>;
	label = "LED_G P2.08";
};

&led1 {
	gpios = <&gpio2 9 GPIO_ACTIVE_LOW>;
	label = "LED_B P2.09";
};

&nfct {
	status = "disabled";
};

/ {
	zephyr,user {
		btn1-gpios = <&gpio0 0 GPIO_ACTIVE_LOW>;
		btn2-gpios = <&gpio0 1 GPIO_ACTIVE_LOW>;

		epd-dc-gpios = <&gpio2 0 GPIO_ACTIVE_HIGH>;
		epd-rst-gpios = <&gpio2 3 GPIO_ACTIVE_HIGH>;
		epd-busy-gpios = <&gpio2 7 (GPIO_ACTIVE_HIGH | GPIO_PULL_UP)>;

		pn5180-busy-gpios = <&gpio1 8 (GPIO_ACTIVE_HIGH | GPIO_PULL_UP)>;
		pn5180-irq-gpios  = <&gpio0 2 GPIO_ACTIVE_LOW>;
		pn5180-rst-gpios  = <&gpio1 7 GPIO_ACTIVE_LOW>;
	};
};
```

`board_config.c`：`spi_buses[]` 增 `spi22`；`spi_slaves[]` 增 `"pn5180"`；可选 `BOARD_MASK_IRQ` 登记 P0.02；可选 `BOARD_MASK_ADC`。LED / UART / EPD GPIO 随 DT 走。PN5180 `set_pwr_on` 可不实现（常供电）。

ADC 在 DT 中示例（合入时再细化通道/增益）：

```dts
&adc {
	status = "okay";
	#address-cells = <1>;
	#size-cells = <0>;

	channel@2 {
		reg = <2>;
		zephyr,gain = "ADC_GAIN_1_6";
		zephyr,reference = "ADC_REF_INTERNAL";
		zephyr,acquisition-time = <ADC_ACQ_TIME_DEFAULT>;
		zephyr,input-positive = <NRF_SAADC_AIN2>; /* P1.06 */
	};
};
```

> AIN 脚勿配成 GPIO 输出。

---

## 2.4 硬件连接示意图（rev_b）

> 示意级原理图，**非**量产 Gerber 终值。电容/电感以 NXP PN5180 Datasheet + AN11740 为准；天线调谐需实测。  
> 电源方案对齐 STM32 侧 [`pn5180_nfc_interface.md`](../../../../STM32F205VCT6/doc/pn5180_nfc_interface.md) §4，MCU 脚为本工程 §2.1。

### 2.4.1 系统总览

```text
                         ┌─────────────────────────────────────────┐
   板级电源               │              nRF54L15                    │
   VIN / +5V             │                                         │
     │                   │   spi22          GPIO                   │
     │                   │   P1.11 SCK ─────────────┐              │
     │                   │   P1.12 MOSI ────────────┤              │
     │                   │   P1.13 MISO ◄───────────┤              │
     │                   │   P1.14 NSS  ────────────┤              │
     │                   │   P0.02 IRQ  ◄───────────┤              │
     │                   │   P1.08 BUSY ◄───────────┤              │
     │                   │   P1.07 RST  ────────────┤              │
     │                   │   P1.06 ADC(AIN2) ◄──────┘ 电池分压等   │
     │                   └──────────────────────────┘              │
     │                                                             │
     ├──────── LDO 3.3V (数字) ── VDD_3V3 ── MCU + PN5180 DVDD     │
     │                                                             │
     └──────── LDO 5V 或直接 +5V ── TVDD（常供电）── PN5180 TVDD   │
                           │                                       │
                           ▼                                       │
              ┌────────────────────────────────────────┐           │
              │              PN5180                     │           │
              │  SPI / BUSY / IRQ / RST / DVDD / TVDD │◄──────────┘
              │  TX1 / TX2 → 匹配网络 → 天线线圈        │
              └────────────────────────────────────────┘
```

### 2.4.2 电源树与去耦

推荐：**数字 3.3 V** 与 **天线 TVDD（建议 5 V）** 分轨；**TVDD/DVDD 常供电**（无 MCU `PWR_ON`）。睡眠关场用软件 `rf_off`。以后若要硬件掉电，可用 PN5180 **GPO1** 控 LDO（本阶段不做）。

```text
                         VIN（电池升压或 USB +5V，按整机定）
                              │
              ┌───────────────┴───────────────┐
              │                               │
              ▼                               ▼
     ┌─────────────────┐             ┌─────────────────┐
     │ LDO 3.3V        │             │ LDO 5.0V        │
     │ 例 XC6219-3.3   │             │ 例 TPS73601     │
     │ Cin 1u + Cout   │             │ EN 常使能/上拉  │
     │ 10u 靠近芯片    │             │ （无 MCU PWR_ON）│
     └────────┬────────┘             └────────┬────────┘
              │ VDD_3V3                       │ TVDD_5V
              │                               │
   ┌──────────┼──────────────┐     ┌──────────┼──────────┐
   │          │              │     │          │          │
   ▼          ▼              ▼     ▼          ▼          ▼
 nRF54L15  PN5180         PN5180  PN5180   100nF×N    10uF
  VDD      DVDD/VDD(TV)  VDDLDO   TVDD     每电源脚     bulk
           3.0~3.6V      (LDO_IN) 3~6V     就近对 GND
           ~80 mA        可接3.3V 峰值~350mA

 电池 ── 分压 ──► P1.06 (AIN2)   （分压后 ≤ VDD，加 100nF 滤波）
```

**简化方案（读距要求不高，&lt; ~4 cm）**  
TVDD 可直接接 **3.3 V**（与数字同轨），省掉 5 V LDO；常供电 + 软件 `rf_off`。

| 轨 | 电压 | 典型电流 | 接 PN5180 | 备注 |
|----|------|----------|-----------|------|
| VDD_3V3 | 3.3 V | 数字 ~80 mA | DVDD / VDD(TV)、VDDLDO_IN | 与 MCU 共地 |
| TVDD | 5 V（优）或 3.3 V | 峰值可达 ~350 mA | TVDD | 粗线 + 本地 bulk |
| GND | — | — | 所有 GND / EPAD | 星地或完整地平面，天线回流干净 |

**去耦（贴片紧贴 PN5180 电源脚）**

| 位置 | 建议 | 说明 |
|------|------|------|
| 每个 DVDD / TVDD 脚 | 100 nF X7R | 必贴 |
| TVDD 组 | 另加 1 μF + 10 μF | 抗开场浪涌 |
| VDDLDO / 内部 LDO 输出脚 | 按 Datasheet 推荐 C | 勿省 |

### 2.4.3 PN5180 周边：晶振、复位、接口

```text
                         nRF54L15                          PN5180
                    ┌──────────────┐                 ┌──────────────────┐
   spi22            │              │                 │                  │
   P1.11 ───────────┤ SCK      SCK ├─────────────────┤ SCK              │
   P1.12 ───────────┤ MOSI    MOSI ├─────────────────┤ MOSI             │
   P1.13 ◄──────────┤ MISO    MISO ├─────────────────┤ MISO             │
   P1.14 ───────────┤ NSS      NSS ├─────────────────┤ NSS (输入,低有效) │
   P0.02 ◄──────────┤ IRQ      IRQ ├─────────────────┤ IRQ (下降沿)     │
                    │              │                 │                  │
   P1.08 ◄── 10k↑ ──┤ BUSY    BUSY ├─────────────────┤ BUSY (高=忙)     │
   P1.07 ───────────┤ RST      RST ├─────────────────┤ RST_N (低复位)   │
                    │              │      ≥10 μs 低脉冲                 │
   P1.06 ◄── 分压 ──┤ ADC AIN2     │  （电池等；非 PN5180 脚）          │
                    └──────────────┘                 │                  │
                                                     │   XTAL 27.12 MHz │
                                                     │   ┌───┐          │
                                                     ├───┤   ├─── XIN   │
                                                     │   │ X │   XOUT   │
                                                     └───┤   ├───┘      │
                                                       CL CL (按晶振手册)│
                                                     └──────────────────┘

SPI：Mode 0，MSB first；线尽量短，远离 TX1/TX2 / 天线。
BUSY：MCU 侧内部上拉或外挂 10 kΩ → 3.3 V。
IRQ：必须接 **P0/P1**（本表 P0.02）才能 `gpio` 边沿中断 / 睡眠唤醒；P2 无 GPIOTE。不用时可悬空，改轮询 IRQ 寄存器。
RST：上电后 MCU 再发复位脉冲；上电瞬间勿浮空（建议默认高或上拉）。
```

### 2.4.4 天线匹配与 ESD（TX1 / TX2）

典型 EMC + 调谐拓扑（元件值 **必须** 按线圈实测 / AN11740 工具改）：

```text
 PN5180                                                          天线
  TX1 ──┬── Cseries ──┬── Lseries ──┬──┬───────────── 线圈端 A
        │             │             │  │
        ═ Cshunt0     ═ Cemc        ═ Ctune     TVS (低电容)
        │             │             │  │         例 PESD5V0S1UB
        ┴ GND         ┴ GND         ┴ GND        两端对 GND
  TX2 ──┴─────────────┴─────────────┴──┴───────────── 线圈端 B

示意符号：
  Cseries  隔直（常见 ~100 pF NPO）
  Cshunt0  并联匹配
  Cemc     EMC（ETSI/EN 300 330）
  Ctune    并联谐振微调
  Lseries  串联电感（或并入线圈设计）
```

| 位置 | 器件 | 典型起点 | 备注 |
|------|------|----------|------|
| TX1/TX2 | 低电容双向 TVS | &lt; 0.5 pF | 用户可触天线必须加 |
| SPI 组 | TVS 阵列可选 | TPD4E00x 类 | 长排线/板边插座时加 |
| IRQ/BUSY/RST | 3.3 V TVS 可选 | — | 板内短线可省 |

**禁止**：把 nRF54L15 原 NFCT 脚接到 **13.56 MHz 天线**；P1.02 / P1.03 仅数字备用。本工程片上 NFCT 已弃用。

### 2.4.5 一页连接总表（布线核对）

| 网名 | 从 | 到 | 外围 |
|------|----|----|------|
| SPI_SCK | P1.11 | PN5180 SCK | 短、等长、远离天线 |
| SPI_MOSI | P1.12 | PN5180 MOSI | |
| SPI_MISO | P1.13 | PN5180 MISO | |
| SPI_NSS | P1.14 | PN5180 NSS | ACTIVE_LOW |
| BUSY | P1.08 | PN5180 BUSY | 10 kΩ 上拉 3.3 V；轮询 |
| IRQ | P0.02 | PN5180 IRQ | 可选上拉；GPIOTE30 |
| RST | P1.07 | PN5180 RST_N | ACTIVE_LOW |
| VBAT_ADC | P1.06 | 分压 → SAADC AIN2 | ≤ VDD；滤波电容 |
| TVDD | LDO 5V（或 3.3）常供电 | PN5180 TVDD | 无 MCU EN |
| VDD_3V3 | LDO 3.3 | DVDD + VDDLDO_IN + MCU | 100 nF×N + bulk |
| TVDD | LDO 5V（或 3.3） | TVDD | 100 nF + 10 μF |
| GND | 系统地 | EPAD + 所有 GND | 完整地平面 |
| RF | TX1/TX2 | 匹配 → 线圈 | TVS + NPO 电容 |

### 2.4.6 上电时序（硬件 + 软件约定）

```text
  VIN / LDO 常供电  ___/¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯
  TVDD / DVDD         稳定 (≥ Datasheet t_power)
  RST (P1.07)     ¯¯¯¯\___/¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯  (≥ 10 μs 低)
  BUSY / IDLE_IRQ         …… 等 IDLE ……
  SPI 访问                    ──► 读 Firmware Version
```

---

## 3. 硬件评估

### 3.1 结论（摘要）

| 项 | 结论 | 说明 |
|----|------|------|
| 总线隔离 | **通过** | EPD=`spi00`(P2)，PN5180=`spi22`(P1)；避免 EPD bitbang sleep SPIM00 时拖死读卡 |
| SPI 速率 | **可用** | NCS DT：`spi22` `max-frequency` **8 MHz**；PN5180 主机口通常 ≤7～20 Mbps，建议软件先 **4～7 MHz** |
| 电源域 | **通过** | `spi22` 四线全在 P1；IRQ 在 P0（LP，GPIOTE30）。勿把 SPIM 信号跨到 P2 |
| GPIOTE | **通过** | IRQ=**P0.02**；**禁止** IRQ 落 P2 |
| 与 DK 差异 | **注意** | 本表为自有 QFN48 板，**不兼容** DK 默认 UART/LED/LFXO 焊桥。旧冒烟板勿烧 rev_b overlay |
| 片上 NFCT | **已弃用** | overlay 关 `&nfct`；天线只接 PN5180；P1.02/03 仅备用 GPIO |
| LFXO | **预留** | P1.00/01 不接 SPI，可后贴 32.768 kHz |
| RF / 电流 | **风险可控** | TVDD 常供电 + 本地 bulk；睡眠 `rf_off`（无硬件掉电） |
| ADC | **预留** | **P1.06 = AIN2**；电池分压；勿超 VDD |
| ESD / 布线 | **注意** | SPI 尽量短、同层回流；BUSY/IRQ 远离天线；13.56 MHz 调谐按模组手册 |

### 3.2 为何不共用 spi00

1. 现网 `epd.c` 会 release / sleep `spi00` 并 bitbang，共总线会打断 PN5180 半双工时序。  
2. EPD 与读卡可能并行（刷屏线程 vs NFC 线程）；分总线零仲裁。  
3. 与 STM32 方案一致（EPD / PN5180 分 SPI）。

### 3.3 电源与天线（硬件检查单）

- [ ] VDD(TV) / 数字 3.3 V 与 MCU 共地  
- [ ] TVDD / 天线驱动轨电流余量（峰值）  
- [ ] TVDD / DVDD **常供电**；上电 → MCU RST 脉冲 → 等 IDLE_IRQ  
- [ ] 电池分压接 **P1.06 (AIN2)**，分压后 ≤ VDD |
- [ ] 13.56 MHz 匹配网络与线圈只接 PN5180  
- [ ] IRQ 接 **P0.02**；可悬空改轮询，但低功耗唤醒必须有边沿脚  
- [ ] **勿**把 IRQ 接到 P2（芯片无 GPIOTE）  

### 3.4 风险清单（硬件）

| 风险 | 影响 | 缓解 |
|------|------|------|
| 原理图脚与本表不一致 | 通不通 | 合入 overlay 前对照 PCB |
| IRQ 误接到 P2 | 中断 `-ENOTSUP`、无法唤醒 | 坚持 P0.02 |
| DK 飞线误用板载脚 | LED/NFC 焊盘干扰 | 优先自有板；DK 仅临时验证 |
| 天线未调谐 | 读距极短 / 失败 | 先读 Firmware Version 验 SPI，再开 RF |
| 大电流跌落 | 复位 / BLE 掉线 | 粗电源、本地电容、开场时暂缓大刷屏 |

---

## 4. 软件评估

### 4.1 结论（摘要）

| 项 | 结论 | 说明 |
|----|------|------|
| 驱动复用 | **通过** | `cbb/pn5180` 回调注入，可直接在 `source/pn5180_port.c` 接 Zephyr SPI/GPIO |
| 架构边界 | **通过** | 片外器件进 `projects/eink_tag/source/`；`board` 只登记 SPI/IRQ；不碰 `ble_gatt` |
| 并发 | **需设计** | BUSY 等待含 sleep → **禁止** GATT/ISR 内调 `pn5180_*`；专用线程或 work + msgq |
| 与 EPD/BLE | **可共存** | 独立 SPI；优先级：链路/CMD > 读卡 ≥ EPD（数字越小越高） |
| Flash | **无关** | 冒烟不写 Settings；配置项若有再事件驱动写 |
| 片上 NFCT 源码 | **已删** | 不再保留 `nfc.c` / Tag 仿真 |

### 4.2 推荐分层

```text
GATT / IRQ 回调
    └─ ingress only（msgq / k_work / atomic）
           │
    app / pn5180 线程（可阻塞等 BUSY）
           │
    pn5180_port（Zephyr SPI + gpio_dt_spec 回调）
           │
    cbb/pn5180（寄存器 / RF / TXRX）
```

硬约束对齐 `dev_norms.md`：

- 入队 `K_NO_WAIT` + 错误码；消费者才可 `K_FOREVER`
- 读卡满队列：拒新任务或 ACK 忙，禁止堵死 BLE
- 刷屏与读卡并行时：共享「系统忙」可用 atomic；锁内禁止 sleep

### 4.3 板级 / Kconfig 改动面（预估）

| 文件 | 改动 |
|------|------|
| `board/app.overlay` | 新板：UART/LED/EPD GPIO + `spi22` + `zephyr,user` 按 §2.1 |
| `board/board_config.c` | `spi22` 总线 + slave `"pn5180"`；可选 IRQ 表 |
| `README.md` | 已切 rev_b 脚表；细节指向本文 |
| `prj.conf` | 已有 `CONFIG_SPI`；一般无需新 Kconfig，除非加 `CONFIG_EINK_PN5180=y` 门控编译 |
| `CMakeLists.txt` | `cbb/pn5180/pn5180.c` + port 源文件 |
| `source/pn5180_*.c` | **新建** port + 冒烟 API（非必要不拆过多文件） |

### 4.4 冒烟验收（建议顺序）

1. Overlay 合入 → `.\nrf.cmd -Project eink_tag build`；核对 `zephyr.dts` 中 `spi22` / GPIO。  
2. `pn5180_init` → 读 Firmware Version ≠ 0x00/0xFF（串口打版本）。  
3. `pn5180_setup_rf` + Inventory（ISO15693）贴白卡；失败先查天线/电源再查软件。  
4. 刷屏中发 BLE PING：仍应 ACK；读卡任务可排队或返回忙。  
5. 睡眠路径：`rf_off`（常供电无 PWR_ON）；电流应下降。  
6. ADC：读 P1.06/AIN2 与万用表分压点对照。

### 4.5 风险清单（软件）

| 风险 | 影响 | 缓解 |
|------|------|------|
| 在回调里等 BUSY | 卡死 BLE/调度 | 专用线程 |
| EPD bitbang 与误共用 SPI | 通信错乱 | 坚持 `spi22` |
| `wait_irq` 轮询烧 CPU | 功耗/调度 | IRQ GPIO + sem；或限超时 |
| 未测延迟上 FLPR | 不可控 | 读卡留 APP 核 |

### 4.6 工作量粗估

| 阶段 | 内容 | 量级 |
|------|------|------|
| A | overlay + README + board_config 登记 | 小（0.5 d） |
| B | Zephyr port + init/读版本冒烟 | 中（1–2 d） |
| C | ISO15693 Inventory + 与 BLE/EPD 并行压测 | 中（2–3 d） |
| D | 低功耗（关场/`rf_off`）+ 产品命令；可选 ADC 上报 | 另开需求门禁 |

---

## 5. 决策记录

| 决策 | 选择 | 理由 |
|------|------|------|
| SPI 实例 | `spi22` | P1 同域；DK 已有 `wifi_spi` 别名习惯；与 `uart20` 无地址冲突（uart20 占另一实例） |
| 不共用 spi00 | 是 | EPD bitbang / 并行 |
| NSS | P1.14 软件 CS | 与 SCK 同边；`cbb` 回调模型 |
| IRQ | P0.02（非 P2） | GPIOTE30；System OFF 唤醒；P2 无 GPIOTE |
| SCK 不占 XL | P1.11 而非 P1.00 | 预留 LFXO；P1.11 为 clock pin |
| EPD 全 P2 | DC/RST/BUSY 收到 P2 | FPC 短线；SPIM00 专用脚 |
| P1.06 | **ADC AIN2**（非 PWR_ON） | 腾出模拟通道；PN5180 常供电 |
| 本阶段交付 | overlay + board_config + README | 脚表已合入；PN5180/ADC/IRQ 业务另开；旧板勿烧 |

---

## 6. 下一步

1. 硬件：原理图/PCB 与 **§2.1** 逐脚签字。  
2. ~~软件：合入 overlay + `board_config`~~（已完成；无 PN5180 业务）。  
3. 再开门禁做 PN5180 port 冒烟（读 Firmware Version；再挂 IRQ/ADC）。

变更脚表：先改本文 §2 → 再改 overlay / README / `board_config`，三处一致。
