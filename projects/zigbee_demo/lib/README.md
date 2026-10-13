# lib/

本工程**私有**驱动 / 业务库。默认空目录；需要某个外设时再建子目录。

## 归属

| 代码 | 放哪里 |
|------|--------|
| 多工程都要用 | 仓库根 `common/`（经 `modules/nrf54_common`） |
| **仅本工程要用** | 本目录 `lib/<模块名>/` |
| 板级引脚 / 时钟 | 仓库 `boards/`（有原理图后再做） |

例：蜂鸣器只给厂测用 → `projects/factory/lib/buzzer/`，**不要**先塞进 `common/`。第二个工程也要用时再上收。

## 子目录约定

```
lib/
└── <module>/           # 目录名 = 主源文件名
    ├── <module>.c
    └── <module>.h      # 头文件与源同目录
```

CMake：**显式列出**（不要 glob）。在工程 `CMakeLists.txt` 中追加：

```cmake
target_sources(app PRIVATE lib/<module>/<module>.c)
target_include_directories(app PRIVATE lib/<module>)
```

`src/main.c` 只做编排；实现放在 `lib/<module>/`。生命周期约定见 [`../src/README.md`](../src/README.md)。
