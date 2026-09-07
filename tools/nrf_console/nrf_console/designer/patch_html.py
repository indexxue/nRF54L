"""Patch designer index.html for nRF BLE bridge + 128x296 defaults."""

from __future__ import annotations

from pathlib import Path

HTML = Path(__file__).resolve().parent / "static" / "index.html"


BLE_CSS = """
    .ble-bar {
      display: flex;
      align-items: center;
      gap: 0.35rem;
      flex-wrap: wrap;
      width: 100%;
      padding: 0.35rem 1rem 0.55rem;
      border-bottom: 1px solid var(--line);
      background: rgba(16, 24, 32, 0.96);
    }
    .ble-bar .ble-title {
      font-size: 0.66rem;
      letter-spacing: 0.08em;
      text-transform: uppercase;
      color: var(--accent);
      margin-right: 0.35rem;
    }
    .ble-bar input[type="text"] { width: 110px; }
    .ble-bar select { min-width: 220px; max-width: 360px; }
    .ble-bar .ble-status { font-family: var(--mono); font-size: 0.72rem; color: var(--muted); margin-left: 0.25rem; }
    .ble-bar .ble-status.ok { color: var(--ok); }
    .ble-bar .ble-status.err { color: var(--err); }
"""

BLE_BAR_HTML = """
  <div class="ble-bar" id="bleBar">
    <span class="ble-title">BLE</span>
    <input type="text" id="bleFilter" value="eink_tag" title="name filter" />
    <button class="sm" id="btnBleScan" type="button">Scan</button>
    <select id="bleDevice"><option value="">(no devices)</option></select>
    <button class="sm primary" id="btnBleConnect" type="button">Connect</button>
    <button class="sm" id="btnBleDisconnect" type="button" disabled>Disconnect</button>
    <button class="sm primary" id="btnBleUpload" type="button" disabled>上传到标签</button>
    <span class="ble-status" id="bleStatus">bridge offline</span>
  </div>
"""

BLE_JS = r"""
    /* ==== nRF54 BLE bridge (pywebview) ==== */
    function bleApi() {
      try {
        if (window.pywebview && window.pywebview.api) return window.pywebview.api;
      } catch (_) {}
      return null;
    }

    function setBleStatus(text, kind) {
      const el = $("bleStatus");
      if (!el) return;
      el.textContent = text;
      el.className = "ble-status" + (kind ? " " + kind : "");
    }

    function bytesToBase64(u8) {
      let s = "";
      const chunk = 0x8000;
      for (let i = 0; i < u8.length; i += chunk) {
        s += String.fromCharCode.apply(null, u8.subarray(i, i + chunk));
      }
      return btoa(s);
    }

    /** Pack planes for GDEY029Z95 / nRF firmware: BW 1=white, RED 1=red, MSB-left. */
    function exportPlanesForNrf() {
      ensureBgCache(true);
      const { width, height } = getSize();
      const doc = composeDocument({ preview: false });
      const { blackMask, redMask } = imageDataToMasks(doc);
      const msbFirst = true;
      // BW: black ink -> bit 0 (inkIs1=false). RED: red ink -> bit 1 (inkIs1=true).
      const blackBytes = packPlane(blackMask, width, height, msbFirst, false);
      const redBytes = packPlane(redMask, width, height, msbFirst, true);
      return {
        width,
        height,
        bw_b64: bytesToBase64(blackBytes),
        red_b64: bytesToBase64(redBytes),
        bw_len: blackBytes.length,
        red_len: redBytes.length,
      };
    }

    async function bleScan() {
      const api = bleApi();
      if (!api) { setBleStatus("no pywebview bridge", "err"); return; }
      const filter = ($("bleFilter").value || "").trim();
      setBleStatus("scanning…");
      try {
        const r = await api.scan(filter);
        if (!r || !r.ok) {
          setBleStatus((r && r.error) || "scan failed", "err");
          return;
        }
        const sel = $("bleDevice");
        sel.innerHTML = "";
        const devices = r.devices || [];
        if (!devices.length) {
          sel.innerHTML = '<option value="">(no devices)</option>';
          setBleStatus("no devices", "err");
          return;
        }
        devices.forEach((d, i) => {
          const opt = document.createElement("option");
          opt.value = d.address;
          opt.textContent = `${d.name || "?"}  [${d.address}]  (${d.rssi != null ? d.rssi + " dBm" : "?"})`;
          if (i === 0) opt.selected = true;
          sel.appendChild(opt);
        });
        setBleStatus(`found ${devices.length}`, "ok");
      } catch (e) {
        setBleStatus(String(e), "err");
      }
    }

    async function bleConnect() {
      const api = bleApi();
      if (!api) { setBleStatus("no pywebview bridge", "err"); return; }
      const addr = $("bleDevice").value;
      if (!addr) { setBleStatus("pick device", "err"); return; }
      const label = $("bleDevice").selectedOptions[0]?.textContent || "";
      const name = (label.split("[")[0] || "").trim();
      setBleStatus("connecting…");
      try {
        const r = await api.connect(addr, name);
        if (!r || !r.ok) {
          setBleStatus((r && r.error) || "connect failed", "err");
          return;
        }
        $("btnBleDisconnect").disabled = false;
        $("btnBleUpload").disabled = false;
        setBleStatus(r.message || "connected", "ok");
      } catch (e) {
        setBleStatus(String(e), "err");
      }
    }

    async function bleDisconnect() {
      const api = bleApi();
      if (!api) return;
      try {
        await api.disconnect();
      } catch (_) {}
      $("btnBleDisconnect").disabled = true;
      $("btnBleUpload").disabled = true;
      setBleStatus("disconnected");
    }

    async function bleUpload() {
      const api = bleApi();
      if (!api) { setBleStatus("no pywebview bridge", "err"); return; }
      let planes;
      try {
        planes = exportPlanesForNrf();
      } catch (e) {
        setBleStatus("export failed: " + e, "err");
        return;
      }
      if (planes.width !== 128 || planes.height !== 296) {
        setBleStatus(`upload needs 128×296 (now ${planes.width}×${planes.height})`, "err");
        setStatus(`固件目前仅支持 128×296，请改尺寸后再上传`, "err");
        return;
      }
      setBleStatus("uploading…");
      $("btnBleUpload").disabled = true;
      try {
        const r = await api.upload(planes.width, planes.height, planes.bw_b64, planes.red_b64);
        if (!r || !r.ok) {
          setBleStatus((r && r.error) || "upload failed", "err");
        } else {
          setBleStatus(r.message || "upload started", "ok");
          setStatus("BLE 上传中…", "ok");
        }
      } catch (e) {
        setBleStatus(String(e), "err");
      } finally {
        $("btnBleUpload").disabled = false;
      }
    }

    // Called from Python event pump
    window.__nrfBleEvent = function (ev) {
      if (!ev || !ev.kind) return;
      if (ev.kind === "status" || ev.kind === "log") {
        if (ev.message) setBleStatus(ev.message, ev.kind === "log" ? "ok" : undefined);
      } else if (ev.kind === "error") {
        setBleStatus(ev.message || "error", "err");
      } else if (ev.kind === "connected") {
        const on = !!ev.data;
        $("btnBleDisconnect").disabled = !on;
        $("btnBleUpload").disabled = !on;
        setBleStatus(on ? "connected" : "disconnected", on ? "ok" : undefined);
      } else if (ev.kind === "devices" && Array.isArray(ev.data)) {
        const sel = $("bleDevice");
        sel.innerHTML = "";
        if (!ev.data.length) {
          sel.innerHTML = '<option value="">(no devices)</option>';
        } else {
          ev.data.forEach((d, i) => {
            const opt = document.createElement("option");
            opt.value = d.address;
            opt.textContent = `${d.name || "?"}  [${d.address}]  (${d.rssi != null ? d.rssi + " dBm" : "?"})`;
            if (i === 0) opt.selected = true;
            sel.appendChild(opt);
          });
        }
      }
    };

    document.addEventListener("DOMContentLoaded", () => {
      // re-bind if DOM already ready below; also call at end of script
    });

    function wireBleUi() {
      const scan = $("btnBleScan");
      if (!scan || scan.dataset.wired) return;
      scan.dataset.wired = "1";
      scan.addEventListener("click", () => { bleScan(); });
      $("btnBleConnect").addEventListener("click", () => { bleConnect(); });
      $("btnBleDisconnect").addEventListener("click", () => { bleDisconnect(); });
      $("btnBleUpload").addEventListener("click", () => { bleUpload(); });
      if (bleApi()) setBleStatus("bridge ready", "ok");
      else setBleStatus("open via python main.py", "err");
    }
"""


def main() -> None:
    t = HTML.read_text(encoding="utf-8")

    t = t.replace(
        "<title>EPD · 价签 / 广告排版</title>",
        "<title>EPD Tag Designer · nRF</title>",
    )
    t = t.replace(
        '<span class="sub">价签 / 广告 · 2.13″–4.2″</span>\n      <a href="/">pi-lab</a>',
        '<span class="sub">DIY · BLE → eink_tag</span>',
    )

    if ".ble-bar" not in t:
        t = t.replace("    @media (max-width: 960px) {", BLE_CSS + "\n    @media (max-width: 960px) {")

    if 'id="bleBar"' not in t:
        t = t.replace("  </header>\n\n  <div class=\"shell\">", "  </header>\n" + BLE_BAR_HTML + "\n  <div class=\"shell\">")

    # defaults: 128x296
    t = t.replace('id="width" min="8" max="800" value="400"', 'id="width" min="8" max="800" value="128"')
    t = t.replace('id="height" min="8" max="800" value="300"', 'id="height" min="8" max="800" value="296"')
    t = t.replace('presetEl.value = "400x300";', 'presetEl.value = "128x296";')

    # inject BLE JS before closing script
    if "exportPlanesForNrf" not in t:
        # insert before final </script>
        marker = "\n  </script>\n</body>"
        if marker not in t:
            raise SystemExit("script end marker not found")
        # also call wireBleUi near end of existing init - append after last listener block
        t = t.replace(
            marker,
            "\n" + BLE_JS + "\n    wireBleUi();\n    window.addEventListener('pywebviewready', () => { setBleStatus('bridge ready', 'ok'); wireBleUi(); });\n  </script>\n</body>",
        )

    # primary upload button label already added; enable btnSend stays for serial

    HTML.write_text(t, encoding="utf-8")
    print("patched", HTML, "bytes", HTML.stat().st_size)


if __name__ == "__main__":
    main()
