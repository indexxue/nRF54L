"""Add adjustable procedural background patterns to designer HTML."""

from __future__ import annotations

from pathlib import Path

HTML = Path(__file__).resolve().parent / "static" / "index.html"

OLD_BG_PANEL = """      <div class="panel">
        <div class="block-title">背景图（可选）</div>
        <div class="drop" id="dropZone" role="button" tabindex="0">
          <span class="drop-title">点击或拖放背景图</span>
          <p class="hint" id="fileName">可空 · 仅背景走抖动</p>
        </div>
        <input type="file" id="fileInput" accept="image/png,image/jpeg,image/bmp,image/gif,image/webp,.png,.jpg,.jpeg,.bmp,.gif,.webp" hidden />
        <div class="seg" id="fitSeg" style="margin-top:0.45rem">
          <button type="button" data-fit="contain" class="active">contain</button>
          <button type="button" data-fit="cover">cover</button>
          <button type="button" data-fit="stretch">stretch</button>
        </div>
        <button class="sm" type="button" id="btnClearBg" style="margin-top:0.4rem;width:100%">清除背景</button>
      </div>"""

NEW_BG_PANEL = """      <div class="panel">
        <div class="block-title">背景</div>
        <label class="field">图案（墨水屏仅黑/白/红）</label>
        <div class="tool-grid" style="grid-template-columns:repeat(4,1fr)" id="bgPatSeg">
          <button type="button" data-bgpat="none" class="active">白底</button>
          <button type="button" data-bgpat="solid">纯色</button>
          <button type="button" data-bgpat="tricolor">三色</button>
          <button type="button" data-bgpat="gradient">渐变</button>
          <button type="button" data-bgpat="stripes">条纹</button>
          <button type="button" data-bgpat="grid">网格</button>
          <button type="button" data-bgpat="checker">棋盘</button>
        </div>
        <div id="bgPatOpts" style="margin-top:0.5rem">
          <div class="row" style="align-items:center;gap:0.35rem;margin-bottom:0.35rem">
            <span class="hint" style="min-width:1.6rem">主色</span>
            <div class="swatch-row" id="bgC1Seg">
              <button type="button" class="swatch black active" data-bgc="black" title="黑"></button>
              <button type="button" class="swatch white" data-bgc="white" title="白"></button>
              <button type="button" class="swatch red" data-bgc="red" title="红"></button>
            </div>
          </div>
          <div class="row" id="bgC2Row" style="align-items:center;gap:0.35rem;margin-bottom:0.35rem;display:none">
            <span class="hint" style="min-width:1.6rem">辅色</span>
            <div class="swatch-row" id="bgC2Seg">
              <button type="button" class="swatch black" data-bgc="black" title="黑"></button>
              <button type="button" class="swatch white active" data-bgc="white" title="白"></button>
              <button type="button" class="swatch red" data-bgc="red" title="红"></button>
            </div>
          </div>
          <div class="row" id="bgC3Row" style="align-items:center;gap:0.35rem;margin-bottom:0.35rem;display:none">
            <span class="hint" style="min-width:1.6rem">第三</span>
            <div class="swatch-row" id="bgC3Seg">
              <button type="button" class="swatch black" data-bgc="black" title="黑"></button>
              <button type="button" class="swatch white" data-bgc="white" title="白"></button>
              <button type="button" class="swatch red active" data-bgc="red" title="红"></button>
            </div>
          </div>
          <div id="bgDirWrap" style="display:none">
            <label class="field">方向</label>
            <div class="seg two" id="bgDirSeg">
              <button type="button" data-bgdir="v" class="active">纵向</button>
              <button type="button" data-bgdir="h">横向</button>
            </div>
          </div>
          <div id="bgPitchWrap" style="display:none;margin-top:0.4rem">
            <label class="field" for="bgPitch">间距 <span id="bgPitchVal">8</span>px</label>
            <input type="range" id="bgPitch" min="2" max="64" value="8" />
          </div>
          <label class="chk" id="bgSoftWrap" style="margin-top:0.4rem;display:none">
            <input type="checkbox" id="bgSoftGrad" checked /> 渐变抖动（更细腻）
          </label>
        </div>
        <p class="hint" style="margin-top:0.45rem">图案作底层；上传图片会覆盖图案。</p>
        <div class="drop" id="dropZone" role="button" tabindex="0">
          <span class="drop-title">点击或拖放背景图</span>
          <p class="hint" id="fileName">可空 · 图片优先于图案</p>
        </div>
        <input type="file" id="fileInput" accept="image/png,image/jpeg,image/bmp,image/gif,image/webp,.png,.jpg,.jpeg,.bmp,.gif,.webp" hidden />
        <div class="seg" id="fitSeg" style="margin-top:0.45rem">
          <button type="button" data-fit="contain" class="active">contain</button>
          <button type="button" data-fit="cover">cover</button>
          <button type="button" data-fit="stretch">stretch</button>
        </div>
        <button class="sm" type="button" id="btnClearBg" style="margin-top:0.4rem;width:100%">清除图片背景</button>
      </div>"""

JS_HELPERS = r'''
    function defaultBgPattern() {
      return { type: "none", c1: "white", c2: "black", c3: "red", dir: "v", pitch: 8, soft: true };
    }

    function inkCssRgb(color) {
      const rgb = INK[color] || INK.white;
      return rgb;
    }

    function fillPixel(img, i, color) {
      const rgb = inkCssRgb(color);
      const o = i * 4;
      img.data[o] = rgb[0];
      img.data[o + 1] = rgb[1];
      img.data[o + 2] = rgb[2];
      img.data[o + 3] = 255;
    }

    function bayerThreshold(x, y) {
      // 4x4 Bayer matrix → 0..15, map to 0..255
      const M = [
        [0, 8, 2, 10],
        [12, 4, 14, 6],
        [3, 11, 1, 9],
        [15, 7, 13, 5],
      ];
      return ((M[y & 3][x & 3] + 0.5) / 16) * 255;
    }

    function generatePatternBg(width, height, pat) {
      const img = new ImageData(width, height);
      const type = (pat && pat.type) || "none";
      const c1 = (pat && pat.c1) || "white";
      const c2 = (pat && pat.c2) || "black";
      const c3 = (pat && pat.c3) || "red";
      const dir = (pat && pat.dir) || "v";
      const pitch = Math.max(2, Number(pat && pat.pitch) || 8);
      const soft = !!(pat && pat.soft);

      if (type === "none") {
        for (let i = 0; i < width * height; i++) fillPixel(img, i, "white");
        return img;
      }
      if (type === "solid") {
        for (let i = 0; i < width * height; i++) fillPixel(img, i, c1);
        return img;
      }
      if (type === "tricolor") {
        for (let y = 0; y < height; y++) {
          for (let x = 0; x < width; x++) {
            const t = dir === "h" ? x / Math.max(1, width) : y / Math.max(1, height);
            const c = t < 1 / 3 ? c1 : (t < 2 / 3 ? c2 : c3);
            fillPixel(img, y * width + x, c);
          }
        }
        return img;
      }
      if (type === "gradient") {
        for (let y = 0; y < height; y++) {
          for (let x = 0; x < width; x++) {
            const t = dir === "h" ? x / Math.max(1, width - 1) : y / Math.max(1, height - 1);
            let c;
            if (soft) {
              const thr = bayerThreshold(x, y);
              c = (t * 255) > thr ? c2 : c1;
            } else {
              c = t < 0.5 ? c1 : c2;
            }
            fillPixel(img, y * width + x, c);
          }
        }
        return img;
      }
      if (type === "stripes") {
        for (let y = 0; y < height; y++) {
          for (let x = 0; x < width; x++) {
            const k = dir === "h" ? Math.floor(x / pitch) : Math.floor(y / pitch);
            fillPixel(img, y * width + x, (k & 1) ? c2 : c1);
          }
        }
        return img;
      }
      if (type === "grid") {
        for (let y = 0; y < height; y++) {
          for (let x = 0; x < width; x++) {
            const onLine = (x % pitch === 0) || (y % pitch === 0);
            fillPixel(img, y * width + x, onLine ? c2 : c1);
          }
        }
        return img;
      }
      if (type === "checker") {
        for (let y = 0; y < height; y++) {
          for (let x = 0; x < width; x++) {
            const cx = Math.floor(x / pitch);
            const cy = Math.floor(y / pitch);
            fillPixel(img, y * width + x, ((cx + cy) & 1) ? c2 : c1);
          }
        }
        return img;
      }
      for (let i = 0; i < width * height; i++) fillPixel(img, i, "white");
      return img;
    }

    function syncBgPatUi() {
      const pat = state.bgPattern || defaultBgPattern();
      const type = pat.type || "none";
      $("bgPatSeg").querySelectorAll("button").forEach((b) => {
        b.classList.toggle("active", b.dataset.bgpat === type);
      });
      const needC2 = ["tricolor", "gradient", "stripes", "grid", "checker"].includes(type);
      const needC3 = type === "tricolor";
      const needDir = ["tricolor", "gradient", "stripes"].includes(type);
      const needPitch = ["stripes", "grid", "checker"].includes(type);
      const needSoft = type === "gradient";
      $("bgC2Row").style.display = needC2 ? "" : "none";
      $("bgC3Row").style.display = needC3 ? "" : "none";
      $("bgDirWrap").style.display = needDir ? "" : "none";
      $("bgPitchWrap").style.display = needPitch ? "" : "none";
      $("bgSoftWrap").style.display = needSoft ? "" : "none";
      $("bgPatOpts").style.display = type === "none" ? "none" : "";

      [["bgC1Seg", pat.c1], ["bgC2Seg", pat.c2], ["bgC3Seg", pat.c3]].forEach(([id, col]) => {
        const el = $(id);
        if (!el) return;
        el.querySelectorAll(".swatch").forEach((b) => {
          b.classList.toggle("active", b.dataset.bgc === col);
        });
      });
      $("bgDirSeg").querySelectorAll("button").forEach((b) => {
        b.classList.toggle("active", b.dataset.bgdir === (pat.dir || "v"));
      });
      const pitch = Math.max(2, Number(pat.pitch) || 8);
      $("bgPitch").value = String(pitch);
      $("bgPitchVal").textContent = String(pitch);
      $("bgSoftGrad").checked = pat.soft !== false;
    }

    function setBgPattern(patch, statusMsg) {
      state.bgPattern = { ...defaultBgPattern(), ...(state.bgPattern || {}), ...patch };
      // Pattern mode: drop uploaded image so pattern is visible.
      if (patch.type && patch.type !== "none") {
        state.bgImage = null;
        state.bgDataUrl = null;
        $("fileName").textContent = `图案 · ${state.bgPattern.type}`;
      } else if (patch.type === "none" && !state.bgImage) {
        $("fileName").textContent = "可空 · 图片优先于图案";
      }
      syncBgPatUi();
      scheduleRender({ invalidateBg: true });
      if (statusMsg) setStatus(statusMsg, "ok");
    }
'''


def main() -> None:
    t = HTML.read_text(encoding="utf-8")

    if 'id="bgPatSeg"' in t:
        print("already patched")
        return

    if OLD_BG_PANEL not in t:
        raise SystemExit("background panel block not found")
    t = t.replace(OLD_BG_PANEL, NEW_BG_PANEL)

    # state field
    old_state = """      bgImage: null,
      bgDataUrl: null,
      /** @type {ImageData|null} cached dithered background */
      bgCache: null,
      bgCacheKey: "","""
    new_state = """      bgImage: null,
      bgDataUrl: null,
      bgPattern: { type: "none", c1: "white", c2: "black", c3: "red", dir: "v", pitch: 8, soft: true },
      /** @type {ImageData|null} cached dithered background */
      bgCache: null,
      bgCacheKey: "","""
    if old_state not in t:
        raise SystemExit("state bg fields not found")
    t = t.replace(old_state, new_state)

    # Insert helpers before bgOptsKey
    marker = "    function bgOptsKey() {"
    if marker not in t:
        raise SystemExit("bgOptsKey not found")
    if "function generatePatternBg" not in t:
        t = t.replace(marker, JS_HELPERS + "\n" + marker)

    # bgOptsKey include pattern
    old_key = """    function bgOptsKey() {
      const { width, height } = getSize();
      return [
        width, height, state.fit, state.colorMode,
        $("threshold").value, $("redSens").value, $("dither").checked ? 1 : 0,
        state.bgDataUrl ? state.bgDataUrl.length : 0,
        state.bgImage ? `${state.bgImage.naturalWidth}x${state.bgImage.naturalHeight}` : "none",
      ].join("|");
    }"""
    new_key = """    function bgOptsKey() {
      const { width, height } = getSize();
      const p = state.bgPattern || defaultBgPattern();
      return [
        width, height, state.fit, state.colorMode,
        $("threshold").value, $("redSens").value, $("dither").checked ? 1 : 0,
        state.bgDataUrl ? state.bgDataUrl.length : 0,
        state.bgImage ? `${state.bgImage.naturalWidth}x${state.bgImage.naturalHeight}` : "none",
        p.type, p.c1, p.c2, p.c3, p.dir, p.pitch, p.soft ? 1 : 0,
      ].join("|");
    }"""
    if old_key not in t:
        raise SystemExit("bgOptsKey body not found")
    t = t.replace(old_key, new_key)

    # ensureBgCache: pattern when no image
    old_cache = """      let img;
      if (state.bgImage) {
        const rgba = drawSourceToCanvas(state.bgImage, width, height, state.fit);
        const { blackMask, redMask } = convertBgPlanes(rgba, opts);
        img = masksToImageData(blackMask, redMask, width, height);
      } else {
        img = new ImageData(width, height);
        for (let i = 0; i < img.data.length; i += 4) {
          img.data[i] = img.data[i + 1] = img.data[i + 2] = 255;
          img.data[i + 3] = 255;
        }
      }"""
    new_cache = """      let img;
      if (state.bgImage) {
        const rgba = drawSourceToCanvas(state.bgImage, width, height, state.fit);
        const { blackMask, redMask } = convertBgPlanes(rgba, opts);
        img = masksToImageData(blackMask, redMask, width, height);
      } else {
        img = generatePatternBg(width, height, state.bgPattern || defaultBgPattern());
      }"""
    if old_cache not in t:
        raise SystemExit("ensureBgCache body not found")
    t = t.replace(old_cache, new_cache)

    # serialize / load / clear
    old_ser = """        colorMode: state.colorMode,
        fit: state.fit,
        layers: state.layers,
        bgDataUrl: state.bgDataUrl,"""
    new_ser = """        colorMode: state.colorMode,
        fit: state.fit,
        layers: state.layers,
        bgDataUrl: state.bgDataUrl,
        bgPattern: state.bgPattern || defaultBgPattern(),"""
    if old_ser not in t:
        raise SystemExit("serializeProject fields not found")
    t = t.replace(old_ser, new_ser)

    old_apply = """      state.fit = proj.fit || "contain";
      state.layers = Array.isArray(proj.layers) ? proj.layers : [];"""
    new_apply = """      state.fit = proj.fit || "contain";
      state.bgPattern = { ...defaultBgPattern(), ...(proj.bgPattern || {}) };
      state.layers = Array.isArray(proj.layers) ? proj.layers : [];"""
    if old_apply not in t:
        raise SystemExit("applyProject fit block not found")
    t = t.replace(old_apply, new_apply)

    old_apply_tail = """      await loadBgFromDataUrl(proj.bgDataUrl || null);
      if (!proj.bgDataUrl) $("fileName").textContent = "可空 · 仅背景走抖动";
      syncPresetFromSize();
      renderLayerList();
      syncPropPanel();
      scheduleRender();"""
    new_apply_tail = """      await loadBgFromDataUrl(proj.bgDataUrl || null);
      if (!proj.bgDataUrl) {
        $("fileName").textContent = state.bgPattern && state.bgPattern.type !== "none"
          ? `图案 · ${state.bgPattern.type}`
          : "可空 · 图片优先于图案";
      }
      syncBgPatUi();
      syncPresetFromSize();
      renderLayerList();
      syncPropPanel();
      scheduleRender({ invalidateBg: true });"""
    if old_apply_tail not in t:
        raise SystemExit("applyProject tail not found")
    t = t.replace(old_apply_tail, new_apply_tail)

    old_clear = """      state.bgImage = null;
      state.bgDataUrl = null;
      state.packed = null;
      $("fileInput").value = "";
      $("fileName").textContent = "可空 · 仅背景走抖动";"""
    new_clear = """      state.bgImage = null;
      state.bgDataUrl = null;
      state.bgPattern = defaultBgPattern();
      state.packed = null;
      $("fileInput").value = "";
      $("fileName").textContent = "可空 · 图片优先于图案";
      syncBgPatUi();"""
    if old_clear not in t:
        raise SystemExit("clearAll bg reset not found")
    t = t.replace(old_clear, new_clear)

    # Event listeners near btnClearBg
    old_clear_btn = """    $("btnClearBg").addEventListener("click", () => {
      state.bgImage = null;
      state.bgDataUrl = null;
      $("fileName").textContent = "可空 · 仅背景走抖动";
      scheduleRender({ invalidateBg: true });
    });"""
    new_clear_btn = """    $("btnClearBg").addEventListener("click", () => {
      state.bgImage = null;
      state.bgDataUrl = null;
      $("fileName").textContent = state.bgPattern && state.bgPattern.type !== "none"
        ? `图案 · ${state.bgPattern.type}`
        : "可空 · 图片优先于图案";
      scheduleRender({ invalidateBg: true });
    });

    $("bgPatSeg").addEventListener("click", (e) => {
      const btn = e.target.closest("button[data-bgpat]");
      if (!btn) return;
      setBgPattern({ type: btn.dataset.bgpat }, `背景 · ${btn.dataset.bgpat}`);
    });
    function wireBgSwatch(segId, key) {
      $(segId).addEventListener("click", (e) => {
        const btn = e.target.closest(".swatch");
        if (!btn) return;
        setBgPattern({ [key]: btn.dataset.bgc });
      });
    }
    wireBgSwatch("bgC1Seg", "c1");
    wireBgSwatch("bgC2Seg", "c2");
    wireBgSwatch("bgC3Seg", "c3");
    $("bgDirSeg").addEventListener("click", (e) => {
      const btn = e.target.closest("button[data-bgdir]");
      if (!btn) return;
      setBgPattern({ dir: btn.dataset.bgdir });
    });
    $("bgPitch").addEventListener("input", () => {
      const pitch = Number($("bgPitch").value) || 8;
      $("bgPitchVal").textContent = String(pitch);
      setBgPattern({ pitch });
    });
    $("bgSoftGrad").addEventListener("change", () => {
      setBgPattern({ soft: $("bgSoftGrad").checked });
    });
    syncBgPatUi();"""
    if old_clear_btn not in t:
        raise SystemExit("btnClearBg handler not found")
    t = t.replace(old_clear_btn, new_clear_btn)

    # Status strings that mention 有背景
    t = t.replace(
        '(state.bgImage ? " · 有背景" : "")',
        '(state.bgImage ? " · 有背景图" : (state.bgPattern && state.bgPattern.type !== "none" ? " · 图案背景" : ""))',
    )

    # When loading a file image, note it overrides pattern
    old_load_name = '$("fileName").textContent = `背景 · ${img.naturalWidth}×${img.naturalHeight}`;'
    if old_load_name in t:
        t = t.replace(
            old_load_name,
            '$("fileName").textContent = `图片 · ${img.naturalWidth}×${img.naturalHeight}（覆盖图案）`;',
        )

    HTML.write_text(t, encoding="utf-8")
    print("patched", HTML.stat().st_size)


if __name__ == "__main__":
    main()
