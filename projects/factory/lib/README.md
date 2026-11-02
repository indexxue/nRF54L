# lib/

本工程**私有**驱动 / 业务库。串口命令框架 `cmd` 只在本工程，不进 `common/`。

## 当前模块

| 模块 | 路径 | 说明 |
|------|------|------|
| cmd | `lib/cmd/` | 厂测串口：help/reboot/log/version/sn/mac/led/btn/ccfg/ftmexit + zb*（todo）；`local_only` + remote capture |

## 归属

| 代码 | 放哪里 |
|------|--------|
| 多工程都要用 | 仓库根 `common/`（经 `modules/nrf54_common`） |
| **仅本工程要用** | 本目录 `lib/<模块名>/` |
| 板级引脚 / 时钟 | 仓库 `boards/`（有原理图后再做） |

## 子目录约定

```
lib/
└── <module>/           # 目录名 = 主源文件名
    ├── <module>.c
    └── <module>.h
```

CMake：**显式列出**（已在 `CMakeLists.txt` 挂上 `lib/cmd`）。

生命周期约定见 [`../src/README.md`](../src/README.md)。
