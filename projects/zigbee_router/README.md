# zigbee_router

Zigbee **Router** + **OnOff Server（EP1）** 联调：入网 / 退网 + 网关 `znp light` 控板载绿灯。  
自 [`zigbee_ed`](../zigbee_ed/)（原 `zigbee_demo`）分出；射频常开，可作中继 / ED parent。

板级脚与按键 UX 与 `zigbee_ed` 相同。**无** BLE、EPD、PN5180。**禁止**本机做 Coordinator。

| 层 | 路径 |
|----|------|
| 产品（按键/灯效/日志） | `src/app.c` |
| Zigbee 协议（本工程私有） | `lib/zigbee/` |

- 需求：[`doc/req_nrf54_zigbee_end_device.md`](../../doc/req_nrf54_zigbee_end_device.md)（允许 Router）  
- ED 镜像：[`../zigbee_ed`](../zigbee_ed/)  
- 系统角色：[`../zigbee_ed/README_zigbee_system.md`](../zigbee_ed/README_zigbee_system.md)

## 与 zigbee_ed 的差异

| 项 | zigbee_ed | zigbee_router |
|----|-----------|---------------|
| 角色 | `CONFIG_ZIGBEE_ROLE_END_DEVICE` | `CONFIG_ZIGBEE_ROLE_ROUTER` |
| ZBOSS 内存 | `zb_mem_config_custom.h`（ZED） | `zb_mem_config_med.h`（对齐 light_bulb） |
| 休眠 | 可睡 | **常开**（中继） |
| 本期验收 | 入网 + 控灯 | **入网 + 控灯**（中继另测） |

## 依赖 / 板级 / ZCL / 按键

与 `zigbee_ed` 相同：NCS + ncs-zigbee v1.4.0；信道掩码 11/15/20/25；EP1 OnOff `0x0006`。

| 按键 | 行为 |
|------|------|
| BTN1 短按 | 60s 配网窗 + Steering |
| BTN2 双击 | leave（BDB 本地复位，不停自动搜网） |
| BTN2 **长按 3s** | **擦 `zboss_nvram` + 冷复位**（ED↔Router 换角色 / 脏网表） |

## 构建 / 烧录

```powershell
.\nrf.cmd -Project zigbee_router build
.\nrf.cmd -Project zigbee_router flash
```

**重要**：

- 从 ED 换成 Router 后先 **BTN2 长按 3s** 清本机 `zboss_nvram`，再 `znp join on` + BTN1。  
- 若出现关联后约 10s leave、无法 `STEERING OK`：先查协调器侧脏表 / 旧子设备残留（ZNP clear / 删设备），再查空中 TCLK；本机仅以 `sig=10 st=0` 为真正入网。

配网健壮性（相对裸 Steering）：

- 仅 `STEERING` 且 status OK 才报 joined（避免过早掐 TCLK）  
- 半入网（`ZB_JOINED` 但无 STEERING OK）→ 强制本地 leave，禁止死等  
- leave 完成前 defer steer；leave 后再开 Steering 间隔 3s  

## ZigBridge 验收（本期）

1. 网关 `znp join on 180` → 本机 BTN1 → `zigbee: joined …`  
2. 网关侧确认设备为 **Router**（非 Sleepy ED）  
3. `znp light on` / `off` / `toggle` → 绿灯 + `light: …`  
4. leave 后再入网 + 再控灯 ≥1  

中继（第二台 ED 只够到本板）：后续加测，非本期必须。
