from pathlib import Path
import re

p = Path(__file__).resolve().parent / "static" / "index.html"
t = p.read_text(encoding="utf-8")
print("bleBar", "bleBar" in t)
print("exportPlanesForNrf", "exportPlanesForNrf" in t)
print("wireBleUi", "wireBleUi" in t)
print("__nrfBleEvent", "__nrfBleEvent" in t)
i = t.find("window.__nrfBleEvent")
print(t[i : i + 1200])

t2 = t.replace(
    """const state = {
      width: 400,
      height: 300,""",
    """const state = {
      width: 128,
      height: 296,""",
)

# After devices list built, set status
old = """          });
        }
      }
    };

    document.addEventListener("DOMContentLoaded", () => {
"""
# find devices branch end - look for pattern after devices handling
if "found ${ev.data.length}" not in t2:
    t2 = t2.replace(
        """        } else {
          ev.data.forEach((d, i) => {
            const opt = document.createElement("option");
            opt.value = d.address;
            opt.textContent = `${d.name || "?"}  [${d.address}]  (${d.rssi != null ? d.rssi + " dBm" : "?"})`;
            if (i === 0) opt.selected = true;
            sel.appendChild(opt);
          });
        }
      }
    };""",
        """        } else {
          ev.data.forEach((d, i) => {
            const opt = document.createElement("option");
            opt.value = d.address;
            opt.textContent = `${d.name || "?"}  [${d.address}]  (${d.rssi != null ? d.rssi + " dBm" : "?"})`;
            if (i === 0) opt.selected = true;
            sel.appendChild(opt);
          });
          setBleStatus(`found ${ev.data.length}`, "ok");
        }
      }
    };""",
    )

p.write_text(t2, encoding="utf-8")
print("state width fixed", "width: 128" in t2)
