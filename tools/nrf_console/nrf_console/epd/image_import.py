"""Import raster images into GDEY029Z95 BWR planes."""

from __future__ import annotations

from pathlib import Path

from nrf_console.epd.display import Canvas
from nrf_console.epd.plane import HEIGHT, WIDTH, Color


def image_to_planes(
    path: str | Path,
    *,
    luminance_threshold: int = 140,
    red_sat_min: int = 40,
    red_dom_min: int = 30,
) -> tuple[bytes, bytes]:
    """
    Load PNG/BMP/JPEG, fit into 128×296, quantize to BWR.

    - Red if R dominates G/B and saturation-ish delta is high enough
    - Else black if luminance < threshold, else white
    """
    try:
        from PIL import Image
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("Pillow required for image import — pip install Pillow") from exc

    src = Image.open(path).convert("RGB")
    fitted = _fit_contain(src, WIDTH, HEIGHT)
    canvas = Canvas()
    canvas.clear(Color.WHITE)

    # Center fitted image on white canvas
    ox = (WIDTH - fitted.width) // 2
    oy = (HEIGHT - fitted.height) // 2
    pixels = fitted.load()
    assert pixels is not None

    for y in range(fitted.height):
        for x in range(fitted.width):
            r, g, b = pixels[x, y]
            color = _quantize_rgb(
                r,
                g,
                b,
                luminance_threshold=luminance_threshold,
                red_sat_min=red_sat_min,
                red_dom_min=red_dom_min,
            )
            canvas.set_pixel(ox + x, oy + y, color)

    return canvas.planes()


def _fit_contain(img: object, tw: int, th: int) -> object:
    from PIL import Image

    assert isinstance(img, Image.Image)
    iw, ih = img.size
    if iw <= 0 or ih <= 0:
        return Image.new("RGB", (tw, th), (255, 255, 255))
    scale = min(tw / iw, th / ih)
    nw = max(1, int(round(iw * scale)))
    nh = max(1, int(round(ih * scale)))
    return img.resize((nw, nh), Image.Resampling.LANCZOS)


def _quantize_rgb(
    r: int,
    g: int,
    b: int,
    *,
    luminance_threshold: int,
    red_sat_min: int,
    red_dom_min: int,
) -> Color:
    # Simple red detector: R ahead of G/B and not too gray.
    red_dom = r - max(g, b)
    sat = max(r, g, b) - min(r, g, b)
    if red_dom >= red_dom_min and sat >= red_sat_min and r >= 80:
        return Color.RED
    # Rec.601-ish luminance
    y = (299 * r + 587 * g + 114 * b) // 1000
    if y < luminance_threshold:
        return Color.BLACK
    return Color.WHITE
