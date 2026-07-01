#!/usr/bin/env python3
"""Run benchmark scenes and emit CSV + JSON + markdown summary.

Usage:
    python3 scripts/benchmark_report.py [--samples N] [--threads N]
                                        [--out reports/benchmark_<ts>]
                                        [--scenes scene1.json scene2.json ...]

Renders each scene with `--stats-format json`, parses the one-line JSON
output, and writes:
  <out>.csv   — one row per (scene, thread_config) run
  <out>.json  — structured report with all runs
  <out>.md    — markdown summary table sorted by render_ms

Focus: identify slow transparent and mesh-heavy scenes.
"""

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path


DEFAULT_SCENES = [
    "scenes/default.json",
    "scenes/three_balls.json",
    "scenes/pbr_test.json",
    "scenes/mirror_glass_water.json",
    "scenes/glass.json",
    "scenes/glass_bottle.json",
    "scenes/glass_emissive.json",
    "scenes/cornell_box.json",
    "scenes/mark.json",
    "scenes/obj_mtl_test.json",
    "scenes/khronos_water_bottle.json",
    "scenes/khronos_transmission_test.json",
    "scenes/studio_materials.json",
]


def find_raytracer(root: Path) -> str:
    if (root / "raytracer").exists() and os.access(root / "raytracer", os.X_OK):
        return str(root / "raytracer")
    build_bin = root / "build" / "raytracer"
    if build_bin.exists() and os.access(build_bin, os.X_OK):
        return str(build_bin)
    sys.exit("raytracer binary not found; run ./build.sh first")


def run_scene(binary: str, scene: str, samples: int, threads: int) -> dict:
    out_path = f"/tmp/bench_{abs(hash((scene, threads, samples))) & 0xFFFFFF}.png"
    cmd = [
        binary,
        "--scene", scene,
        "--samples", str(samples),
        "--threads", str(threads),
        "--stats-format", "json",
        "--out", out_path,
    ]
    t0 = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    wall = time.time() - t0
    if proc.returncode != 0:
        sys.exit(f"raytracer failed for {scene} (threads={threads}):\n{proc.stderr}")

    line = (proc.stdout or "").strip().splitlines()[-1] if proc.stdout.strip() else ""
    if not line.startswith("{"):
        sys.exit(f"no JSON stats line from {scene}: {proc.stdout!r}")

    stats = json.loads(line)
    stats["wall_s"] = round(wall, 3)
    stats["threads"] = threads
    stats["scene"] = scene
    # categorize for profiling focus
    name = Path(scene).stem
    if "glass" in name or "transmission" in name or "attenuation" in name:
        stats["category"] = "transparent"
    elif name in ("mark", "obj_mtl_test", "khronos_water_bottle") or "bunny" in name:
        stats["category"] = "mesh-heavy"
    elif "cornell" in name or "emissive" in name:
        stats["category"] = "area-lit"
    else:
        stats["category"] = "standard"
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", type=int, default=16)
    ap.add_argument("--threads", type=int, default=0,
                    help="0 = run both 1 and hardware threads")
    ap.add_argument("--out", default=None,
                    help="output prefix, e.g. reports/benchmark_<ts>")
    ap.add_argument("--scenes", nargs="*", default=None)
    args = ap.parse_args()

    root = Path(__file__).resolve().parent.parent
    binary = find_raytracer(root)
    scenes = args.scenes or DEFAULT_SCENES
    threads_list = [args.threads] if args.threads > 0 else [1, os.cpu_count() or 4]

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_prefix = args.out or str(root / "reports" / f"benchmark_{ts}")
    Path(out_prefix).parent.mkdir(parents=True, exist_ok=True)

    runs = []
    for scene in scenes:
        scene_path = root / scene
        if not scene_path.exists():
            print(f"skip missing scene: {scene}", file=sys.stderr)
            continue
        for threads in threads_list:
            print(f"== {scene} / {threads} threads ==", file=sys.stderr)
            stats = run_scene(binary, str(scene_path), args.samples, threads)
            runs.append(stats)

    # CSV
    csv_path = f"{out_prefix}.csv"
    fields = ["scene", "category", "threads", "samples", "width", "height",
              "max_depth", "primitives", "load_ms", "render_ms", "total_ms",
              "wall_s", "emissive_area"]
    with open(csv_path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        for r in runs:
            w.writerow(r)

    # JSON
    json_path = f"{out_prefix}.json"
    with open(json_path, "w") as f:
        json.dump({"timestamp": ts, "binary": binary, "runs": runs}, f, indent=2)

    # Markdown summary
    md_path = f"{out_prefix}.md"
    sorted_runs = sorted(runs, key=lambda r: r.get("render_ms", 0), reverse=True)
    with open(md_path, "w") as f:
        f.write(f"# Benchmark Report {ts}\n\n")
        f.write(f"samples={args.samples} threads={threads_list}\n\n")
        f.write("## Slowest scenes by render_ms\n\n")
        f.write("| scene | category | threads | render_ms | load_ms | primitives |\n")
        f.write("|-------|----------|---------|-----------|---------|------------|\n")
        for r in sorted_runs[:15]:
            f.write(f"| {Path(r['scene']).stem} | {r.get('category','')} | "
                    f"{r['threads']} | {r.get('render_ms',0)} | "
                    f"{r.get('load_ms',0)} | {r.get('primitives',0)} |\n")
        f.write("\n## By category (mean render_ms)\n\n")
        cat_map = {}
        for r in runs:
            cat_map.setdefault(r.get("category", "?"), []).append(r.get("render_ms", 0))
        f.write("| category | count | mean_render_ms | max_render_ms |\n")
        f.write("|----------|-------|----------------|---------------|\n")
        for cat, vals in sorted(cat_map.items(), key=lambda kv: -sum(kv[1]) / len(kv[1])):
            f.write(f"| {cat} | {len(vals)} | {sum(vals)/len(vals):.1f} | {max(vals)} |\n")

    print(f"\nWrote {csv_path}\nWrote {json_path}\nWrote {md_path}")


if __name__ == "__main__":
    main()
