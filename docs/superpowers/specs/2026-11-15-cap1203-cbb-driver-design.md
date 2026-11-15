# Design: cbb/cap1203 通用薄驱动（A 范围）

**日期**：2026-11-15  
**状态**：已实现；板上触摸冒烟待验收  
**落点**：`cbb/cap1203/`；板级 `projects/ink_panel/board/cap1203_board.*`

## 目标

1. MCU 无关 I2C 回调驱动；7-bit 地址默认 **0x28**；8-bit 寄存器地址。
2. `init`：**只读 Product ID（期望 0x6D）**，不改灵敏度 / 通道。
3. A 范围：寄存器读写、通道使能、灵敏度、读触摸状态、清 INT、关多触摸互锁（便于联调）。
4. 板级冒烟：与 nPM 共 `i2c21`；起机 configure 后周期读 CS 状态打日志。

## 非目标（本阶段）

- ALERT# ISR / 唤醒策略
- Standby / Deep Sleep 细调、MTP 图案、LED
- 自动校准算法封装（可后续加 `recalibrate`）

## API（摘要）

| API | 行为 |
|-----|------|
| `cap1203_init` | 探测 Product ID |
| `cap1203_reg_read` / `write` | 原始访问 |
| `cap1203_configure` | 使能通道掩码、INT 使能、可选关 MULT_BLK、可选灵敏度 |
| `cap1203_read_touched` | 读 `Sensor Input Status` → CS1/2/3 位 |
| `cap1203_clear_int` | 清 Main Control INT 位 |

板级：`cap1203_board_bringup()` + `cap1203_board_poll()`。
