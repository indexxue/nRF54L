from pathlib import Path

p = Path(__file__).resolve().parent / "static" / "index.html"
t = p.read_text(encoding="utf-8")

old_scan_start = "    async function bleScan() {"
old_connect_start = "    async function bleConnect() {"

new_scan = '''    async function bleScan() {
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
        setBleStatus(r.message || "scanning…");
      } catch (e) {
        setBleStatus(String(e), "err");
      }
    }
'''

new_connect = '''    async function bleConnect() {
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
        setBleStatus(r.message || "connecting…");
      } catch (e) {
        setBleStatus(String(e), "err");
      }
    }
'''


def replace_function(src: str, start_marker: str, new_body: str) -> str:
    i = src.find(start_marker)
    if i < 0:
        raise SystemExit(f"missing {start_marker}")
    # find next "    async function" or "    function wireBleUi" after i+1
    rest = src[i + len(start_marker) :]
    ends = []
    for marker in ("\n    async function ", "\n    function wireBleUi", "\n    // Called from Python"):
        j = rest.find(marker)
        if j >= 0:
            ends.append(j)
    if not ends:
        raise SystemExit("cannot find end of function")
    j = min(ends)
    return src[:i] + new_body + rest[j:]


t2 = replace_function(t, old_scan_start, new_scan)
t2 = replace_function(t2, old_connect_start, new_connect)

# improve __nrfBleEvent for log/status during upload
t2 = t2.replace(
    """    window.__nrfBleEvent = function (ev) {
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
""",
    """    window.__nrfBleEvent = function (ev) {
      if (!ev || !ev.kind) return;
      if (ev.kind === "status") {
        if (ev.message) setBleStatus(ev.message);
      } else if (ev.kind === "log") {
        if (ev.message && /upload|connected|GATT|EPD/i.test(ev.message)) {
          setBleStatus(ev.message.slice(0, 80), "ok");
        }
      } else if (ev.kind === "error") {
        setBleStatus(ev.message || "error", "err");
      } else if (ev.kind === "connected") {
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
      } else if (ev.kind === "devices" && Array.isArray(ev.data)) {
""",
)

p.write_text(t2, encoding="utf-8")
print("updated bleScan/connect/events")
