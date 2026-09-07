"""Patch designer HTML: poll BLE events from JS (thread-safe)."""

from __future__ import annotations

from pathlib import Path

HTML = Path(__file__).resolve().parent / "static" / "index.html"


def main() -> None:
    t = HTML.read_text(encoding="utf-8")

    old_wire = """    function wireBleUi() {
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
    }

    wireBleUi();
    window.addEventListener('pywebviewready', () => { setBleStatus('bridge ready', 'ok'); wireBleUi(); });
"""

    new_wire = """    async function blePollOnce() {
      const api = bleApi();
      if (!api || typeof api.poll_events !== "function") return;
      try {
        const evs = await api.poll_events();
        if (!Array.isArray(evs)) return;
        for (const ev of evs) {
          window.__nrfBleEvent(ev);
        }
      } catch (_) {}
    }

    function wireBleUi() {
      const scan = $("btnBleScan");
      if (!scan || scan.dataset.wired) return;
      scan.dataset.wired = "1";
      scan.addEventListener("click", () => { bleScan(); });
      $("btnBleLink").addEventListener("click", () => { bleToggleLink(); });
      $("btnBleUpload").addEventListener("click", () => { bleUpload(); });
      $("bleDevice").addEventListener("change", () => { syncBleChrome(); });
      syncBleChrome();
      if (!state.blePollTimer) {
        state.blePollTimer = setInterval(() => { blePollOnce(); }, 100);
      }
      if (bleApi()) setBleStatus("bridge ready", "ok");
      else setBleStatus("open via python main.py", "err");
    }

    wireBleUi();
    window.addEventListener("pywebviewready", () => {
      setBleStatus("bridge ready", "ok");
      wireBleUi();
      blePollOnce();
    });
"""

    if old_wire not in t:
        raise SystemExit("wireBleUi block not found — HTML may already be patched")
    t = t.replace(old_wire, new_wire)

    # ensure state has blePollTimer field
    if "blePollTimer:" not in t:
        t = t.replace(
            "      bleConnected: false,\n      bleBusy: false,\n    };",
            "      bleConnected: false,\n      bleBusy: false,\n      blePollTimer: null,\n    };",
        )

    # On connect click, mark busy immediately so UI doesn't double-fire
    old_connect = """      setBleStatus("connecting…");
      syncBleChrome();
      try {
        const r = await api.connect(addr, name);"""
    new_connect = """      setBleStatus("connecting…");
      state.bleBusy = true;
      syncBleChrome();
      try {
        const r = await api.connect(addr, name);"""
    if old_connect in t:
        t = t.replace(old_connect, new_connect, 1)
        # if connect API returns error, clear busy
        t = t.replace(
            """        if (!r || !r.ok) {
          setBleStatus((r && r.error) || "connect failed", "err");
          return;
        }
        setBleStatus(r.message || "connecting…");
      } catch (e) {
        setBleStatus(String(e), "err");
      }
    }

    async function bleDisconnect() {""",
            """        if (!r || !r.ok) {
          setBleStatus((r && r.error) || "connect failed", "err");
          state.bleBusy = false;
          syncBleChrome();
          return;
        }
        setBleStatus(r.message || "connecting…");
      } catch (e) {
        setBleStatus(String(e), "err");
        state.bleBusy = false;
        syncBleChrome();
      }
    }

    async function bleDisconnect() {""",
        )

    HTML.write_text(t, encoding="utf-8")
    print("patched", HTML.stat().st_size)


if __name__ == "__main__":
    main()
