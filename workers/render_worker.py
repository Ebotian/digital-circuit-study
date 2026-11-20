#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SVG/HTML renderer worker for dcstudy.

Reads one JSON request on stdin, writes one JSON response on stdout.  This is a
presentation-only worker: it receives already-validated design topology from
the C++ CLI and never decides whether a circuit is correct.
"""

from __future__ import annotations

import html
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


# ---------------------------------------------------------------------------
# small helpers
# ---------------------------------------------------------------------------

def xml_escape(s: str) -> str:
    return (str(s).replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;").replace('"', "&quot;"))


def lit_disp(lit: str) -> str:
    if lit == "0" or lit == "1":
        return lit
    if lit.endswith("b"):
        return "~" + lit[:-1]
    return lit


def comp_lit(lit: str) -> str:
    if lit.endswith("b"):
        return lit[:-1]
    return lit + "b"


def source_disp(src: str) -> str:
    if src == "1":
        return "VDD"
    if src == "0":
        return "GND"
    return lit_disp(src)


def has_tg(branch: dict[str, Any]) -> bool:
    return any(el.get("kind") in ("TGL", "TGH") for el in branch.get("chain", []))


def branch_text(branch: dict[str, Any]) -> str:
    src = source_disp(str(branch.get("source", "?")))
    chain = branch.get("chain", [])
    if not chain:
        return f"{src} → Y"
    parts = []
    for el in chain:
        kind = str(el.get("kind", "?"))
        if kind in ("TGL", "TGH"):
            kind = "TG"
        parts.append(f"{kind}({lit_disp(el.get('lit', '?'))})")
    return f"{src} → {' · '.join(parts)} → Y"


def switch_style() -> str:
    return """    .w{stroke:#1c1c1e;stroke-width:2.2;fill:none;stroke-linecap:round}
    .ch{stroke:#1c1c1e;stroke-width:7;fill:none;stroke-linecap:butt}
    .plate{stroke:#1c1c1e;stroke-width:3.2;fill:none;stroke-linecap:round}
    .dot{fill:#1c1c1e}
    .lbl{font-family:-apple-system,Segoe UI,Noto Sans CJK SC,sans-serif;font-size:18px;font-weight:600;fill:#1c1c1e}
    .small{font-family:-apple-system,Segoe UI,Noto Sans CJK SC,sans-serif;font-size:13.5px;fill:#6b6b70}
    .title{font-family:-apple-system,Segoe UI,Noto Sans CJK SC,sans-serif;font-size:20px;font-weight:700;fill:#1c1c1e}
    .bub{fill:#fff;stroke:#1c1c1e;stroke-width:1.9}
    .box{fill:#f7f8fa;stroke:#c9ccd2;stroke-width:1.4;stroke-dasharray:5 4}"""


def line(x1, y1, x2, y2, cls="w") -> str:
    return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" class="{cls}"/>'


def text(x, y, s, cls="small", anchor="start") -> str:
    return (f'<text x="{x}" y="{y}" class="{cls}" text-anchor="{anchor}">'
            f'{xml_escape(s)}</text>')


def circle(x, y, r=4.5, cls="dot") -> str:
    return f'<circle cx="{x}" cy="{y}" r="{r}" class="{cls}"/>'


# ---------------------------------------------------------------------------
# schematic primitives
# ---------------------------------------------------------------------------

def draw_np(x1: int, cy: int, sw: int, kind: str, gate: str) -> list[str]:
    """Horizontal N/P switch from x1 to x1+sw; gate label above."""
    x2 = x1 + sw
    cx = x1 + sw // 2
    out = [line(x1, cy, x2, cy, "ch")]
    out.append(line(x1 + 12, cy - 14, x2 - 12, cy - 14, "plate"))
    if kind == "P":
        out.append(f'<circle cx="{cx}" cy="{cy - 22}" r="6" class="bub"/>')
        out.append(line(cx, cy - 28, cx, cy - 42, "w"))
        out.append(text(cx, cy - 52, "g=" + gate, "small", "middle"))
    else:
        out.append(line(cx, cy - 14, cx, cy - 42, "w"))
        out.append(text(cx, cy - 52, "g=" + gate, "small", "middle"))
    out.append(text(x2 + 8, cy - 4, kind, "small"))
    return out


def draw_tg(x1: int, cy: int, sw: int, kind: str, lit: str) -> list[str]:
    """Transmission gate: PMOS above, NMOS below, with both control phases."""
    x2 = x1 + sw
    cx = x1 + sw // 2
    if kind == "TGL":
        pmos_gate, nmos_gate = lit, comp_lit(lit)
    else:  # TGH
        pmos_gate, nmos_gate = comp_lit(lit), lit
    out: list[str] = []
    # connecting stubs
    out.append(line(x1, cy - 40, x1, cy + 40, "w"))
    out.append(line(x2, cy - 40, x2, cy + 40, "w"))
    # upper PMOS
    out.append(line(x1, cy - 24, x2, cy - 24, "ch"))
    out.append(line(x1 + 12, cy - 38, x2 - 12, cy - 38, "plate"))
    out.append(f'<circle cx="{cx}" cy="{cy - 46}" r="6" class="bub"/>')
    out.append(line(cx, cy - 52, cx, cy - 64, "w"))
    out.append(text(cx, cy - 74, "g=" + lit_disp(pmos_gate), "small", "middle"))
    out.append(text(x2 + 8, cy - 28, "P", "small"))
    # lower NMOS
    out.append(line(x1, cy + 24, x2, cy + 24, "ch"))
    out.append(line(x1 + 12, cy + 38, x2 - 12, cy + 38, "plate"))
    out.append(line(cx, cy + 38, cx, cy + 60, "w"))
    out.append(text(cx, cy + 72, "g=" + lit_disp(nmos_gate), "small", "middle"))
    out.append(text(x2 + 8, cy + 20, "N", "small"))
    # label in the middle
    out.append(f'<rect x="{cx - 22}" y="{cy - 11}" width="44" height="22" rx="4" fill="#fff"/>')
    out.append(text(cx, cy + 5, "TG", "small", "middle"))
    return out


# ---------------------------------------------------------------------------
# whole-design SVG
# ---------------------------------------------------------------------------

def render_design_svg(design: dict[str, Any], vars_: list[str],
                      title: str | None = None) -> tuple[str, list[str]]:
    branches = design.get("branches", [])
    if not branches:
        raise ValueError("design has no branches")
    cost = int(design.get("cost", 0))
    invs = [str(x) for x in design.get("inverters", [])]
    max_chain = max((len(b.get("chain", [])) for b in branches), default=0)
    any_tg = any(has_tg(b) for b in branches)

    x0 = 200
    step = 170
    sw = 110
    ybus = x0 + max_chain * step + 85
    width = max(980, ybus + 190)
    top = 100
    rows: list[tuple[dict[str, Any], int, int]] = []
    y = top
    for b in branches:
        h = 180 if has_tg(b) else 130
        cy = y + h // 2
        rows.append((b, cy, h))
        y += h + 12
    bottom = rows[-1][1] + 50
    notes = [v for i, v in enumerate(invs)]
    note_rows = (len(notes) + 2) // 3
    height = max(320, bottom + 50 + note_rows * 86 + 50)

    s: list[str] = []
    s.append(f'<svg viewBox="0 0 {width} {height}" xmlns="http://www.w3.org/2000/svg">')
    s.append(f'<rect width="{width}" height="{height}" fill="#fff"/>')
    s.append("<style>")
    s.append(switch_style())
    s.append("  </style>")

    display_title = title or "pass-network design"
    s.append(text(40, 38, display_title, "title"))
    extra = f"{cost}T · {len(branches)} 条支路"
    if invs:
        extra += " · INV(" + ",".join(invs) + ")"
    s.append(text(40, 62, extra))

    # output bus
    y_mid = (rows[0][1] + rows[-1][1]) // 2
    s.append(line(ybus, top - 20, ybus, bottom, "w"))
    s.append(line(ybus, y_mid, ybus + 55, y_mid, "w"))
    s.append(text(ybus + 68, y_mid + 6, "Y", "lbl"))

    for b, cy, _ in rows:
        src = source_disp(str(b.get("source", "?")))
        s.append(text(80, cy + 6, src, "lbl", "end"))
        s.append(line(92, cy, x0, cy, "w"))
        chain = b.get("chain", [])
        if not chain:
            s.append(line(x0, cy, ybus, cy, "w"))
        for j, el in enumerate(chain):
            x1 = x0 + j * step
            x2 = x1 + sw
            kind = el.get("kind", "N")
            lit = str(el.get("lit", "?"))
            if kind in ("N", "P"):
                s.extend(draw_np(x1, cy, sw, kind, lit_disp(lit)))
            elif kind in ("TGL", "TGH"):
                s.extend(draw_tg(x1, cy, sw, kind, lit))
            else:
                raise ValueError(f"unknown switch kind: {kind}")
            nxt = x0 + (j + 1) * step if j + 1 < len(chain) else ybus
            s.append(line(x2, cy, nxt, cy, "w"))
        s.append(circle(ybus, cy))

    # inverter notes
    for i, var in enumerate(notes):
        col = i % 3
        row = i // 3
        bx = 55 + col * 280
        by = bottom + 35 + row * 86
        s.append(f'<rect x="{bx}" y="{by}" width="246" height="66" rx="10" class="box"/>')
        s.append(text(bx + 16, by + 25, f"~{var} = INV({var})"))
        s.append(text(bx + 16, by + 47, "2T: PMOS + NMOS"))

    # legend
    ly = bottom + 45 + note_rows * 86
    height = max(height, ly + 50)
    s.append(text(55, ly, "● 实心点 = 连接"))
    s.append(text(55, ly + 22, "带小圆圈 = PMOS　·　无圆圈 = NMOS"))
    s.append("</svg>")
    return "\n".join(s), notes


def render_inverter_svg(inp: str, out: str) -> str:
    t = f"2T CMOS 反相器 · {inp} → {out}"
    s: list[str] = []
    s.append('<svg viewBox="0 0 560 340" xmlns="http://www.w3.org/2000/svg">')
    s.append('<rect width="560" height="340" fill="#fff"/>')
    s.append("<style>")
    s.append(switch_style())
    s.append("  </style>")
    s.append(text(30, 32, t, "title"))
    s.append(text(30, 54, f"{inp} 同时驱动 PMOS 与 NMOS 的栅极"))
    # VDD
    s.append(line(240, 80, 240, 120, "w"))
    s.append(line(215, 80, 265, 80, "plate"))
    s.append(text(240, 70, "VDD", "small", "middle"))
    # PMOS
    s.append(line(240, 120, 240, 160, "ch"))
    s.append(line(226, 132, 226, 148, "plate"))
    s.append(circle(214, 140, 6, "bub"))
    s.append(line(208, 140, 150, 140, "w"))
    s.append(text(260, 142, "P", "small"))
    # output
    s.append(line(240, 160, 240, 200, "w"))
    s.append(circle(240, 180))
    s.append(line(240, 180, 350, 180, "w"))
    s.append(text(362, 186, out, "lbl"))
    # NMOS
    s.append(line(240, 200, 240, 240, "ch"))
    s.append(line(226, 212, 226, 228, "plate"))
    s.append(line(226, 220, 150, 220, "w"))
    s.append(text(260, 222, "N", "small"))
    # input bus
    s.append(line(150, 140, 150, 220, "w"))
    s.append(line(90, 180, 150, 180, "w"))
    s.append(circle(150, 180))
    s.append(text(78, 186, inp, "lbl", "end"))
    # GND
    s.append(line(240, 240, 240, 270, "w"))
    s.append(line(215, 270, 265, 270, "plate"))
    s.append(line(225, 278, 255, 278, "plate"))
    s.append(line(235, 286, 245, 286, "plate"))
    s.append(text(278, 272, "GND", "small"))
    s.append("</svg>")
    return "\n".join(s)


# ---------------------------------------------------------------------------
# index page
# ---------------------------------------------------------------------------

def truth_table_html(vars_: list[str], truth: str) -> str:
    n = len(vars_)
    if len(truth) != 1 << n:
        raise ValueError("truth table length does not match vars")
    head = "".join(f"<th>{html.escape(v)}</th>" for v in vars_) + "<th>Y</th>"
    rows = []
    for m in range(1 << n):
        bits = [(m >> (n - 1 - i)) & 1 for i in range(n)]
        cells = "".join(f"<td>{b}</td>" for b in bits)
        rows.append(f"<tr>{cells}<td>{truth[m]}</td></tr>")
    return f"<table><thead><tr>{head}</tr></thead><tbody>{''.join(rows)}</tbody></table>"


def render_index_html(spec: dict[str, Any], designs: list[dict[str, Any]],
                      svg_files: list[str], inverter_files: list[dict[str, str]],
                      canonical: bool = False) -> str:
    vars_ = [str(v) for v in spec["vars"]]
    truth = str(spec["truth"])
    title = str(spec.get("title") or spec.get("expr") or "design")
    optimum = int(designs[0].get("cost", 0)) if designs else 0

    cards: list[str] = []
    for i, (d, svg) in enumerate(zip(designs, svg_files)):
        branches = "".join(
            f"<li><code>{html.escape(branch_text(b))}</code></li>"
            for b in d.get("branches", []))
        invs = ", ".join(str(x) for x in d.get("inverters", [])) or "none"
        cards.append(f"""
  <h2><span class="num">{chr(65 + i)}</span>方案 {i + 1} · {int(d.get('cost', 0))}T</h2>
  <div class="fig"><img src="{html.escape(svg)}" alt="schematic {i}">
    <div class="cap">图 {chr(65 + i)}　{html.escape(cost_line(d))}</div></div>
  <p class="small">反相器：{html.escape(invs)}</p>
  <ul>{branches}</ul>""")

    inv_html = ""
    if inverter_files:
        inv_html = '<h2><span class="num">C</span>2T 反相器细节</h2>'
        for item in inverter_files:
            inv_html += (f'<div class="fig"><img src="{html.escape(item["svg"])}" '
                         f'alt="inverter {html.escape(item["var"])}"></div>')

    return f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<title>{html.escape(title)} · 电路渲染</title>
<style>
  :root{{--ink:#1c1c1e;--mute:#6b6b70;--line:#e2e2e6;--blue:#1c6dd0;--green:#1a7f45}}
  *{{box-sizing:border-box}}
  body{{margin:0;padding:0 0 70px;background:#fff;color:var(--ink);
    font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","Noto Sans CJK SC","PingFang SC",sans-serif;
    font-size:16.5px;line-height:1.75}}
  .wrap{{max-width:1040px;margin:0 auto;padding:0 28px}}
  header{{background:linear-gradient(135deg,#0f2540 0%,#1c4a75 55%,#2b6a9e 100%);
    color:#fff;padding:46px 28px 38px;margin-bottom:34px}}
  header h1{{margin:0 0 10px;font-size:2em}}
  header p{{margin:4px 0;color:#cfe0f0}}
  header code{{background:rgba(0,0,0,.28);color:#fff;padding:1.5px 6px;border-radius:5px}}
  h2{{font-size:1.36em;margin:50px 0 16px;padding-bottom:9px;border-bottom:3px solid var(--ink)}}
  h2 .num{{display:inline-block;background:var(--ink);color:#fff;border-radius:7px;
    padding:1px 10px;margin-right:10px;font-size:.78em;vertical-align:3px}}
  .fig{{margin:24px 0;text-align:center}}
  .fig img{{max-width:100%;height:auto;border:1px solid var(--line);border-radius:12px;background:#fff}}
  .cap{{font-size:.88em;color:var(--mute);margin-top:9px}}
  table{{border-collapse:collapse;width:100%;margin:18px 0;font-size:.94em}}
  th,td{{border:1px solid var(--line);padding:8px 12px;text-align:center}}
  th{{background:#f5f6f8}}
  tbody tr:nth-child(even){{background:#fafbfc}}
  code{{background:#f0f1f3;padding:1.5px 6px;border-radius:5px;font-family:Menlo,Consolas,monospace;font-size:.9em}}
  ul{{margin:8px 0 0;padding-left:22px}}
  li{{margin:4px 0}}
  .small{{font-size:.9em;color:var(--mute)}}
  .box{{border-left:5px solid var(--green);background:#edf9f1;padding:14px 18px;border-radius:0 10px 10px 0;margin:22px 0}}
  .box p:first-child{{margin-top:0}}.box p:last-child{{margin-bottom:0}}
</style>
</head>
<body>
<header><div class="wrap">
  <h1>{html.escape(title)} · 电路渲染</h1>
  <p><code>dcstudy render</code> 自动生成 · truth = {html.escape(truth)} · optimum = {optimum}T</p>
  <p>{'方案按保 f 的输入置换归一化' if canonical else '方案为原始最优解，未做对称归一化'}；晶体管符号与讲义一致。</p>
</div></header>
<div class="wrap">
  <div class="box"><p><strong>真值表</strong>（输入顺序 {html.escape(', '.join(vars_))}）</p>{truth_table_html(vars_, truth)}</div>
  {''.join(cards)}
  {inv_html}
  <div class="box"><p><strong>图例：</strong>带小圆圈的栅极 = PMOS，无圆圈 = NMOS；实心点 = 电气连接；TG = 传输门（PMOS ∥ NMOS，两相控制）。</p></div>
</div>
</body>
</html>
"""


def cost_line(d: dict[str, Any]) -> str:
    parts = []
    for b in d.get("branches", []):
        parts.append(branch_text(b))
    return " · ".join(parts)


# ---------------------------------------------------------------------------
# entry point
# ---------------------------------------------------------------------------

def main() -> int:
    try:
        req = json.load(sys.stdin)
        if req.get("op") != "render":
            raise ValueError("unsupported op")
        out = Path(str(req["out"]))
        out.mkdir(parents=True, exist_ok=True)
        out_abs = out.resolve()

        # Remove files from the previous run listed in manifest.json.  This
        # keeps reruns with fewer designs or without --png from leaving stale
        # schemes; files not recorded in the manifest are never touched.
        manifest = out / "manifest.json"
        if manifest.exists():
            try:
                old = json.loads(manifest.read_text(encoding="utf-8"))
                for f in old.get("files", []):
                    p = Path(str(f)).resolve()
                    if p.parent == out_abs or out_abs in p.parents:
                        p.unlink(missing_ok=True)
            except Exception:
                pass

        vars_ = [str(v) for v in req["vars"]]
        truth = str(req["truth"])
        title = str(req.get("title") or "")
        png = bool(req.get("png", False))
        designs = list(req.get("designs", []))
        if not designs:
            raise ValueError("no designs to render")

        rsvg = shutil.which("rsvg-convert") if png else None
        svg_files: list[str] = []
        design_results: list[dict[str, Any]] = []
        all_files: list[str] = []
        invs: list[str] = []
        for d in designs:
            for v in d.get("inverters", []):
                if str(v) not in invs:
                    invs.append(str(v))

        for i, d in enumerate(designs):
            svg, design_invs = render_design_svg(
                d, vars_, title=f"{title or 'design'} · {int(d.get('cost', 0))}T")
            svg_path = out / f"scheme-{i}.svg"
            svg_path.write_text(svg, encoding="utf-8")
            svg_files.append(svg_path.name)
            all_files.append(str(svg_path.resolve()))
            png_path = None
            if rsvg:
                p = out / f"scheme-{i}.png"
                r = subprocess.run([rsvg, "-w", "1600", str(svg_path), "-o", str(p)],
                                   capture_output=True, text=True)
                if r.returncode == 0:
                    png_path = str(p.resolve())
                    all_files.append(png_path)
            design_results.append({
                "svg": str(svg_path.resolve()),
                "png": png_path,
                "cost": int(d.get("cost", 0)),
                "branches": len(d.get("branches", [])),
            })

        inverter_files: list[dict[str, str]] = []
        for i, v in enumerate(invs):
            svg = render_inverter_svg(v, "~" + v)
            p = out / f"inverter-{i}.svg"
            p.write_text(svg, encoding="utf-8")
            inverter_files.append({"var": v, "svg": p.name})
            all_files.append(str(p.resolve()))

        index = out / "index.html"
        index.write_text(render_index_html(
            {"vars": vars_, "truth": truth, "title": title or "design"},
            designs, svg_files, inverter_files, bool(req.get("canonical", False))),
            encoding="utf-8")
        all_files.append(str(index.resolve()))

        manifest.write_text(json.dumps({"files": all_files}, indent=2),
                            encoding="utf-8")
        all_files.append(str(manifest.resolve()))

        print(json.dumps({
            "ok": True,
            "out": str(out.resolve()),
            "index": str(index.resolve()),
            "files": all_files,
            "designs": design_results,
            "inverters": inverter_files,
        }))
        return 0
    except Exception as e:  # noqa: BLE001 - the protocol needs an error document
        print(json.dumps({"ok": False, "error": str(e)}))
        return 1


if __name__ == "__main__":
    sys.exit(main())
