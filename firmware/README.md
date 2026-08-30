# firmware/

量产 / 发布固件归档目录。用 **`nrf.cmd release`** 生成（无 app 调试日志）；日常 `build` 产物仍在 `projects/*/build/`，不会自动拷到这里。

二进制默认 **gitignore**；本目录只跟踪 README 等说明。

## 命令

```powershell
.\nrf.cmd -Project factory release 0.0.1
```

- 与 debug 共用 `projects/<name>/build/`（`EXTRA_CONF` 不同，west 会重配）
- `-DEXTRA_CONF_FILE=prj_release.conf;prj_version.conf` → 空 log + 注入 `CONFIG_APP_VERSION_STRING`
- `prj_release.conf`：关 boot banner；保证产出 bin/hex
- 本阶段签名标记固定 **`unsigned`**
- 一次产出 **elf + hex + bin** 三个文件

## 目录

```text
firmware/<x.y.z>/
```

- 三段 semver：`0.0.1`（目录**不带** `V`；命令行可写 `V0.0.1`，会去掉 V）
- 同版本可并存多工程

## 文件名（约定 B）

```text
{chip}_{project}_{version}_{YYYYMMDD}_{sign}.{ext}
```

| 字段 | 说明 | 示例 |
|------|------|------|
| `chip` | 默认 `nrf54l15`（可用 `nrf.env` 的 `CHIP_NAME=`） | `nrf54l15` |
| `project` | `-Project` / `project()` | `factory` |
| `version` | 与目录一致 | `0.0.1` |
| `YYYYMMDD` | 打包本地日 | `20260830` |
| `sign` | `unsigned` / 以后 `sign` | `unsigned` |
| `ext` | **必出三种**：`elf`、`hex`、`bin` | |

**示例（一次 release 三个文件）：**

```text
firmware/0.0.1/nrf54l15_factory_0.0.1_20260830_unsigned.elf
firmware/0.0.1/nrf54l15_factory_0.0.1_20260830_unsigned.hex
firmware/0.0.1/nrf54l15_factory_0.0.1_20260830_unsigned.bin
```

| 扩展名 | 来源 |
|--------|------|
| `.hex` | 优先 `build/merged.hex`（可 flash） |
| `.elf` | `build/<project>/zephyr/zephyr.elf` |
| `.bin` | `build/<project>/zephyr/zephyr.bin` |

## 本阶段规则

| 项 | 约定 |
|----|------|
| `build` | 调试：有 LOG / 心跳 → `build/` |
| `release` | 发布：`log.c` 输出空 → 同 `build/`，再归档 elf/hex/bin → `firmware/` |
| 签名 | 固定 `unsigned` |
| factory 串口 cmd | release 仍保留（协议输出，不是调试 log） |
| BOARD / git hash | 暂不进文件名 |
| manifest | 暂缓 |
