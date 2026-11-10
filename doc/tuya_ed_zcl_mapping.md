# zigbee_ed ↔ 涂鸦标准接入映射

> **用途**：涂鸦 IoT 建品后的 **PID / 身份 / DP↔ZCL** 对照表；固件 Task 1+ 的 Basic 字符串与验收以本文为准。  
> **方案**：标准 Zigbee 子设备（非模组 UART）。  
> **计划**：[`plan_tuya_zigbee_ed_standard.md`](plan_tuya_zigbee_ed_standard.md) · T0  
> **状态**：T0 **可关闭**（PID/DP/固件 key 已齐；平台未给厂商/型号 → 联调占位见 §1 / §3.3）。

---

## 1. 产品总表

| 项 | 值 | 来源 |
|----|-----|------|
| 工程 | `projects/zigbee_ed` | 本仓 |
| 接入方式 | **标准 Zigbee**（涂鸦多模网关）· **自定义固件**（非模组） | 已定方案 B |
| 禁止 | 涂鸦模组串口协议 / ED 内 MQTT | 计划非目标 |
| **PID** | **`agbfpcdx`** | 涂鸦 IoT · 2026-11-10 |
| **产品名称** | `zigbee_ed` | 涂鸦 IoT |
| **品类 / 方案名** | 标准品类 · **光源** · 自定义方案 · **Zigbee**（默认彩光/冷暖全功能集） | 涂鸦 IoT |
| **固件名称** | 中文 `zigbee_ed联调` · 英文 `zigbee_ed` | 硬件开发 · 自定义固件 |
| **固件类型** | Zigbee固件 | 平台 |
| **固件 key** | **`keyanydv`** | 平台 · 2026-11-10 |
| **升级通道号** | `3` | 平台 |
| **联网方式** | Zigbee | 平台 |
| **芯片平台** | 其它 | 平台不可改 |
| **固件版本** | （平台暂未填；本地以 `nrf.cmd release` 版本为准） | 后续可补 |
| **manufacturer_name** | 联调占位 **`TY`** | 平台建品页未提供；入网识别异常再改 |
| **model_identifier** | 联调占位 **`zigbee_ed`** | 同上；亦可用 PID `agbfpcdx` 若网关要求 |
| DP 导出表 | `projects/zigbee_ed/zigbee_ed(agbfpcdx)_11_10.xlsx` | 平台导出 |
| Endpoint | **1** | 固件 `ZIGBEE_APP_ENDPOINT` |
| Profile | HA `0x0104` | `ZB_AF_HA_PROFILE_ID` |
| Device ID | On/Off Light **`0x0100`** | `zigbee_device.h` |
| 扫描信道掩码 | 11/15/20/25（`0x02108800`） | `prj.conf` |

---

## 2. 设备侧 ZCL（已实现 / 已契约）

### 2.1 Endpoint 1 簇

| Cluster | ID | 角色 | 说明 |
|---------|-----|------|------|
| Basic | `0x0000` | Server | 当前联调属性偏少；T1 将补 manufacturer/model |
| Identify | `0x0003` | Server | 已声明 |
| OnOff | `0x0006` | Server | **控灯主路径** |

### 2.2 OnOff（对拍涂鸦 DP switch）

| 项 | 值 |
|----|-----|
| Cluster | **`0x0006`** |
| 属性 OnOff | **`0x0000`**（bool；0=关，1=开） |
| Off | 命令 **`0x00`** |
| On | 命令 **`0x01`** |
| Toggle | 命令 **`0x02`** |
| 本地输出 | LED_G |
| 串口 | `light: on` / `light: off` |

与 ZigBridge `znp light on|off|toggle` **同一空中契约**；涂鸦网关亦应走标准 ZCL，而非私有串口。

### 2.3 Basic（T1 目标属性）

| 属性 | ID | 当前固件 | T1 后 |
|------|-----|----------|-------|
| ZCLVersion | `0x0000` | 有 | 保留 |
| PowerSource | `0x0007` | 有（DC） | 按产品改（若标电池再议） |
| ManufacturerName | `0x0004` | **T1 已接** `CONFIG_ZIGBEE_ED_MANUFACTURER`（默认 `TY`） | = §1 |
| ModelIdentifier | `0x0005` | **T1 已接** `CONFIG_ZIGBEE_ED_MODEL`（默认 `zigbee_ed`） | = §1 |
| ApplicationVersion | `0x0001` | **T1 已接** `CONFIG_ZIGBEE_ED_APP_VERSION` | 单字节 |

---

## 3. 涂鸦平台侧（已建品）

### 3.1 建品路径（已完成 · 备查）

1. 标准品类 → **光源** → 产品开发 → **自定义方案** → Zigbee  
2. 产品名 `zigbee_ed` · PID `agbfpcdx`  
3. 硬件开发 → **自定义固件**（非模组）· 类型 Zigbee固件 · key `keyanydv` · 通道 3  
4. 平台未露出独立 Manufacturer/Model 字段 → 采用 §1 / §3.3 联调占位  

### 3.2 DP ↔ ZCL（来自 xlsx · 2026-11-10）

**本期只验收 DP1。** 其余为平台默认彩光模板，固件未实现，App 点了可无响应。

| DP ID | 标识符 | 传输 | 类型 | 含义 | ZCL 映射 | 本期 |
|-------|--------|------|------|------|----------|------|
| **1** | **switch_led** | rw | bool | 开关 | OnOff `0x0006` · attr `0x0000` · On/`0x01` Off/`0x00` Toggle/`0x02`；T2 入网后默认 attribute report | **做** |
| 2 | work_mode | rw | enum | 模式 white/colour/scene/music | （未映射） | 不做 |
| 3 | bright_value | rw | value 10–1000 | 亮度 | Level 等（未实现） | 不做 |
| 4 | temp_value | rw | value 0–1000 | 冷暖 | ColorTemp（未实现） | 不做 |
| 5 | colour_data | rw | string | 彩光 | Color（未实现） | 不做 |
| 6 | scene_data | rw | string | 场景 | 未实现 | 不做 |
| 7 | countdown | rw | value 0–86400 s | 倒计时 | 未实现 | 不做 |
| 8 | music_data | wr | string | 音乐律动 | 未实现 | 不做 |
| 9 | control_data | wr | string | 实时调节 | 未实现 | 不做 |
| 33 | power_memory | rw | raw | 通电反应 | 未实现 | 不做 |
| 34 | do_not_disturb | rw | bool | 通电勿扰 | 未实现 | 不做 |
| 35 | switch_gradient | rw | raw | 开关渐变 | 未实现 | 不做 |
| 81 | zll_value | rw | value −65…−45 dBm | ZLL 阀值 | 未实现 | 不做 |
| 209 | cycle_timing | rw | raw | 循环定时 | 未实现 | 不做 |
| 210 | random_timing | rw | raw | 随机定时 | 未实现 | 不做 |

> 完整属性列以 xlsx「功能点」页为准。若平台允许删减标准功能，可只留 `switch_led` 以简化 App 面板；删不了也不阻塞联调。

### 3.3 身份字符串（T1 写入 Basic）

| 写入 Basic | 当前采用值 | 固件 Kconfig（T1） | 说明 |
|------------|------------|-------------------|------|
| ManufacturerName | **`TY`** | `CONFIG_ZIGBEE_ED_MANUFACTURER` | 平台未给；联调占位 |
| ModelIdentifier | **`zigbee_ed`** | `CONFIG_ZIGBEE_ED_MODEL` | 与产品名一致；联调占位 |

**规则**：若日后平台/网关文档给出强制厂商/型号，必须改 Kconfig 与本文并重新烧录；量产 release 不得 silently 沿用错误占位。

---

## 4. 验收时怎么对拍

| 步骤 | 期望 |
|------|------|
| 涂鸦多模网关上云 | App 网关在线 |
| App 添加设备 + 本机 BTN1 | 发现并绑定到本 PID 产品（非长期「未知设备」） |
| App 开 / 关 | LED_G + `light: on\|off` |
| （可选）ZigBridge `znp light` | 同固件仍可控（回归） |

失败优先查：§1 manufacturer/model 是否已换成平台值；品类是否标准 Zigbee；本机是否在配网窗内 Steering。

---

## 5. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-11-10 | T0：设备侧 ZCL 落仓；平台 PID/DP/身份留 TBD + 建品清单 |
| 2026-11-10 | PID=`agbfpcdx`；产品名 `zigbee_ed`；xlsx DP 表入库；身份 manufacturer/model 仍 TBD |
| 2026-11-10 | 自定义固件 key=`keyanydv`；通道3；身份联调占位 mfr=`TY` model=`zigbee_ed`；**T0 关闭** |
