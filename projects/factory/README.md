# factory

厂测（Factory）工程骨架。生命周期在 [`src/`](src/README.md)；串口命令在 [`lib/cmd/`](lib/cmd/)（**非** `common/`）；其它仅厂测驱动见 [`lib/`](lib/README.md)。

```powershell
.\nrf.cmd -Project factory build
# 本阶段可不 flash：
# .\nrf.cmd -Project factory flash
```

共享：`common/` → `modules/nrf54_common`。板目标见 `nrf.env` 的 `BOARD=`。
