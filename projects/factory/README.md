# factory

厂测（Factory）工程骨架。生命周期在 [`src/`](src/README.md)；串口命令在 [`lib/cmd/`](lib/cmd/)。

MCUBoot 已启用（共享配置见 [`bootloader/`](../../bootloader/README.md)）：双槽 + `boot_share`，开发阶段不验签。TY 板 UART：P1.09 / P1.10 @ 115200。

```powershell
.\nrf.cmd -Project factory build
.\nrf.cmd -Project factory flash
```

期望串口：MCUBoot 横幅 → `Jumping to the first image slot` → NCS/APP 横幅 → `factory printk ok` / `factory heartbeat`。
