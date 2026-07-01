#!/usr/bin/env python3
"""Generate a beautiful self-contained HTML render quality report.

Reads metrics.json + benchmark JSON produced by render_report.py /
benchmark_report.py, embeds the rendered PNGs as base64 data URIs, and
emits a single portable .html file with a responsive card layout,
acceptance badges, per-scene metrics, and performance comparison.

Usage:
    python3 scripts/html_report.py \
        --render-dir reports/render_20260701 \
        --benchmark reports/benchmark_20260701.json \
        --out reports/render_20260701/report.html
"""

import argparse
import base64
import json
import os
from datetime import datetime
from pathlib import Path


SCENE_TITLES = {
    "three_balls": "Three Balls — Baseline Glass/Metal/Diffuse",
    "glass": "Glass Sphere",
    "glass_bottle": "GLB Glass Bottle",
    "glass_emissive": "Glass + Emissive Area Light",
    "pbr_test": "PBR Rough/Metal Test",
    "mirror_glass_water": "Mirror + Glass + Water + PBR + Emissive",
    "cornell_box": "Cornell Box",
    "khronos_water_bottle": "Khronos WaterBottle (GLB PBR)",
    "khronos_transmission_test": "Khronos TransmissionTest (KHR transmission)",
    "mark": "OBJ Mark (Mesh + Transform)",
    "obj_mtl_test": "OBJ + MTL (Kd / map_Kd)",
    "bunny_test": "OBJ Bunny (mark.obj + transform)",
    "no_normal_obj": "OBJ without Normals (smooth normals)",
    "studio_materials": "Studio Materials Preset (metal/glass/PBR)",
}

SCENE_CATEGORIES = {
    "three_balls": "baseline",
    "glass": "transparent",
    "glass_bottle": "transparent",
    "glass_emissive": "transparent",
    "khronos_transmission_test": "transparent",
    "mirror_glass_water": "transparent",
    "pbr_test": "pbr",
    "cornell_box": "area-lit",
    "khronos_water_bottle": "mesh-heavy",
    "mark": "mesh-heavy",
    "obj_mtl_test": "mesh-heavy",
    "bunny_test": "mesh-heavy",
    "no_normal_obj": "mesh-heavy",
    "studio_materials": "studio",
}

CATEGORY_COLORS = {
    "baseline": "#6b7280",
    "transparent": "#06b6d4",
    "pbr": "#8b5cf6",
    "area-lit": "#f59e0b",
    "mesh-heavy": "#10b981",
    "studio": "#ec4899",
}


def b64_image(path: Path) -> str:
    data = path.read_bytes()
    return "data:image/png;base64," + base64.b64encode(data).decode("ascii")


def acceptance_badge(result: dict) -> str:
    if not result.get("rendered"):
        return '<span class="badge badge-fail">Fail</span>'
    notes = []
    flag = "pass"
    if result.get("sat_pct", 0) > 1.0:
        flag = "needs"; notes.append(f"high saturation {result['sat_pct']}%")
    if result.get("mean_luma", 128) > 220:
        flag = "needs"; notes.append("over-bright")
    if result.get("mean_luma", 128) < 10:
        flag = "needs"; notes.append("too dark")
    cls = "badge-pass" if flag == "pass" else "badge-needs"
    label = "Pass" if flag == "pass" else "Needs work"
    note_str = f" &middot; {', '.join(notes)}" if notes else ""
    return f'<span class="badge {cls}">{label}</span><span class="notes">{note_str}</span>'


def scene_card(scene_name: str, result: dict, render_dir: Path, samples: int) -> str:
    img_path = render_dir / f"{scene_name}_{samples}s.png"
    img_uri = b64_image(img_path) if img_path.exists() else ""
    title = SCENE_TITLES.get(scene_name, scene_name)
    cat = SCENE_CATEGORIES.get(scene_name, "standard")
    cat_color = CATEGORY_COLORS.get(cat, "#6b7280")
    w = result.get("width", "?")
    h = result.get("height", "?")
    luma = result.get("mean_luma", "-")
    sat = result.get("sat_pct", "-")
    render_ms = result.get("render_ms", "-")
    size_kb = result.get("size_kb", "-")
    badge = acceptance_badge(result)
    return f"""
    <article class="card" data-category="{cat}">
      <div class="card-img-wrap">
        <img src="{img_uri}" alt="{scene_name}" loading="lazy" />
        <span class="cat-chip" style="background:{cat_color}">{cat}</span>
      </div>
      <div class="card-body">
        <h3>{title}</h3>
        <div class="card-meta">
          <span><strong>{w}&times;{h}</strong></span>
          <span>render: <strong>{render_ms} ms</strong></span>
        </div>
        <div class="metrics-row">
          <div class="metric"><label>mean luma</label><span>{luma}</span></div>
          <div class="metric"><label>sat %</label><span>{sat}</span></div>
          <div class="metric"><label>size</label><span>{size_kb} KB</span></div>
        </div>
        <div class="accept-row">{badge}</div>
      </div>
    </article>"""


def perf_table(bench_json: dict) -> str:
    runs = bench_json.get("runs", [])
    # Build per-scene 1-thread vs max-thread comparison
    by_scene = {}
    for r in runs:
        name = Path(r["scene"]).stem
        by_scene.setdefault(name, {})[r["threads"]] = r
    max_threads = max((r["threads"] for r in runs), default=1)

    rows = []
    for name, thread_map in sorted(by_scene.items(), key=lambda kv: -kv[1].get(1, {}).get("render_ms", 0)):
        r1 = thread_map.get(1, {})
        rn = thread_map.get(max_threads, {})
        t1 = r1.get("render_ms", 0)
        tn = rn.get("render_ms", 0)
        speedup = (t1 / tn) if tn > 0 else 0
        cat = SCENE_CATEGORIES.get(name, "standard")
        cat_color = CATEGORY_COLORS.get(cat, "#6b7280")
        rows.append(f"""
        <tr>
          <td><span class="cat-dot" style="background:{cat_color}"></span>{name}</td>
          <td class="num">{t1}</td>
          <td class="num">{tn}</td>
          <td class="num"><strong>{speedup:.1f}x</strong></td>
        </tr>""")
    return f"""
    <table class="perf-table">
      <thead><tr><th>Scene</th><th>1 thread (ms)</th><th>{max_threads} threads (ms)</th><th>Speedup</th></tr></thead>
      <tbody>{''.join(rows)}
      </tbody>
    </table>"""


def category_summary(bench_json: dict) -> str:
    runs = bench_json.get("runs", [])
    cat_map = {}
    for r in runs:
        cat = r.get("category", "standard")
        cat_map.setdefault(cat, []).append(r.get("render_ms", 0))
    cards = []
    for cat, vals in sorted(cat_map.items(), key=lambda kv: -sum(kv[1]) / len(kv[1])):
        mean = sum(vals) / len(vals)
        mx = max(vals)
        color = CATEGORY_COLORS.get(cat, "#6b7280")
        cards.append(f"""
        <div class="cat-card" style="border-top:4px solid {color}">
          <div class="cat-name" style="color:{color}">{cat}</div>
          <div class="cat-stat"><span>mean</span><strong>{mean:.0f} ms</strong></div>
          <div class="cat-stat"><span>max</span><strong>{mx:.0f} ms</strong></div>
          <div class="cat-stat"><span>runs</span><strong>{len(vals)}</strong></div>
        </div>""")
    return "".join(cards)


def build_html(render_dir: Path, bench_path: Path, out_path: Path) -> None:
    metrics = json.loads((render_dir / "metrics.json").read_text())
    bench = json.loads(bench_path.read_text()) if bench_path and bench_path.exists() else {}
    samples = metrics.get("samples", 0)
    threads = metrics.get("threads", 0)
    results = metrics.get("results", [])
    rendered = sum(1 for r in results if r.get("rendered"))
    pass_count = 0
    needs_count = 0
    fail_count = 0
    for r in results:
        if not r.get("rendered"):
            fail_count += 1
        elif r.get("mean_luma", 128) > 220 or r.get("sat_pct", 0) > 1.0 or r.get("mean_luma", 128) < 10:
            needs_count += 1
        else:
            pass_count += 1
    ts = datetime.now().strftime("%Y-%m-%d %H:%M")

    cards = "\n".join(scene_card(r["scene"], r, render_dir, samples) for r in results)
    perf = perf_table(bench) if bench else "<p class='muted'>No benchmark data provided.</p>"
    cats = category_summary(bench) if bench else ""

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Raytracer Render Report &mdash; {ts}</title>
<style>
  :root {{
    --bg: #0f1115;
    --panel: #171a21;
    --panel-2: #1f232c;
    --text: #e6e7eb;
    --muted: #8a91a0;
    --accent: #60a5fa;
    --border: #2a2f3a;
    --pass: #22c55e;
    --needs: #f59e0b;
    --fail: #ef4444;
  }}
  * {{ box-sizing: border-box; }}
  body {{
    margin: 0;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
    background: radial-gradient(1200px 800px at 10% -10%, #1a2236 0%, var(--bg) 60%) fixed;
    color: var(--text);
    line-height: 1.55;
  }}
  header {{
    padding: 56px 24px 32px;
    text-align: center;
    border-bottom: 1px solid var(--border);
    background: linear-gradient(180deg, rgba(96,165,250,0.08), transparent);
  }}
  header h1 {{
    font-size: 34px;
    font-weight: 700;
    margin: 0 0 8px;
    letter-spacing: -0.5px;
    background: linear-gradient(90deg, #fff, #9ec5ff);
    -webkit-background-clip: text;
    -webkit-text-fill-color: transparent;
    background-clip: text;
  }}
  header .sub {{ color: var(--muted); font-size: 14px; }}
  header .meta {{ margin-top: 14px; display: inline-flex; gap: 18px; flex-wrap: wrap; justify-content: center; }}
  header .meta span {{ background: var(--panel); padding: 6px 12px; border-radius: 999px; border: 1px solid var(--border); font-size: 13px; }}
  .container {{ max-width: 1280px; margin: 0 auto; padding: 32px 24px 64px; }}

  .summary-grid {{
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 14px;
    margin-bottom: 28px;
  }}
  .summary-card {{
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 18px 20px;
  }}
  .summary-card .label {{ color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: 0.06em; }}
  .summary-card .value {{ font-size: 28px; font-weight: 700; margin-top: 6px; }}
  .summary-card.pass .value {{ color: var(--pass); }}
  .summary-card.needs .value {{ color: var(--needs); }}
  .summary-card.fail .value {{ color: var(--fail); }}

  section h2 {{
    font-size: 22px;
    margin: 40px 0 16px;
    padding-bottom: 10px;
    border-bottom: 1px solid var(--border);
    display: flex;
    align-items: center;
    gap: 10px;
  }}
  section h2::before {{
    content: "";
    width: 4px;
    height: 22px;
    background: var(--accent);
    border-radius: 2px;
  }}

  .filter-bar {{
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
    margin-bottom: 18px;
  }}
  .filter-bar button {{
    background: var(--panel);
    color: var(--text);
    border: 1px solid var(--border);
    padding: 6px 14px;
    border-radius: 999px;
    cursor: pointer;
    font-size: 13px;
    transition: all 0.15s;
  }}
  .filter-bar button:hover {{ border-color: var(--accent); }}
  .filter-bar button.active {{ background: var(--accent); color: #0b1220; border-color: var(--accent); }}

  .grid {{
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
    gap: 20px;
  }}
  .card {{
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 14px;
    overflow: hidden;
    transition: transform 0.2s, border-color 0.2s, box-shadow 0.2s;
    display: flex;
    flex-direction: column;
  }}
  .card:hover {{
    transform: translateY(-3px);
    border-color: var(--accent);
    box-shadow: 0 12px 30px rgba(0,0,0,0.4);
  }}
  .card-img-wrap {{
    position: relative;
    background: #000;
    aspect-ratio: 3 / 2;
    overflow: hidden;
  }}
  .card-img-wrap img {{ width: 100%; height: 100%; object-fit: contain; display: block; }}
  .cat-chip {{
    position: absolute;
    top: 10px;
    left: 10px;
    color: #fff;
    font-size: 11px;
    padding: 4px 10px;
    border-radius: 999px;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    font-weight: 600;
    box-shadow: 0 2px 6px rgba(0,0,0,0.5);
  }}
  .card-body {{ padding: 14px 16px 18px; flex: 1; display: flex; flex-direction: column; gap: 10px; }}
  .card-body h3 {{ margin: 0; font-size: 15px; font-weight: 600; color: #fff; }}
  .card-meta {{ display: flex; justify-content: space-between; font-size: 12px; color: var(--muted); }}
  .metrics-row {{ display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; }}
  .metric {{ background: var(--panel-2); border-radius: 8px; padding: 8px 10px; }}
  .metric label {{ display: block; font-size: 10px; color: var(--muted); text-transform: uppercase; letter-spacing: 0.05em; }}
  .metric span {{ font-size: 14px; font-weight: 600; }}
  .accept-row {{ display: flex; align-items: center; gap: 10px; flex-wrap: wrap; font-size: 12px; }}
  .badge {{ padding: 4px 10px; border-radius: 999px; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.05em; }}
  .badge-pass {{ background: rgba(34,197,94,0.15); color: var(--pass); border: 1px solid rgba(34,197,94,0.4); }}
  .badge-needs {{ background: rgba(245,158,11,0.15); color: var(--needs); border: 1px solid rgba(245,158,11,0.4); }}
  .badge-fail {{ background: rgba(239,68,68,0.15); color: var(--fail); border: 1px solid rgba(239,68,68,0.4); }}
  .notes {{ color: var(--muted); }}

  .cat-grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 12px; margin-bottom: 24px; }}
  .cat-card {{ background: var(--panel); border: 1px solid var(--border); border-radius: 10px; padding: 14px 16px; }}
  .cat-name {{ font-size: 14px; font-weight: 700; margin-bottom: 8px; text-transform: uppercase; letter-spacing: 0.05em; }}
  .cat-stat {{ display: flex; justify-content: space-between; font-size: 13px; padding: 2px 0; }}
  .cat-stat span {{ color: var(--muted); }}

  .perf-table {{ width: 100%; border-collapse: collapse; font-size: 14px; }}
  .perf-table th, .perf-table td {{ padding: 10px 12px; text-align: left; border-bottom: 1px solid var(--border); }}
  .perf-table th {{ color: var(--muted); font-weight: 600; font-size: 12px; text-transform: uppercase; letter-spacing: 0.05em; }}
  .perf-table td.num {{ text-align: right; font-variant-numeric: tabular-nums; }}
  .perf-table tr:hover td {{ background: var(--panel-2); }}
  .cat-dot {{ display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 8px; vertical-align: middle; }}

  .principles {{ background: var(--panel); border: 1px solid var(--border); border-radius: 12px; padding: 20px 24px; margin-bottom: 28px; }}
  .principles ol {{ margin: 8px 0 0; padding-left: 22px; }}
  .principles li {{ margin: 6px 0; color: var(--text); }}
  .principles li strong {{ color: var(--accent); }}

  footer {{ text-align: center; color: var(--muted); padding: 28px 0 8px; font-size: 13px; border-top: 1px solid var(--border); margin-top: 40px; }}
  footer a {{ color: var(--accent); text-decoration: none; }}
  .muted {{ color: var(--muted); }}

  @media (max-width: 640px) {{
    header h1 {{ font-size: 26px; }}
    .grid {{ grid-template-columns: 1fr; }}
  }}
</style>
</head>
<body>
<header>
  <h1>Raytracer Render Quality Report</h1>
  <div class="sub">CPU path tracer &middot; PBR + glTF + OBJ &middot; generated {ts}</div>
  <div class="meta">
    <span>samples: <strong>{samples}</strong></span>
    <span>threads: <strong>{threads}</strong></span>
    <span>scenes rendered: <strong>{rendered}/{len(results)}</strong></span>
  </div>
</header>

<div class="container">

  <div class="summary-grid">
    <div class="summary-card pass"><div class="label">Passed</div><div class="value">{pass_count}</div></div>
    <div class="summary-card needs"><div class="label">Needs work</div><div class="value">{needs_count}</div></div>
    <div class="summary-card fail"><div class="label">Failed</div><div class="value">{fail_count}</div></div>
    <div class="summary-card"><div class="label">Scenes</div><div class="value">{len(results)}</div></div>
    <div class="summary-card"><div class="label">Samples/px</div><div class="value">{samples}</div></div>
  </div>

  <section>
    <h2>Rendering Principles</h2>
    <div class="principles">
      <ol>
        <li><strong>Path tracing</strong>: camera ray &rarr; closest hit &rarr; direct lighting + indirect bounce, recursive to <code>max_depth</code>.</li>
        <li><strong>Cook-Torrance PBR</strong>: GGX NDF + Smith geometry + Fresnel-Schlick, GGX importance sampling.</li>
        <li><strong>Transparent paths</strong>: Dielectric with Fresnel reflect/refract + <strong>partial transmission</strong> factor from <code>KHR_materials_transmission</code>.</li>
        <li><strong>Russian roulette</strong>: depth &ge; 4 terminates low-luminance paths, survivors scaled by 1/p (unbiased).</li>
        <li><strong>Firefly clamp</strong>: per-sample radiance peak clamped before accumulation to suppress glossy/caustic outliers.</li>
        <li><strong>Alpha mask</strong>: <code>alphaMode: MASK</code> discards hits with alpha &lt; cutoff, ray continues through.</li>
        <li><strong>Linear BVH</strong>: flat POD nodes + stack traversal, GPU-ready.</li>
      </ol>
    </div>
  </section>

  <section>
    <h2>Rendered Scenes</h2>
    <div class="filter-bar">
      <button class="active" data-filter="all">All</button>
      <button data-filter="baseline">Baseline</button>
      <button data-filter="transparent">Transparent</button>
      <button data-filter="pbr">PBR</button>
      <button data-filter="area-lit">Area-lit</button>
      <button data-filter="mesh-heavy">Mesh-heavy</button>
      <button data-filter="studio">Studio</button>
    </div>
    <div class="grid">
      {cards}
    </div>
  </section>

  <section>
    <h2>Performance Profile</h2>
    {cats}
    {perf}
  </section>

  <footer>
    Generated by <code>scripts/html_report.py</code> &middot; Raytracer feature_hdj branch &middot;
    data from <code>render_report.py</code> and <code>benchmark_report.py</code>
  </footer>
</div>

<script>
  const buttons = document.querySelectorAll('.filter-bar button');
  const cards = document.querySelectorAll('.card');
  buttons.forEach(b => b.addEventListener('click', () => {{
    buttons.forEach(x => x.classList.remove('active'));
    b.classList.add('active');
    const f = b.dataset.filter;
    cards.forEach(c => {{
      const show = f === 'all' || c.dataset.category === f;
      c.style.display = show ? '' : 'none';
    }});
  }}));
</script>
</body>
</html>
"""
    out_path.write_text(html, encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--render-dir", required=True,
                    help="directory with metrics.json + <scene>_<samples>s.png images")
    ap.add_argument("--benchmark", default=None,
                    help="benchmark_*.json from benchmark_report.py")
    ap.add_argument("--out", required=True, help="output .html path")
    args = ap.parse_args()

    render_dir = Path(args.render_dir).resolve()
    bench_path = Path(args.benchmark).resolve() if args.benchmark else None
    out_path = Path(args.out).resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    build_html(render_dir, bench_path, out_path)
    size_kb = out_path.stat().st_size / 1024
    print(f"Wrote {out_path} ({size_kb:.1f} KB)")


if __name__ == "__main__":
    main()
