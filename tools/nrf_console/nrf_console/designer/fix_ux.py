"""Fix bridge JSON + designer UX: toggle link, size-safe demos, 防呆."""

from __future__ import annotations

from pathlib import Path

HTML = Path(__file__).resolve().parent / "static" / "index.html"


def patch_bridge() -> None:
    bridge = Path(__file__).resolve().parent / "bridge.py"
    text = bridge.read_text(encoding="utf-8")
    old = '''    def _on_event(self, ev: UiEvent) -> None:
        data: Any = ev.data
        if ev.kind == "devices" and data is not None:
            data = [
                {"name": d.name, "address": d.address, "rssi": d.rssi}
                for d in data
            ]
        self._emit_js({"kind": ev.kind, "message": ev.message, "data": data})
'''
    new = '''    def _on_event(self, ev: UiEvent) -> None:
        # Only JSON-safe payloads — never forward CmdFrame / raw objects.
        data: Any = None
        if ev.kind == "devices" and ev.data is not None:
            data = [
                {"name": d.name, "address": d.address, "rssi": d.rssi}
                for d in ev.data
            ]
        elif ev.kind == "connected":
            data = bool(ev.data)
        elif ev.kind == "busy" and isinstance(ev.data, dict):
            data = {
                "op": str(ev.data.get("op", "idle")),
                "connected": bool(ev.data.get("connected")),
            }
        elif ev.kind == "upload":
            data = bool(ev.data)
        self._emit_js({"kind": ev.kind, "message": ev.message, "data": data})
'''
    if old not in text:
        raise SystemExit("bridge _on_event block not found")
    bridge.write_text(text.replace(old, new), encoding="utf-8")
    print("bridge ok")


def patch_html() -> None:
    t = HTML.read_text(encoding="utf-8")

    # Fix accidental double closing tags at EOF
    while t.rstrip().endswith("</html>"):
        t2 = t.rstrip()
        # keep one </html>
        if t2.count("</html>") <= 1:
            break
        # remove trailing duplicate
        idx = t2.rfind("</html>")
        prev = t2.rfind("</html>", 0, idx)
        if prev < 0:
            break
        t = t2[:idx].rstrip() + "\n"

    # BLE bar: single Connect/Disconnect toggle
    t = t.replace(
        """    <button class="sm primary" id="btnBleConnect" type="button">Connect</button>
    <button class="sm" id="btnBleDisconnect" type="button" disabled>Disconnect</button>
    <button class="sm primary" id="btnBleUpload" type="button" disabled>上传到标签</button>""",
        """    <button class="sm primary" id="btnBleLink" type="button">Connect</button>
    <button class="sm primary" id="btnBleUpload" type="button" disabled>上传到标签</button>""",
    )

    # Template demos: keep 2.9 smoke/gfx
    t = t.replace(
        """        <div class="tool-grid" style="grid-template-columns:repeat(2,1fr)">
          <button type="button" data-tpl="price_tag">经典价签</button>
          <button type="button" data-tpl="promo">促销条</button>
          <button type="button" data-tpl="strike">划线价</button>
          <button type="button" data-tpl="poker">扑克牌</button>
        </div>""",
        """        <div class="tool-grid" style="grid-template-columns:repeat(2,1fr)">
          <button type="button" data-tpl="price_tag">经典价签</button>
          <button type="button" data-tpl="promo">促销条</button>
          <button type="button" data-tpl="strike">划线价</button>
          <button type="button" data-tpl="poker">扑克牌</button>
          <button type="button" data-tpl="smoke">三色条 demo</button>
          <button type="button" data-tpl="gfx">GFX demo</button>
        </div>
        <p class="hint" style="margin-top:0.35rem">换尺寸会按当前模板重排，避免拉伸变形。默认 2.9″(128×296)。</p>""",
    )

    # Track activeTemplate in state
    if "activeTemplate:" not in t:
        t = t.replace(
            "      pokerSuit: \"H\",\n    };",
            "      pokerSuit: \"H\",\n      activeTemplate: \"price_tag\",\n      bleConnected: false,\n      bleBusy: false,\n    };",
        )

    # applyPreset: re-apply template after size change
    old_preset = '''    function applyPreset(value) {
      if (value === "custom") return;
      const [w, h] = value.split("x").map(Number);
      state.syncingPreset = true;
      $("width").value = w;
      $("height").value = h;
      state.syncingPreset = false;
      updateSizeStats();
      state.autoFitOnce = true;
      scheduleRender({ invalidateBg: true });
    }'''
    new_preset = '''    function applyPreset(value) {
      if (value === "custom") return;
      const [w, h] = value.split("x").map(Number);
      state.syncingPreset = true;
      $("width").value = w;
      $("height").value = h;
      state.syncingPreset = false;
      updateSizeStats();
      state.autoFitOnce = true;
      if (state.activeTemplate) {
        applyTemplate(state.activeTemplate);
      } else {
        scheduleRender({ invalidateBg: true });
      }
    }'''
    if old_preset not in t:
        raise SystemExit("applyPreset not found")
    t = t.replace(old_preset, new_preset)

    # onSizeInput: also reflow template
    old_size = '''    function onSizeInput() {
      if (state.syncingPreset) return;
      syncPresetFromSize();
      state.autoFitOnce = true;
      scheduleRender({ invalidateBg: true });
    }'''
    new_size = '''    function onSizeInput() {
      if (state.syncingPreset) return;
      syncPresetFromSize();
      state.autoFitOnce = true;
      if (state.activeTemplate) {
        applyTemplate(state.activeTemplate);
      } else {
        scheduleRender({ invalidateBg: true });
      }
    }'''
    t = t.replace(old_size, new_size)

    # Replace applyTemplate with aspect-aware + smoke/gfx demos
    start = t.find("    function applyTemplate(name) {")
    end = t.find("    // —— inline edit ——")
    if start < 0 or end < 0:
        raise SystemExit("applyTemplate bounds not found")

    new_apply = r'''    function applyTemplate(name) {
      const { width, height } = getSize();
      state.layers = [];
      state.activeTemplate = name;
      const landscape = width >= height;
      const m = Math.min(width, height);
      const long = Math.max(width, height);
      const pad = Math.max(4, Math.round(m * 0.05));
      $("pokerPanel").style.display = name === "poker" ? "" : "none";

      if (name === "poker") {
        applyPokerCard();
        return;
      }

      if (name === "smoke") {
        // 2.9" smoke: B|R|W bands along short axis of panel orientation
        if (landscape) {
          const band = Math.floor(height / 3);
          state.layers.push({ ...defaultLayer("rect"), x: 0, y: 0, w: width, h: band, fill: true, color: "black" });
          state.layers.push({ ...defaultLayer("rect"), x: 0, y: band, w: width, h: band, fill: true, color: "red" });
          state.layers.push({ ...defaultLayer("rect"), x: 0, y: band * 2, w: width, h: height - band * 2, fill: true, color: "white" });
        } else {
          const band = Math.floor(width / 3);
          state.layers.push({ ...defaultLayer("rect"), x: 0, y: 0, w: band, h: height, fill: true, color: "black" });
          state.layers.push({ ...defaultLayer("rect"), x: band, y: 0, w: band, h: height, fill: true, color: "red" });
          state.layers.push({ ...defaultLayer("rect"), x: band * 2, y: 0, w: width - band * 2, h: height, fill: true, color: "white" });
        }
        state.layers.push({
          ...defaultLayer("roundrect"),
          x: 0, y: 0, w: width, h: height,
          radius: 0, stroke: Math.max(1, Math.round(m / 80)),
          color: "black", fill: false,
        });
      } else if (name === "gfx") {
        const fs = Math.max(10, Math.round(m * 0.08));
        state.layers.push({
          ...defaultLayer("rect"), x: 0, y: 0, w: width, h: Math.max(3, Math.round(m * 0.02)),
          fill: true, color: "black",
        });
        state.layers.push({
          ...defaultLayer("text"), text: "GFX/ICON TEST",
          x: pad, y: pad + Math.round(m * 0.02),
          w: width - pad * 2, h: Math.round(fs * 1.4),
          fontSize: fs, color: "black", fontKey: "body", fontWeight: "700", align: "left",
        });
        const box = Math.round(m * 0.22);
        const y0 = pad + Math.round(m * 0.18);
        state.layers.push({
          ...defaultLayer("rect"),
          x: pad, y: y0, w: box, h: box, fill: true, color: "black",
        });
        state.layers.push({
          ...defaultLayer("roundrect"),
          x: pad + box + Math.round(m * 0.06), y: y0, w: box, h: box,
          radius: Math.round(m * 0.03), stroke: Math.max(1, Math.round(m / 90)),
          color: "black", fill: false,
        });
        state.layers.push({
          ...defaultLayer("emoji"), text: "♥",
          x: pad, y: y0 + box + Math.round(m * 0.08),
          w: Math.round(m * 0.14), h: Math.round(m * 0.14),
          fontSize: Math.round(m * 0.12), color: "red",
        });
        state.layers.push({
          ...defaultLayer("text"), text: "GFX TEST OK",
          x: pad, y: height - pad - Math.round(fs * 1.5),
          w: width - pad * 2, h: Math.round(fs * 1.4),
          fontSize: fs, color: "black", fontKey: "body", fontWeight: "700",
        });
      } else if (name === "price_tag") {
        state.layers.push({
          ...defaultLayer("roundrect"),
          x: pad, y: pad, w: width - pad * 2, h: height - pad * 2,
          radius: Math.max(3, Math.round(m * 0.025)),
          stroke: Math.max(1, Math.round(m / 110)),
          color: "black", fill: false,
        });
        if (landscape) {
          const titleFs = Math.max(11, Math.round(m * 0.12));
          const priceFs = Math.max(28, Math.round(m * 0.42));
          state.layers.push({
            ...defaultLayer("text"), text: "精选商品",
            x: pad * 2, y: pad * 2,
            w: Math.round(width * 0.4), h: Math.round(titleFs * 1.4),
            fontSize: titleFs, color: "black", align: "left", fontKey: "body", fontWeight: "700",
          });
          state.layers.push({
            ...defaultLayer("pricegroup"), text: "12.90", currency: "¥", currencyPos: "topleft",
            x: Math.round(width * 0.38), y: Math.round((height - priceFs * 1.1) / 2),
            w: Math.round(width * 0.58), h: Math.round(priceFs * 1.15),
            fontSize: priceFs, color: "red", fontKey: "price", fontWeight: "900",
          });
        } else {
          const titleFs = Math.max(12, Math.round(height * 0.055));
          const priceFs = Math.max(36, Math.round(height * 0.28));
          state.layers.push({
            ...defaultLayer("text"), text: "精选商品",
            x: pad + Math.round(pad * 0.6), y: pad + Math.round(pad * 0.55),
            w: width - pad * 3, h: Math.round(titleFs * 1.45),
            fontSize: titleFs, color: "black", align: "left", fontKey: "body", fontWeight: "700",
          });
          state.layers.push({
            ...defaultLayer("pricegroup"), text: "12.90", currency: "¥", currencyPos: "topleft",
            x: pad + Math.round(pad * 0.5), y: Math.round(height * 0.36),
            w: width - pad * 2.5, h: Math.round(priceFs * 1.15),
            fontSize: priceFs, color: "red", fontKey: "price", fontWeight: "900",
          });
        }
      } else if (name === "promo") {
        state.layers.push({
          ...defaultLayer("rect"), x: 0, y: 0, w: width, h: height, fill: true, color: "black",
        });
        const inset = Math.max(4, Math.round(m * 0.05));
        if (landscape) {
          state.layers.push({
            ...defaultLayer("rect"),
            x: inset, y: inset, w: Math.round(width * 0.55), h: height - inset * 2,
            fill: false, color: "red", stroke: Math.max(2, Math.round(m / 55)),
          });
          state.layers.push({
            ...defaultLayer("text"), text: "限时特惠",
            x: inset * 2, y: Math.round(height * 0.22),
            w: Math.round(width * 0.5), h: Math.round(m * 0.35),
            fontSize: Math.max(18, Math.round(m * 0.28)),
            color: "red", align: "center", fontKey: "body", fontWeight: "900",
          });
          state.layers.push({
            ...defaultLayer("text"), text: "全场低至 5 折",
            x: Math.round(width * 0.58), y: Math.round(height * 0.35),
            w: Math.round(width * 0.38), h: Math.round(m * 0.28),
            fontSize: Math.max(12, Math.round(m * 0.14)),
            color: "white", align: "center", fontKey: "body", fontWeight: "700",
          });
        } else {
          state.layers.push({
            ...defaultLayer("rect"),
            x: inset, y: Math.round(height * 0.12),
            w: width - inset * 2, h: Math.round(height * 0.48),
            fill: false, color: "red", stroke: Math.max(2, Math.round(m / 60)),
          });
          state.layers.push({
            ...defaultLayer("text"), text: "限时特惠",
            x: inset, y: Math.round(height * 0.2),
            w: width - inset * 2, h: Math.round(height * 0.32),
            fontSize: Math.max(20, Math.round(height * 0.18)),
            color: "red", align: "center", fontKey: "body", fontWeight: "900",
          });
          state.layers.push({
            ...defaultLayer("text"), text: "全场低至 5 折",
            x: inset, y: Math.round(height * 0.68),
            w: width - inset * 2, h: Math.round(height * 0.16),
            fontSize: Math.max(12, Math.round(height * 0.08)),
            color: "white", align: "center", fontKey: "body", fontWeight: "700",
          });
        }
      } else if (name === "strike") {
        if (landscape) {
          const strikeSize = Math.max(14, Math.round(m * 0.16));
          const saleSize = Math.max(28, Math.round(m * 0.4));
          state.layers.push({
            ...defaultLayer("text"), text: "原价",
            x: pad, y: pad, w: Math.round(width * 0.2), h: Math.round(m * 0.14),
            fontSize: Math.max(10, Math.round(m * 0.1)), color: "black", fontKey: "body", fontWeight: "400",
          });
          state.layers.push({
            ...defaultLayer("strike"), text: "29.90", currency: "¥", currencyPos: "baseline",
            x: pad, y: Math.round(height * 0.28),
            w: Math.round(width * 0.35), h: Math.round(strikeSize * 1.3),
            fontSize: strikeSize, color: "black", fontKey: "price", fontWeight: "700",
          });
          state.layers.push({
            ...defaultLayer("pricegroup"), text: "19.90", currency: "¥", currencyPos: "topleft",
            x: Math.round(width * 0.4), y: Math.round((height - saleSize) / 2),
            w: Math.round(width * 0.55), h: Math.round(saleSize * 1.15),
            fontSize: saleSize, color: "red", fontKey: "price", fontWeight: "900",
          });
        } else {
          const strikeSize = Math.max(14, Math.round(height * 0.09));
          const saleSize = Math.max(36, Math.round(height * 0.28));
          state.layers.push({
            ...defaultLayer("text"), text: "原价",
            x: pad, y: Math.round(height * 0.08),
            w: Math.round(width * 0.35), h: Math.round(height * 0.08),
            fontSize: Math.max(11, Math.round(height * 0.05)), color: "black", fontKey: "body", fontWeight: "400",
          });
          state.layers.push({
            ...defaultLayer("strike"), text: "29.90", currency: "¥", currencyPos: "baseline",
            x: pad, y: Math.round(height * 0.18),
            w: Math.round(width * 0.5), h: Math.round(strikeSize * 1.3),
            fontSize: strikeSize, color: "black", fontKey: "price", fontWeight: "700",
          });
          state.layers.push({
            ...defaultLayer("pricegroup"), text: "19.90", currency: "¥", currencyPos: "topleft",
            x: pad, y: Math.round(height * 0.42),
            w: width - pad * 2, h: Math.round(saleSize * 1.15),
            fontSize: saleSize, color: "red", fontKey: "price", fontWeight: "900",
          });
        }
      }
      selectLayer(state.layers[state.layers.length - 1]?.id || null);
      scheduleRender();
      setStatus(`已套用模板 ${name} · ${width}×${height}`, "ok");
    }

'''
    t = t[:start] + new_apply + t[end:]

    # Replace BLE connect/disconnect JS with toggle + 防呆
    ble_start = t.find("    async function bleScan() {")
    ble_end = t.find("    // Called from Python event pump")
    if ble_start < 0 or ble_end < 0:
        raise SystemExit("ble functions not found")

    new_ble = r'''    async function bleScan() {
      const api = bleApi();
      if (!api) { setBleStatus("no pywebview bridge", "err"); return; }
      if (state.bleBusy) { setBleStatus("busy — wait", "err"); return; }
      if (state.bleConnected) {
        if (!confirm("Scan will disconnect current link (Windows BLE). Continue?")) return;
        await bleDisconnect();
      }
      const filter = ($("bleFilter").value || "").trim();
      setBleStatus("scanning…");
      syncBleChrome();
      try {
        const r = await api.scan(filter);
        if (!r || !r.ok) {
          setBleStatus((r && r.error) || "scan failed", "err");
          return;
        }
        setBleStatus(r.message || "scanning…");
      } catch (e) {
        setBleStatus(String(e), "err");
      }
    }

    async function bleToggleLink() {
      if (state.bleBusy) { setBleStatus("busy — wait", "err"); return; }
      if (state.bleConnected) {
        await bleDisconnect();
      } else {
        await bleConnect();
      }
    }

    async function bleConnect() {
      const api = bleApi();
      if (!api) { setBleStatus("no pywebview bridge", "err"); return; }
      if (state.bleBusy) { setBleStatus("busy — wait", "err"); return; }
      if (state.bleConnected) { setBleStatus("already connected", "ok"); return; }
      const addr = $("bleDevice").value;
      if (!addr) { setBleStatus("pick device", "err"); return; }
      const label = $("bleDevice").selectedOptions[0]?.textContent || "";
      const name = (label.split("[")[0] || "").trim();
      setBleStatus("connecting…");
      syncBleChrome();
      try {
        const r = await api.connect(addr, name);
        if (!r || !r.ok) {
          setBleStatus((r && r.error) || "connect failed", "err");
          return;
        }
        setBleStatus(r.message || "connecting…");
      } catch (e) {
        setBleStatus(String(e), "err");
      }
    }

    async function bleDisconnect() {
      const api = bleApi();
      if (!api) return;
      if (state.bleBusy) { setBleStatus("busy — wait", "err"); return; }
      try {
        await api.disconnect();
      } catch (_) {}
      state.bleConnected = false;
      syncBleChrome();
      setBleStatus("disconnected");
    }

    function syncBleChrome() {
      const link = $("btnBleLink");
      const up = $("btnBleUpload");
      const scan = $("btnBleScan");
      if (!link) return;
      link.textContent = state.bleConnected ? "Disconnect" : "Connect";
      link.classList.toggle("primary", !state.bleConnected);
      link.classList.toggle("danger", state.bleConnected);
      const idle = !state.bleBusy;
      link.disabled = !idle || (!state.bleConnected && !$("bleDevice").value);
      // allow disconnect even if device list empty
      if (state.bleConnected) link.disabled = !idle;
      if (up) up.disabled = !(idle && state.bleConnected);
      if (scan) scan.disabled = !idle;
    }

    async function bleUpload() {
      const api = bleApi();
      if (!api) { setBleStatus("no pywebview bridge", "err"); return; }
      if (!state.bleConnected) { setBleStatus("connect first", "err"); return; }
      if (state.bleBusy) { setBleStatus("busy — wait", "err"); return; }
      let planes;
      try {
        planes = exportPlanesForNrf();
      } catch (e) {
        setBleStatus("export failed: " + e, "err");
        return;
      }
      setBleStatus("uploading…");
      state.bleBusy = true;
      syncBleChrome();
      try {
        const r = await api.upload(planes.width, planes.height, planes.bw_b64, planes.red_b64);
        if (!r || !r.ok) {
          setBleStatus((r && r.error) || "upload failed", "err");
          state.bleBusy = false;
          syncBleChrome();
        } else {
          setBleStatus(r.message || "upload started", "ok");
          setStatus("BLE 上传中…", "ok");
        }
      } catch (e) {
        setBleStatus(String(e), "err");
        state.bleBusy = false;
        syncBleChrome();
      }
    }

'''
    t = t[:ble_start] + new_ble + t[ble_end:]

    # Replace __nrfBleEvent connected/busy handling
    old_ev = '''      } else if (ev.kind === "connected") {
        const on = !!ev.data;
        $("btnBleDisconnect").disabled = !on;
        $("btnBleUpload").disabled = !on;
        setBleStatus(on ? "connected" : "disconnected", on ? "ok" : undefined);
      } else if (ev.kind === "busy") {
        const op = (ev.data && ev.data.op) || "idle";
        const on = !!(ev.data && ev.data.connected);
        $("btnBleDisconnect").disabled = !(on && op === "idle");
        $("btnBleUpload").disabled = !(on && op === "idle");
        if (op === "uploading") setBleStatus("uploading…");
      } else if (ev.kind === "devices" && Array.isArray(ev.data)) {'''

    new_ev = '''      } else if (ev.kind === "connected") {
        state.bleConnected = !!ev.data;
        if (!state.bleConnected) state.bleBusy = false;
        syncBleChrome();
        setBleStatus(state.bleConnected ? "connected" : "disconnected", state.bleConnected ? "ok" : undefined);
      } else if (ev.kind === "busy") {
        const op = (ev.data && ev.data.op) || "idle";
        state.bleConnected = !!(ev.data && ev.data.connected);
        state.bleBusy = op !== "idle";
        syncBleChrome();
        if (op === "uploading") setBleStatus("uploading…");
        else if (op === "scanning") setBleStatus("scanning…");
        else if (op === "connecting") setBleStatus("connecting…");
        else if (state.bleConnected) setBleStatus("connected", "ok");
      } else if (ev.kind === "devices" && Array.isArray(ev.data)) {'''

    if old_ev not in t:
        raise SystemExit("event handler block not found")
    t = t.replace(old_ev, new_ev)

    # After devices update, syncBleChrome
    t = t.replace(
        """          setBleStatus(`found ${ev.data.length}`, "ok");
        }
      }
    };""",
        """          setBleStatus(`found ${ev.data.length}`, "ok");
        }
        syncBleChrome();
      }
    };""",
    )

    # wireBleUi for toggle button
    old_wire = '''    function wireBleUi() {
      const scan = $("btnBleScan");
      if (!scan || scan.dataset.wired) return;
      scan.dataset.wired = "1";
      scan.addEventListener("click", () => { bleScan(); });
      $("btnBleConnect").addEventListener("click", () => { bleConnect(); });
      $("btnBleDisconnect").addEventListener("click", () => { bleDisconnect(); });
      $("btnBleUpload").addEventListener("click", () => { bleUpload(); });
      if (bleApi()) setBleStatus("bridge ready", "ok");
      else setBleStatus("open via python main.py", "err");
    }'''
    new_wire = '''    function wireBleUi() {
      const scan = $("btnBleScan");
      if (!scan || scan.dataset.wired) return;
      scan.dataset.wired = "1";
      scan.addEventListener("click", () => { bleScan(); });
      $("btnBleLink").addEventListener("click", () => { bleToggleLink(); });
      $("btnBleUpload").addEventListener("click", () => { bleUpload(); });
      $("bleDevice").addEventListener("change", () => { syncBleChrome(); });
      syncBleChrome();
      if (bleApi()) setBleStatus("bridge ready", "ok");
      else setBleStatus("open via python main.py", "err");
    }'''
    if old_wire not in t:
        raise SystemExit("wireBleUi not found")
    t = t.replace(old_wire, new_wire)

    # Ensure single </html>
    t = t.replace("</html>\n</html>", "</html>")
    if not t.rstrip().endswith("</html>"):
        t = t.rstrip() + "\n</html>\n"

    HTML.write_text(t, encoding="utf-8")
    print("html ok", HTML.stat().st_size)


if __name__ == "__main__":
    patch_bridge()
    patch_html()
