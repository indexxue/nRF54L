# Design: cbb/npm1300 通用薄驱动（A 范围）

**日期**：2026-11-15  
**状态**：对话设计已批准；待规格审阅后写实现计划  
**仓库落点**：Git 子模块 `cbb` → `cbb/npm1300/`（远程 `git@github.com:indexxue/cbb.git`）  
**硬件依据**：
- `docs/superpowers/plans/schematic_wiring/02_power_npm1300.md`
- `docs/superpowers/plans/nRF54L15 墨水屏 Zigbee SED 硬件评审意见.md` §1.1
- Nordic nPM1300 Product Specification（TWI：7-bit `0x6B`，16-bit 寄存器地址大端，8-bit 数据）
- 对照参考（不 vendor）：[NordicSemiconductor/npmx](https://github.com/NordicSemiconductor/npmx)

---

## 1. 目标与非目标

### 目标（第一版 Must）

1. 在 `cbb` 新增 **MCU 无关** nPM1300 驱动：回调注入 I2C，风格对齐 `ina226` / `mcp4651` 与 `cbb/README.md`。
2. **自研薄驱动**：对照 PS / npmx 寄存器定义手写；**不**整库 vendor `npmx`，**不**依赖 Zephyr / nRF Connect SDK。
3. **`npm1300_init` 只探测通信**，不改任何电源轨 / 充电 / Ship。
4. 覆盖 A 范围：原始寄存器读写、BUCK、LDO/LoadSW、充电基本项、Ship、事件读清。
5. 提供子目录 `README.md`（器件说明、API、STM32/Zephyr 板级回调示例）。

### 非目标（第一版）

- Fuel gauge 算法 / 库对接
- LED、timer、POF 细配、完整 GPIO 复用矩阵
- INT 线 ISR 实现（板级接 `NPM_INT_N`，再调 `event_read`）
- 板级预设写死（本墨水屏 3.0 V / 200–300 mA 等由**调用方**传入）
- 本轮不改 `projects/ink_panel` 业务；驱动入库后由后续计划接入

---

## 2. 已确认决策

| 项 | 选择 |
|----|------|
| 范围 | **A** — 最小可用 |
| 官方代码用法 | **1** — 自研薄驱动，对照官方寄存器 |
| `init` 行为 | **G** — 只探测；不偷偷改轨 |
| 配置写入 | 由显式 API 完成（无「init 内 apply 全套预设」） |

---

## 3. 架构与文件

| 文件 | 职责 |
|------|------|
| `cbb/npm1300/npm1300.h` | 回调 typedef、status、句柄、公开 API |
| `cbb/npm1300/npm1300.c` | 16-bit 地址读写 + A 范围功能实现 |
| `cbb/npm1300/README.md` | 器件说明、硬件注意、HAL 示例、编入工程 |

可选后续：`cbb/CMakeLists.txt` / `idf_component.yml` 按需登记（ESP 工程用到时再加；Keil/Zephyr 按需加源文件即可）。

### 总线约定

- I2C **7-bit** 地址默认 **`0x6B`**（`NPM1300_ADDR_7BIT_DEFAULT`）
- 寄存器：**16-bit 大端地址** + **8-bit 数据**（先高字节、再低字节）
- 回调：`write` / `write_read`（优先）/ 可选 `delay_ms`
- 成功 `0`、失败非 `0`；驱动映射为 `NPM1300_ERROR_I2C`
- 辅助宏：`NPM1300_HAL_ADDR_FROM_7BIT(addr7)`

### 职责边界

| 层 | 做 | 不做 |
|----|----|------|
| `cbb/npm1300` | 寄存器协议与换算、公开控制 API | RTOS、日志、板级脚位、fuel gauge |
| 板级 / 工程 | 注入 I2C、GPIO 门控、充电策略、Ship 流程 | 改 cbb 去 `#include` HAL |

说明：本 EVT 板上 `EPD_PWR_EN` / `TOUCH_LDO_EN` 经 MCU GPIO 进 nPM GPIO 控制 LS2 / LDO1。驱动仍提供 **I2C 寄存器开关**，供调试或无 GPIO 绑线方案；**产品路径优先板级 GPIO**，避免双控冲突（README 写明）。

---

## 4. API 清单

命名：`npm1300_verb_object()`。状态：`npm1300_status_t`。

### 4.1 类型（摘要）

```c
typedef int (*npm1300_i2c_write_t)(uint8_t addr7, const uint8_t *data, uint16_t len);
typedef int (*npm1300_i2c_write_read_t)(uint8_t addr7,
                                        const uint8_t *tx, uint16_t tx_len,
                                        uint8_t *rx, uint16_t rx_len);
typedef void (*npm1300_delay_ms_t)(uint32_t ms);

typedef enum {
    NPM1300_OK = 0,
    NPM1300_ERROR_PARAM,
    NPM1300_ERROR_NOT_INIT,
    NPM1300_ERROR_I2C,
    NPM1300_ERROR_RANGE,
    NPM1300_ERROR_UNSUPPORTED,
} npm1300_status_t;

typedef enum { NPM1300_BUCK1 = 1, NPM1300_BUCK2 = 2 } npm1300_buck_t;
typedef enum { NPM1300_LDSW1 = 1, NPM1300_LDSW2 = 2 } npm1300_ldsw_t;
typedef enum { NPM1300_LDSW_MODE_LDO, NPM1300_LDSW_MODE_LOADSW } npm1300_ldsw_mode_t;
```

索引约定：`1`/`2` 对齐手册通道；本板：LDO1=触摸、LS2=EPD；BUCK2 硬件已废，API 保留但启动序列不碰。

### 4.2 核心

| API | 行为 |
|-----|------|
| `npm1300_init(dev, cfg)` | 参数检查 → 存回调/地址 → 通信探测 → `initialized=true`；**不写轨** |
| `npm1300_reg_write(dev, addr16, val)` | 原始写 |
| `npm1300_reg_read(dev, addr16, *val)` | 原始读 |

通信探测：读一份手册规定的只读/状态寄存器（实现时锁定具体地址，对照 PS/npmx）；失败则不置 `initialized`。

### 4.3 BUCK

| API | 行为 |
|-----|------|
| `npm1300_buck_set_vout_mv(dev, buck, mv)` | 设输出电压；非法档位 → `RANGE` |
| `npm1300_buck_enable(dev, buck, on)` | 软件使能；关 BUCK1 可能导致 SoC 掉电（调用方责任） |
| `npm1300_buck_get_status(dev, buck, *st)` | 状态位摘要 |

### 4.4 LDO / Load Switch

| API | 行为 |
|-----|------|
| `npm1300_ldsw_set_mode(dev, idx, mode)` | LDO 或 LoadSW |
| `npm1300_ldsw_set_vout_mv(dev, idx, mv)` | LDO 模式电压 |
| `npm1300_ldsw_enable(dev, idx, on)` | 寄存器开关 |
| `npm1300_ldsw_get_status(dev, idx, *st)` | 状态 |

### 4.5 充电（基本）

| API | 行为 |
|-----|------|
| `npm1300_charger_set_ichg_ma(dev, ma)` | 充电电流（工程常用 200–300） |
| `npm1300_charger_set_vterm_mv(dev, mv)` | 截止电压（如 4200） |
| `npm1300_charger_enable(dev, on)` | 使能充电；`init` 不默认开 |
| `npm1300_charger_get_status(dev, *st)` | 充电/电池相关状态摘要 |

### 4.6 Ship / 事件

| API | 行为 |
|-----|------|
| `npm1300_ship_enter(dev)` | 进入 Ship；成功后可能无法再通信 |
| `npm1300_event_read(dev, *mask)` | 读待处理事件位图（最小集合，位定义在头文件） |
| `npm1300_event_clear(dev, mask)` | 清事件 |

---

## 5. 错误处理

| 场景 | 返回 |
|------|------|
| NULL、非法索引、空回调 | `PARAM` |
| 电压/电流不在支持表 | `RANGE` |
| 未 init | `NOT_INIT` |
| 总线失败 | `I2C` |
| 该硅片不支持的组合（若检出） | `UNSUPPORTED` |

规则：

- 总线失败不在驱动内重试
- 失败路径禁止置 `initialized=true`
- 驱动内禁止 `printf` / `LOG_*`
- `ship_enter` 后调用方视为「可能已失电」

---

## 6. 板级接入约定（墨水屏 EVT）

| 项 | 约定 |
|----|------|
| I2C | 与 CAP1203 共 `I2C0`；地址 `0x6B`；HAL `DevAddress` 需 `<<1` |
| 轨门控 | EPD/Touch **优先 MCU GPIO**；I2C `ldsw_enable` 备用/调试 |
| BUCK1 | 上电 VSET≈2.7 V；需要时固件 `buck_set_vout_mv(BUCK1, 3000)` |
| BUCK2 | 硬件 VSET2=GND、VOUT2=VSYS；推荐启动不碰 |
| 充电 | 板级设 ICHG/VTERM 后再 enable |
| INT | `NPM_INT_N`→P0.04；ISR 只 ingress，线程 `event_read/clear` |
| Ship | 仅仓储流程；退出靠 SHPHLD（与用户键同源） |

### 推荐启动顺序（板级，不进驱动）

1. 总线就绪 → `npm1300_init`
2. 需要时 `buck_set_vout_mv(BUCK1, 3000)`
3. 配充电参数 → 按策略 `charger_enable`
4. 刷屏前后用 GPIO（或 I2C）开关 LS2

---

## 7. 验收标准

1. `init` 后 `reg_read` 稳定成功。
2. `buck_set_vout_mv(BUCK1, 3000)` 后 VOUT1 可测到约 3.0 V（或示波器/万用表）。
3. 在明确单控路径下，LS2/LDO1 寄存器开关可见输出变化（或与 GPIO 路径二选一验证，避免双控）。
4. 充电参数写入后 `charger_get_status` 可读；使能策略由测试用例定义。
5. Ship 仅在专用测试流程验证（SHPHLD 可唤醒）。
6. 源码符合 `cbb/README.md`：无 HAL/`Common` include、有器件 README、命名与 status 规范。

---

## 8. 实现注意（供 plan 引用）

1. 寄存器地址与位域实现时对照 PS 与 npmx `adk/npm1300.h`；头文件可保留必要 `#define`，复杂位域放 `.c`。
2. 电压/电流 API 用查表或手册档位换算，禁止静默钳位到错误值而不返回 `RANGE`（若采用「就近合法档」必须在 README 写明；**默认严格 RANGE**）。
3. 不把业务塞进 `ble_gatt` / Zephyr 驱动树；Zephyr 工程以板级回调包装本库即可。
4. 提交落在 **cbb 子模块仓库**；主仓只更新 submodule 指针（实现阶段再做）。

---

## 9. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-11-15 | 初版：A 范围 + 自研薄驱动 + init 只探测；对话三节设计已确认 |
