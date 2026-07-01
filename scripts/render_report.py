#!/usr/bin/env python3
"""Batch render -> PNG validate -> contact sheet -> markdown report.

Usage:
    python3 scripts/render_report.py [--samples N] [--threads N]
                                     [--out reports/render_<ts>]
                                     [--scenes scene1.json ...]

For each scene:
  1. Render with the raytracer binary to a PNG.
  2. Validate the PNG (size, dimensions, non-empty).
  3. Collect mean luma / saturation stats.
Then:
  4. Build a contact sheet PNG (grid of thumbnails).
  5. Emit a markdown report with per-scene results and the contact sheet.

Produces:
  <out>/<scene>_<samples>s.png   — individual renders
  <out>/contact_sheet.png        — grid contact sheet
  <out>/report.md                — markdown acceptance report
  <out>/metrics.json             — per-image metrics
"""

import argparse
import json
import os
import struct
import subprocess
import sys
import zlib
from datetime import datetime
from pathlib import Path

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False


DEFAULT_SCENES = [
    "scenes/three_balls.json",
    "scenes/glass.json",
    "scenes/glass_bottle.json",
    "scenes/glass_emissive.json",
    "scenes/pbr_test.json",
    "scenes/mirror_glass_water.json",
    "scenes/cornell_box.json",
    "scenes/khronos_water_bottle.json",
    "scenes/khronos_transmission_test.json",
    "scenes/khronos_glass_vase_flowers.json",
    "scenes/khronos_glass_broken_window.json",
    "scenes/khronos_glass_hurricane_candle_holder.json",
    "scenes/mark.json",
    "scenes/obj_mtl_test.json",
    "scenes/bunny_test.json",
    "scenes/no_normal_obj.json",
    "scenes/studio_materials.json",
]


def find_raytracer(root: Path) -> str:
    for p in (root / "raytracer", root / "build" / "raytracer"):
        if p.exists() and os.access(p, os.X_OK):
            return str(p)
    sys.exit("raytracer binary not found; run ./build.sh first")


def validate_png(path: Path) -> dict:
    """Read PNG header + IHDR to confirm valid 8-bit RGB."""
    data = path.read_bytes()
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return {"valid": False, "error": "bad PNG signature"}
    # IHDR is the first chunk; bytes 8..16 = length+type, 16..24 = width/height
    width = struct.unpack(">I", data[16:20])[0]
    height = struct.unpack(">I", data[20:24])[0]
    bit_depth = data[24]
    color_type = data[25]
    if bit_depth != 8:
        return {"valid": False, "error": f"bit_depth={bit_depth}", "width": width, "height": height}
    return {
        "valid": True,
        "width": width,
        "height": height,
        "size_kb": round(len(data) / 1024.0, 1),
        "bit_depth": bit_depth,
        "color_type": color_type,
    }


def image_metrics(path: Path) -> dict:
    """Compute mean luma and saturated pixel % using PIL if available."""
    if not HAS_PIL:
        return {}
    img = Image.open(path).convert("RGB")
    pixels = list(img.getdata())
    n = len(pixels)
    if n == 0:
        return {"mean_luma": 0, "sat_pct": 0}
    total_luma = 0
    sat_count = 0
    for r, g, b in pixels:
        luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
        total_luma += luma
        if r == 255 or g == 255 or b == 255:
            sat_count += 1
    return {
        "mean_luma": round(total_luma / n, 2),
        "sat_pct": round(100.0 * sat_count / n, 4),
        "size_kb": round(path.stat().st_size / 1024.0, 1),
        "width": img.width,
        "height": img.height,
    }


def build_contact_sheet(image_paths, out_path: Path, cols=4, thumb_w=240):
    if not HAS_PIL:
        return False
    rows = (len(image_paths) + cols - 1) // cols
    pad = 8
    label_h = 18
    # Compute thumb height preserving aspect
    thumbs = []
    for ip, _ in image_paths:
        img = Image.open(ip).convert("RGB")
        ratio = thumb_w / img.width
        thumb_h = max(1, int(img.height * ratio))
        thumbs.append((img.resize((thumb_w, thumb_h)), Path(ip).stem))

    cell_h = max(t.height for t, _ in thumbs) + label_h + pad
    sheet_w = cols * (thumb_w + pad) + pad
    sheet_h = rows * cell_h + pad
    sheet = Image.new("RGB", (sheet_w, sheet_h), (32, 32, 32))
    from PIL import ImageDraw
    draw = ImageDraw.Draw(sheet)
    for idx, (thumb, name) in enumerate(thumbs):
        col = idx % cols
        row = idx // cols
        x = pad + col * (thumb_w + pad)
        y = pad + row * cell_h
        sheet.paste(thumb, (x, y))
        draw.text((x, y + thumb.height + 2), name, fill=(220, 220, 220))
    sheet.save(out_path)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=64,
                    help="thumbnail samples for the contact sheet / card images")
    ap.add_argument("--hq-samples", type=int, default=512,
                    help="high-quality samples for the linked full-resolution images (0 = skip HQ)")
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--out", default=None)
    ap.add_argument("--scenes", nargs="*", default=None)
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent
    binary = find_raytracer(root)
    scenes = args.scenes or DEFAULT_SCENES
    threads = args.threads or (os.cpu_count() or 4)

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = Path(args.out or (root / "reports" / f"render_{ts}"))
    out_dir.mkdir(parents=True, exist_ok=True)

    results = []
    for scene in scenes:
        scene_path = root / scene
        if not scene_path.exists():
            print(f"skip missing scene: {scene}", file=sys.stderr)
            continue
        name = Path(scene).stem
        out_png = out_dir / f"{name}_{args.samples}s.png"
        hq_png = out_dir / f"{name}_hq.png"
        stats = {}
        # Thumbnail render (lower samples, fast)
        cmd = [binary, "--scene", str(scene_path),
               "--samples", str(args.samples), "--threads", str(threads),
               "--out", str(out_png), "--stats-format", "json"]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            results.append({"scene": name, "rendered": False,
                            "error": proc.stderr.strip().splitlines()[-1:][:200]})
            continue
        for line in proc.stdout.splitlines():
            if line.startswith("{"):
                try:
                    stats = json.loads(line)
                except json.JSONDecodeError:
                    pass
        # High-quality render (large samples, for the linked full-res image)
        hq_ok = False
        hq_info = {}
        if args.hq_samples > 0:
            hq_cmd = [binary, "--scene", str(scene_path),
                      "--samples", str(args.hq_samples), "--threads", str(threads),
                      "--out", str(hq_png), "--stats-format", "json"]
            hq_proc = subprocess.run(hq_cmd, capture_output=True, text=True)
            hq_ok = hq_proc.returncode == 0 and hq_png.exists()
            if hq_ok:
                hq_info = validate_png(hq_png)
        info = validate_png(out_png)
        metrics = image_metrics(out_png)
        results.append({
            "scene": name,
            "rendered": info.get("valid", False),
            "render_ms": stats.get("render_ms", 0),
            "load_ms": stats.get("load_ms", 0),
            "primitives": stats.get("primitives", 0),
            "has_hq": hq_ok,
            "hq_samples": args.hq_samples if hq_ok else 0,
            "hq_width": hq_info.get("width", 0),
            "hq_height": hq_info.get("height", 0),
            "hq_size_kb": hq_info.get("size_kb", 0),
            **info,
            **metrics,
        })

    # Contact sheet
    rendered = [(out_dir / f"{Path(r['scene']).stem}_{args.samples}s.png", r)
                for r in results if r.get("rendered")]
    sheet_path = out_dir / "contact_sheet.png"
    sheet_ok = build_contact_sheet([(p, r) for p, r in rendered], sheet_path)

    # Metrics JSON
    (out_dir / "metrics.json").write_text(
        json.dumps({"timestamp": ts, "samples": args.samples,
                    "hq_samples": args.hq_samples, "threads": threads,
                    "has_pil": HAS_PIL, "results": results}, indent=2))

    # Markdown report
    md = out_dir / "report.md"
    with open(md, "w") as f:
        f.write(f"# Render Quality Report {ts}\n\n")
        f.write(f"samples={args.samples} threads={threads} scenes={len(results)} "
                f"rendered={sum(1 for r in results if r.get('rendered'))}\n\n")
        if sheet_ok:
            f.write("![Contact sheet](contact_sheet.png)\n\n")
        f.write("## Per-scene results\n\n")
        f.write("| scene | rendered | w x h | mean_luma | sat_pct | render_ms | size_kb | hq |\n")
        f.write("|-------|----------|-------|-----------|---------|-----------|---------|----|\n")
        for r in results:
            if r.get("rendered"):
                hq = f"[{r.get('hq_samples','?')}s {r.get('hq_width','?')}x{r.get('hq_height','?')}]({r['scene']}_hq.png)" if r.get("has_hq") else "no"
                f.write(f"| {r['scene']} | yes | {r.get('width','?')}x{r.get('height','?')} | "
                        f"{r.get('mean_luma','-')} | {r.get('sat_pct','-')} | "
                        f"{r.get('render_ms','-')} | {r.get('size_kb','-')} | {hq} |\n")
            else:
                f.write(f"| {r['scene']} | no | - | - | - | - | - | - |\n")
        f.write("\n## Acceptance flags\n\n")
        for r in results:
            flag = "Pass"
            notes = []
            if not r.get("rendered"):
                flag = "Fail"; notes.append("render failed")
            else:
                if r.get("sat_pct", 0) > 1.0:
                    flag = "Needs work"; notes.append(f"high saturation {r['sat_pct']}%")
                if r.get("mean_luma", 128) > 220:
                    flag = "Needs work"; notes.append("over-bright")
                if r.get("mean_luma", 128) < 10:
                    flag = "Needs work"; notes.append("too dark")
            f.write(f"- **{r['scene']}**: {flag}"
                    + (f" — {', '.join(notes)}" if notes else "") + "\n")

    hq_count = sum(1 for r in results if r.get("has_hq"))
    print(f"\nWrote {sheet_path}" if sheet_ok else "\nContact sheet skipped (no PIL)")
    print(f"Wrote {out_dir / 'metrics.json'}")
    print(f"Wrote {md}")
    if args.hq_samples > 0:
        print(f"High-quality renders ({args.hq_samples} samples): {hq_count}/{len(results)} -> {out_dir}/*_hq.png")


if __name__ == "__main__":
    main()
