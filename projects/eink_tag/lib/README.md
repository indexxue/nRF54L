# lib/

本工程其它**私有业务**模块。默认空。

片外总线器件（EPD / PN5180）放 [`../source/`](../source/)，不要放这里。

| 代码 | 放哪里 |
|------|--------|
| 多工程复用 | 仓库 `common/` / `cbb/` |
| SoC 外设表 | `board/board_config.c` |
| 片外器件胶水 | `source/<device>/` |
| 仅本工程业务 | 本目录 `lib/<module>/` |
