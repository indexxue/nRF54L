# zigbee_ed <-> Tuya mapping

> Main product: 显示板 PID c0rpr0hl.
> Light PID agbfpcdx archived in section 6.

## 1. Product (显示板)

| Item | Value |
|------|-------|
| Project | projects/zigbee_ed |
| Gateway | LQ-WG03 |
| PID | c0rpr0hl |
| Firmware key | key3prr8 |
| **ManufacturerName (Basic 0x0004)** | **`_TZ3000_c0rpr0hl`** (prefix `_TZ3000_` + PID) |
| **ModelIdentifier (Basic 0x0005)** | **`TS0601`** (custom DP common Mode ID) |
| ApplicationVersion | 0x40 (=1.0.0 Tuya nibble encoding) |
| DP xlsx | projects/zigbee_ed/显示板(c0rpr0hl)_11_16.xlsx |
| Endpoint | 1 |
| Channel mask | 11-26 (0x07FFF800) |

**Why App showed 触控开关:** old mfr=`TY` model=`disp_board` is not Tuya PID encoding; gateway falls back to HA OnOff = switch.

## 2. ZCL

| Cluster | ID | Role |
|---------|-----|------|
| Basic | 0x0000 | Server |
| Identify | 0x0003 | Server |
| OnOff | 0x0006 | Server (DP102 LED_G) |
| DispText | 0xFC10 | EP handler (DP101 EPD) |

### OnOff <-> DP102 switch_led
Cluster 0x0006; On 0x01 / Off 0x00.

### DispText <-> DP101 display_text
Cluster 0xFC10; cmd 0x00; payload [len]+ASCII (<=96).

## 3. Platform DPs

| DP ID | Identifier | Type | ZCL |
|-------|------------|------|-----|
| 101 | display_text | string rw | EP1 0xFC10 cmd 0x00 |
| 102 | switch_led | bool rw | EP1 OnOff 0x0006 |

Conversion file still required for DP101/102 payload mapping; PID binding needs correct Basic strings above.

## 4. Acceptance

1. Flash; log must show `mfr=_TZ3000_c0rpr0hl model=TS0601`
2. Leave + delete App device + re-pair
3. App product should be 显示板 (not 触控开关)
4. Text + switch DPs work

## 5. History

- 2026-11-16: fix Tuya identity — ManufacturerName embeds PID; model TS0601

## 6. Archive: light agbfpcdx

PID agbfpcdx / key keyanydv — OnOff lab only.
