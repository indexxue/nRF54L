# demo

**工程模板**（skeleton 默认工程）。新应用优先复制本目录，或用 `scripts/new_project.ps1`。

最小应用：`common` 的 log / event。无私有串口 cmd（cmd 仅 factory）。私有库目录：`lib/`（默认空，见 [`lib/README.md`](lib/README.md)）。

```powershell
.\nrf.cmd -Project demo build
# 本阶段可不 flash：
# .\nrf.cmd -Project demo flash
```

Board 默认：`nrf54l15dk/nrf54l15/cpuapp`（见 `nrf.env`）。日志用自备串口工具。

## 必含文件

| 文件 | 作用 |
|------|------|
| `CMakeLists.txt` | `EXTRA_ZEPHYR_MODULES` → `nrf54_common`；`project(<name>)` |
| `prj.conf` | UART console 等 Kconfig |
| `src/main.c` | 薄入口 → `app_startup` / `app_run` |
| `src/app.c` / `app.h` | Zephyr 应用生命周期 |
| `lib/` | 本工程私有库（空占位；按模块名建子目录） |
| `README.md` | 本工程说明 |

不要提交 `build/`。
