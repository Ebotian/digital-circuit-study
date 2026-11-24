#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-only
// SVG/HTML renderer worker for dcstudy.  Runs directly on Node 23+/26 (type
// stripping); no build step and no npm dependencies.

import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { spawnSync } from "node:child_process";

type Elem = { kind: string; lit: string; cost?: number };
type Branch = { source: string; chain: Elem[]; cost?: number };
type Design = {
  cost: number; inverters: string[]; branches: Branch[];
  kind?: string; pd_terms?: string[][]; pu_groups?: string[][];
  pull_down?: string; pull_up?: string; topology?: string;
  nfet?: number; pfet?: number;
  cells?: Array<{
    type: string;
    ports: Record<string, string>;
    directions?: Record<string, string>;
  }>;
  outputs?: string[];
  truths?: Record<string, string>;
};
type Request = {
  op: string; out: string; vars: string[]; truth: string; title?: string;
  png?: boolean; canonical?: boolean; logic?: boolean; designs: Design[];
  outputs?: string[]; truths?: Record<string, string>;
};

function esc(value: unknown): string {
  return String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;")
    .replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function litDisp(lit: string): string {
  if (lit === "0" || lit === "1") return lit;
  if (lit.endsWith("b")) return "~" + lit.slice(0, -1);
  return lit;
}

function compLit(lit: string): string {
  return lit.endsWith("b") ? lit.slice(0, -1) : lit + "b";
}

function sourceDisp(src: string): string {
  if (src === "1") return "VDD";
  if (src === "0") return "GND";
  return litDisp(src);
}

function hasTg(b: Branch): boolean {
  return b.chain.some((e) => e.kind === "TGL" || e.kind === "TGH");
}

function branchText(b: Branch): string {
  const src = sourceDisp(b.source);
  if (b.chain.length === 0) return src + " → Y";
  const parts = b.chain.map((e) => {
    const kind = e.kind === "TGL" || e.kind === "TGH" ? "TG" : e.kind;
    return kind + "(" + litDisp(e.lit) + ")";
  });
  return src + " → " + parts.join(" · ") + " → Y";
}

function style(): string {
  return [
    ".w{stroke:#1c1c1e;stroke-width:2.2;fill:none;stroke-linecap:round}",
    ".ch{stroke:#1c1c1e;stroke-width:7;fill:none;stroke-linecap:butt}",
    ".plate{stroke:#1c1c1e;stroke-width:3.2;fill:none;stroke-linecap:round}",
    ".dot{fill:#1c1c1e}",
    ".lbl{font-family:-apple-system,Segoe UI,Noto Sans CJK SC,sans-serif;font-size:18px;font-weight:600;fill:#1c1c1e}",
    ".small{font-family:-apple-system,Segoe UI,Noto Sans CJK SC,sans-serif;font-size:13.5px;fill:#6b6b70}",
    ".title{font-family:-apple-system,Segoe UI,Noto Sans CJK SC,sans-serif;font-size:20px;font-weight:700;fill:#1c1c1e}",
    ".bub{fill:#fff;stroke:#1c1c1e;stroke-width:1.9}",
  ].join("\n");
}

function line(x1: number, y1: number, x2: number, y2: number, cls = "w"): string {
  return '<line x1="' + x1 + '" y1="' + y1 + '" x2="' + x2 + '" y2="' + y2 +
    '" class="' + cls + '"/>';
}

function text(x: number, y: number, value: string, cls = "small", anchor = "start"): string {
  return '<text x="' + x + '" y="' + y + '" class="' + cls +
    '" text-anchor="' + anchor + '">' + esc(value) + "</text>";
}

function circle(x: number, y: number, r = 4.5, cls = "dot"): string {
  return '<circle cx="' + x + '" cy="' + y + '" r="' + r + '" class="' + cls + '"/>';
}

function drawNp(x1: number, cy: number, sw: number, kind: string, gate: string): string[] {
  const x2 = x1 + sw;
  const cx = x1 + Math.floor(sw / 2);
  const out: string[] = [line(x1, cy, x2, cy, "ch"),
    line(x1 + 12, cy - 14, x2 - 12, cy - 14, "plate")];
  if (kind === "P") {
    out.push('<circle cx="' + cx + '" cy="' + (cy - 22) + '" r="6" class="bub"/>');
    out.push(line(cx, cy - 28, cx, cy - 42, "w"));
  } else {
    out.push(line(cx, cy - 14, cx, cy - 42, "w"));
  }
  out.push(text(cx, cy - 52, "g=" + gate, "small", "middle"));
  out.push(text(x2 + 8, cy - 4, kind, "small"));
  return out;
}

function drawTg(x1: number, cy: number, sw: number, kind: string, lit: string): string[] {
  const x2 = x1 + sw;
  const cx = x1 + Math.floor(sw / 2);
  const pmosGate = kind === "TGL" ? lit : compLit(lit);
  const nmosGate = kind === "TGL" ? compLit(lit) : lit;
  const out: string[] = [];
  out.push(line(x1, cy - 40, x1, cy + 40, "w"));
  out.push(line(x2, cy - 40, x2, cy + 40, "w"));
  out.push(line(x1, cy - 24, x2, cy - 24, "ch"));
  out.push(line(x1 + 12, cy - 38, x2 - 12, cy - 38, "plate"));
  out.push(circle(cx, cy - 46, 6, "bub"));
  out.push(line(cx, cy - 52, cx, cy - 64, "w"));
  out.push(text(cx, cy - 74, "g=" + litDisp(pmosGate), "small", "middle"));
  out.push(text(x2 + 8, cy - 28, "P", "small"));
  out.push(line(x1, cy + 24, x2, cy + 24, "ch"));
  out.push(line(x1 + 12, cy + 38, x2 - 12, cy + 38, "plate"));
  out.push(line(cx, cy + 38, cx, cy + 60, "w"));
  out.push(text(cx, cy + 72, "g=" + litDisp(nmosGate), "small", "middle"));
  out.push(text(x2 + 8, cy + 20, "N", "small"));
  out.push('<rect x="' + (cx - 22) + '" y="' + (cy - 11) +
    '" width="44" height="22" rx="4" fill="#fff"/>');
  out.push(text(cx, cy + 5, "TG", "small", "middle"));
  return out;
}

// A 2T CMOS inverter drawn as a vertical stack between VDD and GND.
function drawInverter(cy: number, input: string, railX: number): string[] {
  const x = 135;
  const gateX = 90;
  const out: string[] = [];
  out.push(text(28, cy + 6, input, "lbl", "end"));
  // VDD
  out.push(line(x, cy - 60, x, cy - 78, "w"));
  out.push(line(x - 28, cy - 78, x + 28, cy - 78, "plate"));
  out.push(text(x, cy - 86, "VDD", "small", "middle"));
  // PMOS
  out.push(line(x, cy - 60, x, cy - 15, "ch"));
  out.push(line(x - 14, cy - 53, x - 14, cy - 32, "plate"));
  out.push(circle(x - 21, cy - 42, 6, "bub"));
  out.push(line(x - 27, cy - 42, gateX, cy - 42, "w"));
  out.push(text(x + 16, cy - 38, "P", "small"));
  // output node
  out.push(line(x, cy - 15, x, cy + 15, "w"));
  out.push(circle(x, cy));
  out.push(line(x, cy, railX, cy, "w"));
  out.push(circle(railX, cy));
  // NMOS
  out.push(line(x, cy + 15, x, cy + 60, "ch"));
  out.push(line(x - 14, cy + 32, x - 14, cy + 53, "plate"));
  out.push(line(x - 14, cy + 42, gateX, cy + 42, "w"));
  out.push(text(x + 16, cy + 38, "N", "small"));
  // GND
  out.push(line(x, cy + 60, x, cy + 78, "w"));
  out.push(line(x - 28, cy + 78, x + 28, cy + 78, "plate"));
  out.push(line(x - 18, cy + 86, x + 18, cy + 86, "plate"));
  out.push(line(x - 8, cy + 94, x + 8, cy + 94, "plate"));
  out.push(text(x + 36, cy + 80, "GND", "small"));
  // input gate bus
  out.push(line(gateX, cy - 42, gateX, cy + 42, "w"));
  out.push(line(55, cy, gateX, cy, "w"));
  out.push(circle(gateX, cy));
  return out;
}

function isInvertedSource(src: string, inverters: string[]): boolean {
  if (!src.endsWith("b")) return false;
  return inverters.includes(src.slice(0, -1));
}

function drawVDevice(x: number, top: number, bottom: number, kind: string, gate: string): string[] {
  const out: string[] = [];
  const cy = Math.floor((top + bottom) / 2);
  out.push(line(x, top, x, bottom, "ch"));
  out.push(line(x - 14, top + 10, x - 14, bottom - 10, "plate"));
  if (kind === "P") {
    out.push('<circle cx="' + (x - 21) + '" cy="' + cy + '" r="6" class="bub"/>');
    out.push(line(x - 27, cy, x - 42, cy, "w"));
  } else {
    out.push(line(x - 14, cy, x - 42, cy, "w"));
  }
  out.push(text(x - 50, cy + 4, gate, "small", "end"));
  out.push(text(x + 10, cy + 4, kind, "small"));
  return out;
}

function renderStaticCmosSvg(d: Design, title: string): string {
  const pd = d.pd_terms || [];
  const pu = d.pu_groups || pd;
  if (pd.length === 0) throw new Error("static CMOS design has no terms");
  const maxP = Math.max(...pu.map((g) => g.length), 1);
  const maxN = Math.max(...pd.map((t) => t.length), 1);
  const width = Math.max(760, 220 + pd.length * 140, 220 + maxP * 110);
  const cx = Math.floor(width / 2);

  const groupH = 100;
  const groupGap = 20;
  const punTop = 110;
  const punBottom = punTop + pu.length * groupH + (pu.length - 1) * groupGap;
  const yNode = punBottom + 70;
  const pdnTop = yNode;
  const pdnBottom = pdnTop + maxN * 90 + 40;
  const height = pdnBottom + 100;

  const parts: string[] = [];
  parts.push('<svg viewBox="0 0 ' + width + " " + height + '" xmlns="http://www.w3.org/2000/svg">');
  parts.push('<rect width="' + width + '" height="' + height + '" fill="#fff"/>');
  parts.push("<style>");
  parts.push(style());
  parts.push("</style>");
  parts.push(text(40, 38,
    title + (d.cost > 0 ? " · " + d.cost + "T" : ""), "title"));
  parts.push(text(40, 62, "static complementary CMOS · " + (d.nfet || 0) +
    " NMOS + " + (d.pfet || 0) + " PMOS"));

  // VDD rail
  parts.push(line(80, 80, width - 80, 80, "w"));
  parts.push(text(90, 100, "VDD", "small"));
  parts.push(line(cx, 80, cx, punTop, "w"));

  // PUN: series groups, each group is parallel PMOS.
  let groupTop = punTop;
  let prevBottom: number | null = null;
  pu.forEach((group) => {
    const groupBottom = groupTop + groupH;
    if (prevBottom !== null) parts.push(line(cx, prevBottom, cx, groupTop, "w"));
    const k = group.length;
    const xs: number[] = [];
    for (let i = 0; i < k; i++) xs.push(cx - Math.floor((k - 1) * 100 / 2) + i * 100);
    const left = k === 0 ? cx - 30 : Math.min(...xs) - 40;
    const right = k === 0 ? cx + 30 : Math.max(...xs) + 40;
    parts.push(line(left, groupTop, right, groupTop, "w"));
    parts.push(line(left, groupBottom, right, groupBottom, "w"));
    if (k === 0) parts.push(line(cx, groupTop, cx, groupBottom, "w"));
    group.forEach((gate, i) => {
      parts.push(...drawVDevice(xs[i], groupTop, groupBottom, "P", gate));
    });
    prevBottom = groupBottom;
    groupTop = groupBottom + groupGap;
  });

  // PUN bottom to Y
  parts.push(line(cx, punBottom, cx, yNode, "w"));
  parts.push(circle(cx, yNode));
  parts.push(line(cx, yNode, cx + 55, yNode, "w"));
  parts.push(text(cx + 68, yNode - 8, "Y", "lbl"));

  // PDN: parallel terms, each is series NMOS from Y to GND.
  const termXs: number[] = [];
  const spacing = 130;
  for (let i = 0; i < pd.length; i++)
    termXs.push(cx - Math.floor((pd.length - 1) * spacing / 2) + i * spacing);
  const left = Math.min(...termXs) - 50;
  const right = Math.max(...termXs) + 50;
  parts.push(line(left, pdnTop, right, pdnTop, "w"));
  parts.push(line(left, pdnBottom, right, pdnBottom, "w"));
  parts.push(text(left, pdnBottom + 26, "GND", "small"));

  pd.forEach((term, ti) => {
    const x = termXs[ti];
    parts.push(circle(x, pdnTop));
    parts.push(line(x, pdnTop, x, pdnTop + 20, "w"));
    let y = pdnTop + 20;
    term.forEach((gate) => {
      parts.push(...drawVDevice(x, y, y + 70, "N", gate));
      y += 80;
    });
    parts.push(line(x, y, x, pdnBottom, "w"));
    parts.push(circle(x, pdnBottom));
  });

  parts.push(text(24, punTop + 20, "PUN", "small"));
  parts.push(text(24, pdnTop + 20, "PDN", "small"));
  parts.push(text(55, height - 28, "● 实心点 = 连接　·　带小圆圈 = PMOS　·　无圆圈 = NMOS"));
  parts.push("</svg>");
  return parts.join("\n");
}

function tikzPort(type: string, n: number): string {
  const base = type.replace(/_X[0-9]+$/, "").replace(/_/g, "").toUpperCase();
  let shape = "";
  if (base.startsWith("NAND")) shape = "nand port";
  else if (base.startsWith("NOR")) shape = "nor port";
  else if (base.startsWith("XNOR")) shape = "xnor port";
  else if (base.startsWith("XOR")) shape = "xor port";
  else if (base.startsWith("AND")) shape = "and port";
  else if (base.startsWith("OR")) shape = "or port";
  else if (base.startsWith("INV") || base.startsWith("NOT")) shape = "not port";
  else if (base.startsWith("BUF")) shape = "buffer port";
  else return "";
  return shape + (shape === "not port" ? "" : ", number inputs=" + n);
}

function renderGateLevelSvg(d: Design, vars: string[], title: string): string {
  const cells = d.cells || [];
  if (cells.length === 0) throw new Error("multi-stage design has no cells");
  const outs = d.outputs && d.outputs.length ? d.outputs : ["Y"];
  const cellLabel = (type: string) => {
    const map: Record<string, string> = {
      "$or": "OR", "$and": "AND", "$not": "INV", "$mux": "MUX2",
      "$xor": "XOR", "$xnor": "XNOR", "$nand": "NAND", "$nor": "NOR",
      "$reduce_or": "OR-RED", "$reduce_and": "AND-RED",
    };
    return map[type] || type;
  };
  const outPort = (c: { type: string; ports: Record<string, string> }) => {
    for (const p of ["ZN", "Z", "Q", "Y", "CO", "S"]) if (c.ports[p]) return p;
    const keys = Object.keys(c.ports);
    return keys.length ? keys[keys.length - 1] : "";
  };
  const outPorts = (c: {
    type: string; ports: Record<string, string>;
    directions?: Record<string, string>;
  }) => {
    if (c.directions)
      return Object.keys(c.ports)
        .filter((p) => c.directions?.[p] === "output").sort();
    return [outPort(c)];
  };
  const inputsOf = (c: {
    type: string; ports: Record<string, string>;
    directions?: Record<string, string>;
  }) => {
    if (c.directions)
      return Object.keys(c.ports)
        .filter((p) => c.directions?.[p] === "input").sort();
    return Object.keys(c.ports).filter((p) => p !== outPort(c)).sort();
  };

  // Reuse the circuitikz logic-port flow.  If any cell cannot be expressed as
  // a standard port (e.g. a compound AOI/OAI), fall back to the generic boxes.
  const shapes = cells.map((c) => tikzPort(c.type, inputsOf(c).length));
  if (shapes.some((s) => s === "")) return renderGateLevelSvgBoxes(d, vars, title);
  if (spawnSync("pdflatex", ["--version"]).status !== 0 ||
      spawnSync("pdftocairo", ["-v"]).status !== 0)
    return renderGateLevelSvgBoxes(d, vars, title);

  const producer = new Map<string, number>();
  cells.forEach((c, i) => {
    for (const p of outPorts(c)) if (c.ports[p]) producer.set(c.ports[p], i);
  });
  const level = new Array<number>(cells.length).fill(0);
  for (let iter = 0; iter < cells.length; iter++) {
    for (let i = 0; i < cells.length; i++) {
      for (const p of inputsOf(cells[i])) {
        const src = producer.get(cells[i].ports[p]);
        if (src !== undefined && src !== i)
          level[i] = Math.max(level[i], level[src] + 1);
      }
    }
  }
  // Primary outputs go in the rightmost column, even when a gate has no
  // internal fanin.  This keeps output stubs out of the gate array.
  const maxLevel = Math.max(...level, 0);
  cells.forEach((c, i) => {
    if (outs.indexOf(c.ports[outPort(c)]) >= 0) level[i] = maxLevel;
  });
  const rows = new Map<number, number>();
  const pos = cells.map((_, i) => {
    const row = rows.get(level[i]) ?? 0;
    rows.set(level[i], row + 1);
    return { x: 1.2 + level[i] * 4.2, y: -0.8 - row * 2.0 };
  });
  const inAnchor = (i: number, j: number) => {
    const s = shapes[i];
    return (s === "not port" || s === "buffer port") ? ".in" : ".in " + (j + 1);
  };

  const lines: string[] = [];
  lines.push("\\documentclass[border=8pt]{standalone}");
  lines.push("\\usepackage[american]{circuitikz}");
  lines.push("\\begin{document}");
  lines.push("\\begin{circuitikz}[x=1cm,y=1cm]");
  cells.forEach((c, i) => {
    const p = pos[i];
    lines.push("\\node[" + shapes[i] + "] (g" + i + ") at (" + p.x + "," + p.y + ") {};");
    lines.push("\\node[above=1pt,font=\\tiny] at (g" + i +
      ".north) {" + c.type.replace(/_/g, "\\_") + "};");
  });
  cells.forEach((c, i) => {
    inputsOf(c).forEach((port, j) => {
      const net = c.ports[port];
      const src = producer.get(net);
      if (src !== undefined && src !== i) {
        // Give every source cell its own vertical track, so unrelated nets
        // never share a vertical segment.  Keep a uniform minimum lane gap;
        // 0.11 cm was visually too tight for adjacent gate outputs.
        const WIRE_GAP = 0.38;
        const track = 1.0 + WIRE_GAP * (src % 8);
        // Orthogonal PCB-style routing: horizontal stub, vertical trunk,
        // horizontal entry into the actual pin.  The old cell-center dy made
        // the final segment diagonal because gate pins are offset from center.
        lines.push("\\draw (g" + src + ".out) -- ++(" + track.toFixed(2) +
          ",0) |- (g" + i + inAnchor(i, j) + ");");
      } else if (vars.indexOf(net) >= 0) {
        lines.push("\\draw (g" + i + inAnchor(i, j) +
          ") -- ++(-0.55,0) node[left]{" + net.replace(/_/g, "\\_") + "};");
      }
    });
  });
  outs.forEach((net, oi) => {
    const src = producer.get(net);
    if (src !== undefined) {
      lines.push("\\draw (g" + src + ".out) -- ++(0.7,0) node[right]{" +
        net.replace(/_/g, "\\_") + "};");
    } else {
      lines.push("\\node[right] at (" + (1.2 + (Math.max(...level, 0) + 1) * 3.2) +
        "," + (-0.8 - oi * 0.8) + ") {" + net.replace(/_/g, "\\_") + "};");
    }
  });
  lines.push("\\end{circuitikz}");
  lines.push("\\end{document}");

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "dcstudy-gates-"));
  try {
    const tex = path.join(dir, "gate.tex");
    const pdf = path.join(dir, "gate.pdf");
    const svg = path.join(dir, "gate.svg");
    fs.writeFileSync(tex, lines.join("\n"), "utf8");
    const r = spawnSync("pdflatex",
      ["-interaction=nonstopmode", "-halt-on-error", "-output-directory", dir, tex],
      { cwd: dir, encoding: "utf8" });
    if (r.status !== 0) return renderGateLevelSvgBoxes(d, vars, title);
    const c = spawnSync("pdftocairo", ["-svg", pdf, svg], { cwd: dir, encoding: "utf8" });
    if (c.status !== 0 || !fs.existsSync(svg))
      return renderGateLevelSvgBoxes(d, vars, title);
    return fs.readFileSync(svg, "utf8");
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

// Two-level rail-matrix view, the classic decoder/PLA drawing: primary inputs
// and their complements are vertical rails; product gates sit in one column and
// tap the selected rails.  This is structural, not decoder-specific: any
// two-level netlist (inverters from primary inputs + product gates over those
// literals) qualifies.
function renderGateLevelSvgRails(d: Design, vars: string[], title: string): string | null {
  const cells = d.cells || [];
  if (cells.length === 0) return null;
  const outs = d.outputs && d.outputs.length ? d.outputs : ["Y"];
  const cellLabel = (type: string) => {
    const map: Record<string, string> = {
      "$or": "OR", "$and": "AND", "$not": "INV", "$mux": "MUX2",
      "$xor": "XOR", "$xnor": "XNOR", "$nand": "NAND", "$nor": "NOR",
      "$reduce_or": "OR-RED", "$reduce_and": "AND-RED",
    };
    return map[type] || type;
  };
  const outPort = (c: { type: string; ports: Record<string, string> }) => {
    for (const p of ["ZN", "Z", "Q", "Y", "CO", "S"]) if (c.ports[p]) return p;
    const keys = Object.keys(c.ports);
    return keys.length ? keys[keys.length - 1] : "";
  };
  const outPorts = (c: {
    type: string; ports: Record<string, string>;
    directions?: Record<string, string>;
  }) => {
    if (c.directions)
      return Object.keys(c.ports)
        .filter((p) => c.directions?.[p] === "output").sort();
    return [outPort(c)];
  };
  const inputsOf = (c: {
    type: string; ports: Record<string, string>;
    directions?: Record<string, string>;
  }) => {
    if (c.directions)
      return Object.keys(c.ports)
        .filter((p) => c.directions?.[p] === "input").sort();
    return Object.keys(c.ports).filter((p) => p !== outPort(c)).sort();
  };
  const shapes = cells.map((c) => tikzPort(c.type, inputsOf(c).length));
  if (shapes.some((s) => s === "")) return null;

  const inv = new Map<string, { cell: number; input: string }>();
  const product: number[] = [];
  for (let i = 0; i < cells.length; ++i) {
    const c = cells[i];
    const ins = inputsOf(c);
    if (c.type.startsWith("INV") || c.type.startsWith("NOT")) {
      if (ins.length !== 1 || vars.indexOf(c.ports[ins[0]]) < 0) return null;
      inv.set(c.ports[outPort(c)], { cell: i, input: c.ports[ins[0]] });
    } else {
      for (const p of ins) {
        const n = c.ports[p];
        if (vars.indexOf(n) < 0 && !inv.has(n)) return null;
      }
      product.push(i);
    }
  }
  if (product.length === 0) return null;
  // Product outputs must be primary outputs (a true two-level netlist).
  for (const i of product) {
    const n = cells[i].ports[outPort(cells[i])];
    if (outs.indexOf(n) < 0) return null;
  }

  const railList: string[] = [];
  const invForVar = new Map<string, string>();
  for (const [out, info] of inv) invForVar.set(info.input, out);
  for (const v of vars) {
    railList.push(v);
    const c = invForVar.get(v);
    if (c) railList.push(c);
  }
  const railName = (net: string) => {
    const ii = inv.get(net);
    return ii ? ii.input + "'" : net;
  };
  const xOf = new Map<string, number>();
  let x = 0.5;
  for (const v of vars) {
    xOf.set(v, x);
    const c = invForVar.get(v);
    if (c) {
      xOf.set(c, x + 0.85);
      x += 1.7;
    } else {
      x += 0.85;
    }
  }
  const gateX = x + 2.1;
  product.sort((a, b) => {
    const ka = outs.indexOf(cells[a].ports[outPort(cells[a])]);
    const kb = outs.indexOf(cells[b].ports[outPort(cells[b])]);
    return (ka < 0 ? 1e6 : ka) - (kb < 0 ? 1e6 : kb);
  });
  const headerY = 1.3;
  const yOf = (row: number) => -0.9 - row * 2.0;
  const bottom = yOf(product.length) + 0.4;

  const lines: string[] = [];
  lines.push("\\documentclass[border=12pt]{standalone}");
  lines.push("\\usepackage[american]{circuitikz}");
  lines.push("\\begin{document}");
  lines.push("\\begin{circuitikz}[x=1cm,y=1cm]");
  for (const net of railList) {
    const rx = xOf.get(net)!;
    lines.push("\\coordinate (r_" + net.replace(/[^A-Za-z0-9_]/g, "_") +
      ") at (" + rx.toFixed(2) + ",0);");
    lines.push("\\draw (" + rx.toFixed(2) + "," + headerY.toFixed(2) + ") -- (" +
      rx.toFixed(2) + "," + bottom.toFixed(2) + ");");
    lines.push("\\node[font=\\scriptsize,fill=white,inner sep=1pt] at (" +
      rx.toFixed(2) + "," + (headerY + 0.75).toFixed(2) + ") {" +
      texEscape(railName(net)) + "};");
  }
  // Inverter header: primary rail in, complement rail out.
  for (const [out, info] of inv) {
    const base = xOf.get(info.input)!;
    const comp = xOf.get(out)!;
    lines.push("\\node[not port,scale=0.75] (inv_" + out.replace(/[^A-Za-z0-9_]/g, "_") +
      ") at (" + ((base + comp) / 2).toFixed(2) + "," + headerY.toFixed(2) + ") {};");
    lines.push("\\draw (" + base.toFixed(2) + "," + headerY.toFixed(2) +
      ") -- (inv_" + out.replace(/[^A-Za-z0-9_]/g, "_") + ".in);");
    lines.push("\\draw (inv_" + out.replace(/[^A-Za-z0-9_]/g, "_") + ".out) -- (" +
      comp.toFixed(2) + "," + headerY.toFixed(2) + ");");
  }
  // Product gates, one row per minterm/product term.
  product.forEach((ci, row) => {
    const c = cells[ci];
    const y = yOf(row);
    lines.push("\\node[" + shapes[ci] + "] (g" + ci + ") at (" + gateX.toFixed(2) +
      "," + y.toFixed(2) + ") {};");
    lines.push("\\node[above=2pt,font=\\tiny,fill=white,inner sep=1pt] at (g" + ci +
      ".north) {" + c.type.replace(/_/g, "\\_") + "};");
    inputsOf(c).forEach((p, j) => {
      const net = c.ports[p];
      const rx = xOf.get(net)!;
      const rn = "r_" + net.replace(/[^A-Za-z0-9_]/g, "_");
      const pin = "g" + ci + (shapes[ci] === "not port" || shapes[ci] === "buffer port"
        ? ".in" : ".in " + (j + 1));
      lines.push("\\draw (" + pin + " -| " + rn + ") -- (" + pin + ");");
      lines.push("\\fill (" + pin + " -| " + rn + ") circle (1.1pt);");
    });
    lines.push("\\draw (g" + ci + ".out) -- ++(0.95,0) node[above=2pt,right,fill=white,inner sep=1pt]{" +
      texEscape(c.ports[outPort(c)]) + "};");
  });
  lines.push("\\end{circuitikz}");
  lines.push("\\end{document}");

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "dcstudy-gates-rails-"));
  try {
    const tex = path.join(dir, "gate.tex");
    const pdf = path.join(dir, "gate.pdf");
    const svg = path.join(dir, "gate.svg");
    fs.writeFileSync(tex, lines.join("\n"), "utf8");
    const r = spawnSync("pdflatex",
      ["-interaction=nonstopmode", "-halt-on-error", "-output-directory", dir, tex],
      { cwd: dir, encoding: "utf8" });
    if (r.status !== 0) return null;
    const c = spawnSync("pdftocairo", ["-svg", pdf, svg], { cwd: dir, encoding: "utf8" });
    if (c.status !== 0 || !fs.existsSync(svg)) return null;
    return fs.readFileSync(svg, "utf8");
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

// Local-bus view: each level is a row band, the primary input bus runs on the
// left, and every signal that crosses a column boundary gets a vertical track
// in that boundary's channel.  A primary input used by a later level is copied
// into that level's channel by a short feeder in the inter-band gap, so no
// signal has to detour over the whole gate array.  Only nets that skip a logic
// level fall back to the global top bus.
function renderGateLevelSvgLocalBus(d: Design, vars: string[], title: string): string {
  const cells = d.cells || [];
  if (cells.length === 0) throw new Error("multi-stage design has no cells");
  const outs = d.outputs && d.outputs.length ? d.outputs : ["Y"];
  const cellLabel = (type: string) => {
    const map: Record<string, string> = {
      "$or": "OR", "$and": "AND", "$not": "INV", "$mux": "MUX2",
      "$xor": "XOR", "$xnor": "XNOR", "$nand": "NAND", "$nor": "NOR",
      "$reduce_or": "OR-RED", "$reduce_and": "AND-RED",
    };
    return map[type] || type;
  };
  const outPort = (c: { type: string; ports: Record<string, string> }) => {
    for (const p of ["ZN", "Z", "Q", "Y", "CO", "S"]) if (c.ports[p]) return p;
    const keys = Object.keys(c.ports);
    return keys.length ? keys[keys.length - 1] : "";
  };
  const outPorts = (c: {
    type: string; ports: Record<string, string>;
    directions?: Record<string, string>;
  }) => {
    if (c.directions)
      return Object.keys(c.ports)
        .filter((p) => c.directions?.[p] === "output").sort();
    return [outPort(c)];
  };
  const inputsOf = (c: {
    type: string; ports: Record<string, string>;
    directions?: Record<string, string>;
  }) => {
    if (c.directions)
      return Object.keys(c.ports)
        .filter((p) => c.directions?.[p] === "input").sort();
    return Object.keys(c.ports).filter((p) => p !== outPort(c)).sort();
  };
  const shapes = cells.map((c) => tikzPort(c.type, inputsOf(c).length));
  if (shapes.some((s) => s === ""))
    return renderGateLevelSvgBoxes(d, vars, title);
  if (spawnSync("pdflatex", ["--version"]).status !== 0 ||
      spawnSync("pdftocairo", ["-v"]).status !== 0)
    return renderGateLevelSvgBoxes(d, vars, title);

  const producer = new Map<string, number>();
  cells.forEach((c, i) => {
    for (const p of outPorts(c)) if (c.ports[p]) producer.set(c.ports[p], i);
  });
  const level = new Array<number>(cells.length).fill(0);
  for (let iter = 0; iter < cells.length; iter++) {
    for (let i = 0; i < cells.length; i++) {
      for (const p of inputsOf(cells[i])) {
        const src = producer.get(cells[i].ports[p]);
        if (src !== undefined && src !== i)
          level[i] = Math.max(level[i], level[src] + 1);
      }
    }
  }
  const maxLevel = Math.max(...level, 0);
  const usedAsInput = new Set<string>();
  for (const c of cells)
    for (const p of inputsOf(c)) usedAsInput.add(c.ports[p]);
  cells.forEach((c, i) => {
    const net = c.ports[outPort(c)];
    if (outs.indexOf(net) >= 0 && !usedAsInput.has(net)) level[i] = maxLevel;
  });
  const outRank = (i: number) => {
    const net = cells[i].ports[outPort(cells[i])];
    const k = outs.indexOf(net);
    return k >= 0 ? k : 1_000_000;
  };
  const order = cells.map((_, i) => i).sort((a, b) => {
    if (level[a] !== level[b]) return level[a] - level[b];
    const ra = outRank(a), rb = outRank(b);
    if (ra !== rb) return ra - rb;
    return a - b;
  });

  type Sink = { cell: number; port: string };
  const sinks = new Map<string, Sink[]>();
  for (let i = 0; i < cells.length; ++i) {
    for (const p of inputsOf(cells[i])) {
      const net = cells[i].ports[p];
      const list = sinks.get(net) || [];
      list.push({ cell: i, port: p });
      sinks.set(net, list);
    }
  }

  // Long internal nets skip a level; keep the global-bus fallback for them.
  for (const [net, list] of sinks) {
    const src = producer.get(net);
    if (src === undefined) continue;
    for (const sk of list)
      if (level[sk.cell] > level[src] + 1)
        return renderGateLevelSvgBus(d, vars, title);
  }

  // Level-banded rows.  Before level L, reserve one feeder row per primary
  // input used at that level, plus one blank row.
  const perLevel = new Map<number, number[]>();
  order.forEach((i) => {
    const list = perLevel.get(level[i]) || [];
    list.push(i);
    perLevel.set(level[i], list);
  });
  const levelInputs = new Map<number, string[]>();
  for (const net of vars) {
    const ls = new Set<number>();
    for (const sk of sinks.get(net) || []) if (level[sk.cell] > 0) ls.add(level[sk.cell]);
    for (const l of ls) {
      const list = levelInputs.get(l) || [];
      list.push(net);
      levelInputs.set(l, list);
    }
  }
  const yAt = (row: number) => -1 - row * 1.8;
  const rowOf = new Map<number, number>();
  const feedY = new Map<string, number>();
  let row = 0;
  for (let l = 0; l <= maxLevel; ++l) {
    if (l > 0) {
      const ins = levelInputs.get(l) || [];
      const gap = ins.length;
      ins.forEach((net, i) => feedY.set(l + ":" + net, yAt(row + i)));
      row += gap;
    }
    for (const i of perLevel.get(l) || []) rowOf.set(i, row++);
  }
  const baseX = 0.35 + Math.max(1, vars.length) * 0.45 + 1.8;
  const pos = cells.map((_, i) => ({
    x: baseX + level[i] * 4.2,
    y: yAt(rowOf.get(i) || 0),
  }));

  const display = new Map<string, string>();
  for (const v of vars) display.set(v, v);
  for (const o of outs) display.set(o, o);
  const visiting = new Set<string>();
  let internal = 0;
  const netName = (net: string): string => {
    const known = display.get(net);
    if (known !== undefined) return known;
    if (visiting.has(net)) return net;
    visiting.add(net);
    const src = producer.get(net);
    let result = "";
    if (src !== undefined) {
      const c = cells[src];
      const ip = inputsOf(c);
      if ((c.type.startsWith("INV") || c.type.startsWith("NOT")) && ip.length === 1)
        result = netName(c.ports[ip[0]]) + "'";
      else
        result = "n" + (++internal);
    } else {
      result = net;
    }
    visiting.delete(net);
    display.set(net, result);
    return result;
  };
  for (const [net] of producer) netName(net);
  const texNet = (net: string) => texEscape(netName(net));

  // Assign channel tracks per level.  A key is "level:net"; the same primary
  // input may have a different local track at each level.
  const trackX = new Map<string, number>();
  const trackKind = new Map<string, "internal" | "input">();
  for (let l = 1; l <= maxLevel; ++l) {
    const internalNets: string[] = [];
    for (const [net, src] of producer) {
      const list = sinks.get(net) || [];
      if (list.length === 0) continue;
      if (level[src] !== l - 1) continue;
      if (Math.max(...list.map((s) => level[s.cell])) !== l) continue;
      internalNets.push(net);
    }
    internalNets.sort((a, b) => (producer.get(a) || 0) - (producer.get(b) || 0));
    const inputs = levelInputs.get(l) || [];
    const keys = [
      ...internalNets.map((n) => "internal:" + n),
      ...inputs.map((n) => "input:" + n),
    ];
    const n = keys.length;
    if (n === 0) continue;
    const base = (baseX + (l - 1) * 4.2 + baseX + l * 4.2) / 2 -
      (n - 1) * 0.20 / 2;
    keys.forEach((key, i) => {
      const kind = key.startsWith("internal:") ? "internal" : "input";
      const net = key.slice(kind.length + 1);
      const k = l + ":" + net;
      trackX.set(k, base + i * 0.20);
      trackKind.set(k, kind);
    });
  }

  const lines: string[] = [];
  lines.push("\\documentclass[border=12pt]{standalone}");
  lines.push("\\usepackage[american]{circuitikz}");
  lines.push("\\begin{document}");
  lines.push("\\begin{circuitikz}[x=1cm,y=1cm]");
  cells.forEach((c, i) => {
    const p = pos[i];
    lines.push("\\node[" + shapes[i] + "] (g" + i + ") at (" + p.x + "," + p.y + ") {};");
    lines.push("\\node[above=2pt,anchor=south west,font=\\tiny,fill=white,inner sep=1pt] at (g" + i +
      ".north) {" + c.type.replace(/_/g, "\\_") + "};");
  });
  const inAnchor = (i: number, j: number) => {
    const s = shapes[i];
    return (s === "not port" || s === "buffer port") ? ".in" : ".in " + (j + 1);
  };

  // Primary-input bus: directly feeds level 0; each later level gets a short
  // feeder in its inter-band gap and a local channel track.
  const inputNets = vars.filter((v) => sinks.has(v));
  const inputBusX = new Map<string, number>();
  inputNets.forEach((net, i) => inputBusX.set(net, 0.35 + i * 0.45));
  inputNets.forEach((net, bi) => {
    const x = inputBusX.get(net)!;
    const bus = "in" + bi;
    lines.push("\\coordinate (" + bus + ") at (" + x.toFixed(2) + ",0);");
    const list = sinks.get(net) || [];
    const direct = list.filter((s) => level[s.cell] === 0);
    const feeds: number[] = [];
    for (let l = 1; l <= maxLevel; ++l)
      if ((levelInputs.get(l) || []).indexOf(net) >= 0) feeds.push(l);
    const ys: number[] = [];
    for (const s of direct) ys.push(pos[s.cell].y - 1.2);
    for (const l of feeds) ys.push(feedY.get(l + ":" + net)!);
    if (ys.length === 0) return;
    const lastY = Math.min(...ys);
    lines.push("\\draw (" + x.toFixed(2) + ",1.3) -- (" + x.toFixed(2) + "," +
      lastY.toFixed(2) + ");");
    lines.push("\\node[above=2pt,font=\\scriptsize,fill=white,inner sep=1pt] at (" +
      x.toFixed(2) + ",1.3) {" + texNet(net) + "};");
    for (const sk of direct) {
      const j = inputsOf(cells[sk.cell]).indexOf(sk.port);
      const pin = "g" + sk.cell + inAnchor(sk.cell, j);
      lines.push("\\draw (" + pin + ") -- (" + pin + " -| " + bus + ");");
      lines.push("\\fill (" + pin + " -| " + bus + ") circle (1.1pt);");
    }
    for (const l of feeds) {
      const fy = feedY.get(l + ":" + net)!;
      const tx = trackX.get(l + ":" + net)!;
      lines.push("\\draw (" + x.toFixed(2) + "," + fy.toFixed(2) + ") -- (" +
        tx.toFixed(2) + "," + fy.toFixed(2) + ");");
      lines.push("\\fill (" + x.toFixed(2) + "," + fy.toFixed(2) + ") circle (1.1pt);");
      lines.push("\\node[above=2pt,font=\\scriptsize,anchor=west,fill=white,inner sep=1pt] at (" +
        tx.toFixed(2) + "," + fy.toFixed(2) + ") {" + texNet(net) + "};");
    }
  });

  // Draw every channel track and its sink stubs.
  for (const [key, x] of trackX) {
    const l = Number(key.slice(0, key.indexOf(":")));
    const net = key.slice(key.indexOf(":") + 1);
    const kind = trackKind.get(key)!;
    const list = sinks.get(net) || [];
    const tname = "t" + l + "_" + net.replace(/[^A-Za-z0-9_]/g, "_");
    lines.push("\\coordinate (" + tname + ") at (" + x.toFixed(2) + ",0);");
    let source = "";
    if (kind === "internal") {
      const src = producer.get(net)!;
      const snet = "(g" + src + ".out -| " + tname + ")";
      lines.push("\\draw (g" + src + ".out) -- " + snet + ";");
      lines.push("\\fill " + snet + " circle (1.1pt);");
      if (outs.indexOf(net) < 0 || usedAsInput.has(net))
        lines.push("\\node[above=2pt,anchor=south west,xshift=4pt,font=\\scriptsize,fill=white,inner sep=1pt] at " +
          snet + " {" + texNet(net) + "};");
      source = snet;
    } else {
      source = "(" + x.toFixed(2) + "," + feedY.get(key)!.toFixed(2) + ")";
      lines.push("\\fill " + source + " circle (1.1pt);");
    }
    for (const sk of list) {
      const j = inputsOf(cells[sk.cell]).indexOf(sk.port);
      const pin = "g" + sk.cell + inAnchor(sk.cell, j);
      const dnet = "(" + pin + " -| " + tname + ")";
      lines.push("\\draw " + source + " -- " + dnet + ";");
      lines.push("\\draw " + dnet + " -- (" + pin + ");");
      lines.push("\\fill " + dnet + " circle (1.1pt);");
    }
  }

  cells.forEach((c, i) => {
    const op = outPort(c);
    const net = c.ports[op];
    if (outs.indexOf(net) >= 0 && !usedAsInput.has(net)) {
      lines.push("\\draw (g" + i + ".out) -- ++(0.95,0) node[above=2pt,right,fill=white,inner sep=1pt]{" +
        texNet(net) + "};");
    }
  });

  lines.push("\\end{circuitikz}");
  lines.push("\\end{document}");

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "dcstudy-gates-localbus-"));
  try {
    const tex = path.join(dir, "gate.tex");
    const pdf = path.join(dir, "gate.pdf");
    const svg = path.join(dir, "gate.svg");
    fs.writeFileSync(tex, lines.join("\n"), "utf8");
    const r = spawnSync("pdflatex",
      ["-interaction=nonstopmode", "-halt-on-error", "-output-directory", dir, tex],
      { cwd: dir, encoding: "utf8" });
    if (r.status !== 0) return renderGateLevelSvgBoxes(d, vars, title);
    const c = spawnSync("pdftocairo", ["-svg", pdf, svg], { cwd: dir, encoding: "utf8" });
    if (c.status !== 0 || !fs.existsSync(svg))
      return renderGateLevelSvgBoxes(d, vars, title);
    return fs.readFileSync(svg, "utf8");
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

// Global top-bus fallback for nets that skip a logic level.
function renderGateLevelSvgBus(d: Design, vars: string[], title: string): string {
  const cells = d.cells || [];
  if (cells.length === 0) throw new Error("multi-stage design has no cells");
  const outs = d.outputs && d.outputs.length ? d.outputs : ["Y"];
  const cellLabel = (type: string) => {
    const map: Record<string, string> = {
      "$or": "OR", "$and": "AND", "$not": "INV", "$mux": "MUX2",
      "$xor": "XOR", "$xnor": "XNOR", "$nand": "NAND", "$nor": "NOR",
      "$reduce_or": "OR-RED", "$reduce_and": "AND-RED",
    };
    return map[type] || type;
  };
  const outPort = (c: { type: string; ports: Record<string, string> }) => {
    for (const p of ["ZN", "Z", "Q", "Y", "CO", "S"]) if (c.ports[p]) return p;
    const keys = Object.keys(c.ports);
    return keys.length ? keys[keys.length - 1] : "";
  };
  const outPorts = (c: {
    type: string; ports: Record<string, string>;
    directions?: Record<string, string>;
  }) => {
    if (c.directions)
      return Object.keys(c.ports)
        .filter((p) => c.directions?.[p] === "output").sort();
    return [outPort(c)];
  };
  const inputsOf = (c: {
    type: string; ports: Record<string, string>;
    directions?: Record<string, string>;
  }) => {
    if (c.directions)
      return Object.keys(c.ports)
        .filter((p) => c.directions?.[p] === "input").sort();
    return Object.keys(c.ports).filter((p) => p !== outPort(c)).sort();
  };
  const shapes = cells.map((c) => tikzPort(c.type, inputsOf(c).length));
  if (shapes.some((s) => s === ""))
    return renderGateLevelSvgBoxes(d, vars, title);
  if (spawnSync("pdflatex", ["--version"]).status !== 0 ||
      spawnSync("pdftocairo", ["-v"]).status !== 0)
    return renderGateLevelSvgBoxes(d, vars, title);

  const producer = new Map<string, number>();
  cells.forEach((c, i) => {
    for (const p of outPorts(c)) if (c.ports[p]) producer.set(c.ports[p], i);
  });
  const level = new Array<number>(cells.length).fill(0);
  for (let iter = 0; iter < cells.length; iter++) {
    for (let i = 0; i < cells.length; i++) {
      for (const p of inputsOf(cells[i])) {
        const src = producer.get(cells[i].ports[p]);
        if (src !== undefined && src !== i)
          level[i] = Math.max(level[i], level[src] + 1);
      }
    }
  }
  const maxLevel = Math.max(...level, 0);
  cells.forEach((c, i) => {
    if (outs.indexOf(c.ports[outPort(c)]) >= 0) level[i] = maxLevel;
  });
  const outRank = (i: number) => {
    const net = cells[i].ports[outPort(cells[i])];
    const k = outs.indexOf(net);
    return k >= 0 ? k : 1_000_000;
  };
  const order = cells.map((_, i) => i).sort((a, b) => {
    if (level[a] !== level[b]) return level[a] - level[b];
    const ra = outRank(a), rb = outRank(b);
    if (ra !== rb) return ra - rb;
    return a - b;
  });
  const rows = new Map<number, number>();
  const pos = new Array<{ x: number; y: number }>(cells.length);
  for (const i of order) {
    const row = rows.get(level[i]) ?? 0;
    rows.set(level[i], row + 1);
    pos[i] = { x: 1.8 + level[i] * 4.0, y: -1.0 - row * 2.2 };
  }

  // Display names: primary I/O keep their names; inverter outputs become X';
  // other internal nets get stable n1, n2, ... names.
  const display = new Map<string, string>();
  for (const v of vars) display.set(v, v);
  for (const o of outs) display.set(o, o);
  const visiting = new Set<string>();
  let internal = 0;
  const netName = (net: string): string => {
    const known = display.get(net);
    if (known !== undefined) return known;
    if (visiting.has(net)) return net;
    visiting.add(net);
    const src = producer.get(net);
    let result = "";
    if (src !== undefined) {
      const c = cells[src];
      const ip = inputsOf(c);
      if ((c.type.startsWith("INV") || c.type.startsWith("NOT")) && ip.length === 1)
        result = netName(c.ports[ip[0]]) + "'";
      else
        result = "n" + (++internal);
    } else {
      result = net;
    }
    visiting.delete(net);
    display.set(net, result);
    return result;
  };
  for (const [net] of producer) netName(net);

  type Sink = { cell: number; port: string };
  const sinks = new Map<string, Sink[]>();
  for (let i = 0; i < cells.length; ++i) {
    for (const p of inputsOf(cells[i])) {
      const net = cells[i].ports[p];
      const list = sinks.get(net) || [];
      list.push({ cell: i, port: p });
      sinks.set(net, list);
    }
  }
  const busNets: string[] = [];
  for (const v of vars) if (sinks.has(v)) busNets.push(v);
  cells.forEach((c) => {
    const net = c.ports[outPort(c)];
    if (net && sinks.has(net) && busNets.indexOf(net) < 0) busNets.push(net);
  });
  const busY = new Map<string, number>();
  busNets.forEach((net, i) => busY.set(net, 1.4 + i * 0.36));

  const srcOff = new Map<number, number>();
  const colSrcCount = new Map<number, number>();
  const sinkOff = new Map<string, number>();
  const colSinkCount = new Map<number, number>();
  const offForSink = (net: string, lvl: number) => {
    const key = net + "@" + lvl;
    const old = sinkOff.get(key);
    if (old !== undefined) return old;
    const n = colSinkCount.get(lvl) || 0;
    colSinkCount.set(lvl, n + 1);
    const off = 0.55 + 0.18 * n;
    sinkOff.set(key, off);
    return off;
  };
  const offForSource = (cell: number, lvl: number) => {
    const old = srcOff.get(cell);
    if (old !== undefined) return old;
    const n = colSrcCount.get(lvl) || 0;
    colSrcCount.set(lvl, n + 1);
    const off = 0.55 + 0.18 * n;
    srcOff.set(cell, off);
    return off;
  };

  const lines: string[] = [];
  lines.push("\\documentclass[border=12pt]{standalone}");
  lines.push("\\usepackage[american]{circuitikz}");
  lines.push("\\begin{document}");
  lines.push("\\begin{circuitikz}[x=1cm,y=1cm]");
  cells.forEach((c, i) => {
    const p = pos[i];
    lines.push("\\node[" + shapes[i] + "] (g" + i + ") at (" + p.x + "," + p.y + ") {};");
    lines.push("\\node[above=1pt,font=\\tiny] at (g" + i +
      ".north) {" + c.type.replace(/_/g, "\\_") + "};");
  });

  const inAnchor = (i: number, j: number) => {
    const s = shapes[i];
    return (s === "not port" || s === "buffer port") ? ".in" : ".in " + (j + 1);
  };
  const texNet = (net: string) => texEscape(netName(net));

  busNets.forEach((net, bi) => {
    const y = busY.get(net)!;
    const bus = "bus" + bi;
    lines.push("\\coordinate (" + bus + ") at (0," + y.toFixed(3) + ");");
    const src = producer.get(net);
    let sourceTop: string;
    if (src === undefined) {
      // Primary input: track starts at the left edge.
      sourceTop = "s" + bi;
      lines.push("\\coordinate (" + sourceTop + ") at (0.25," + y.toFixed(3) + ");");
      lines.push("\\node[left=2pt] at (" + sourceTop + ") {" + texNet(net) + "};");
    } else {
      const off = offForSource(src, level[src]);
      sourceTop = "s" + bi;
      lines.push("\\draw (g" + src + ".out) -- ++(" + off.toFixed(2) +
        ",0) coordinate (" + sourceTop + ") -- (" + sourceTop + " |- " + bus + ");");
      lines.push("\\fill (" + sourceTop + " |- " + bus + ") circle (1.1pt);");
      lines.push("\\node[above=1pt,font=\\scriptsize] at (" + sourceTop + " |- " +
        bus + ") {" + texNet(net) + "};");
    }
    const list = sinks.get(net) || [];
    list.forEach((sk, si) => {
      const off = offForSink(net, level[sk.cell]);
      const d = "d" + bi + "_" + si;
      const j = inputsOf(cells[sk.cell]).indexOf(sk.port);
      lines.push("\\draw (g" + sk.cell + inAnchor(sk.cell, j) + ") -- ++(-" +
        off.toFixed(2) + ",0) coordinate (" + d + ") -- (" + d + " |- " + bus + ");");
      lines.push("\\draw (" + sourceTop + " |- " + bus + ") -- (" + d + " |- " +
        bus + ");");
      lines.push("\\fill (" + d + " |- " + bus + ") circle (1.1pt);");
    });
  });

  // Primary outputs leave from the rightmost column.  They are not part of the
  // distribution bus unless they also feed an internal gate.
  cells.forEach((c, i) => {
    const op = outPort(c);
    const net = c.ports[op];
    if (outs.indexOf(net) >= 0) {
      lines.push("\\draw (g" + i + ".out) -- ++(0.95,0) node[right]{" +
        texNet(net) + "};");
    }
  });

  lines.push("\\end{circuitikz}");
  lines.push("\\end{document}");

  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "dcstudy-gates-bus-"));
  try {
    const tex = path.join(dir, "gate.tex");
    const pdf = path.join(dir, "gate.pdf");
    const svg = path.join(dir, "gate.svg");
    fs.writeFileSync(tex, lines.join("\n"), "utf8");
    const r = spawnSync("pdflatex",
      ["-interaction=nonstopmode", "-halt-on-error", "-output-directory", dir, tex],
      { cwd: dir, encoding: "utf8" });
    if (r.status !== 0) return renderGateLevelSvgBoxes(d, vars, title);
    const c = spawnSync("pdftocairo", ["-svg", pdf, svg], { cwd: dir, encoding: "utf8" });
    if (c.status !== 0 || !fs.existsSync(svg))
      return renderGateLevelSvgBoxes(d, vars, title);
    return fs.readFileSync(svg, "utf8");
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

function renderGateLevelSvgBoxes(d: Design, vars: string[], title: string): string {
  const cells = d.cells || [];
  if (cells.length === 0) throw new Error("multi-stage design has no cells");
  const outs = d.outputs && d.outputs.length ? d.outputs : ["Y"];
  const cellLabel = (type: string) => {
    const map: Record<string, string> = {
      "$or": "OR", "$and": "AND", "$not": "INV", "$mux": "MUX2",
      "$xor": "XOR", "$xnor": "XNOR", "$nand": "NAND", "$nor": "NOR",
      "$reduce_or": "OR-RED", "$reduce_and": "AND-RED",
    };
    return map[type] || type;
  };
  const outPort = (c: { type: string; ports: Record<string, string> }) => {
    for (const p of ["ZN", "Z", "Q", "Y", "CO", "S"]) if (c.ports[p]) return p;
    const keys = Object.keys(c.ports);
    return keys.length ? keys[keys.length - 1] : "";
  };
  const outPorts = (c: {
    type: string; ports: Record<string, string>;
    directions?: Record<string, string>;
  }) => {
    if (c.directions)
      return Object.keys(c.ports)
        .filter((p) => c.directions?.[p] === "output").sort();
    return [outPort(c)];
  };
  const inputsOf = (c: {
    type: string; ports: Record<string, string>;
    directions?: Record<string, string>;
  }) => {
    if (c.directions)
      return Object.keys(c.ports)
        .filter((p) => c.directions?.[p] === "input").sort();
    return Object.keys(c.ports).filter((p) => p !== outPort(c)).sort();
  };

  const producer = new Map<string, number>();
  cells.forEach((c, i) => {
    for (const p of outPorts(c)) if (c.ports[p]) producer.set(c.ports[p], i);
  });
  const level = new Array<number>(cells.length).fill(0);
  for (let iter = 0; iter < cells.length; iter++) {
    for (let i = 0; i < cells.length; i++) {
      for (const p of inputsOf(cells[i])) {
        const src = producer.get(cells[i].ports[p]);
        if (src !== undefined && src !== i)
          level[i] = Math.max(level[i], level[src] + 1);
      }
    }
  }

  const boxW = 150;
  const cellH = cells.map((c) => Math.max(56,
    18 + Math.max(inputsOf(c).length, outPorts(c).length) * 15));
  const gapX = 80;
  const gapY = 46;
  const cols = Math.max(3, Math.ceil(Math.sqrt(cells.length)));
  const order = cells.map((_, i) => i).sort((a, b) =>
    level[a] !== level[b] ? level[a] - level[b] : a - b);
  const rows = Math.ceil(cells.length / cols);
  const rowH: number[] = new Array(rows).fill(56);
  order.forEach((cell, k) => {
    const r = Math.floor(k / cols);
    rowH[r] = Math.max(rowH[r], cellH[cell]);
  });
  const rowY: number[] = [];
  let cy = 120;
  for (let r = 0; r < rows; r++) {
    rowY[r] = cy;
    cy += rowH[r] + gapY;
  }
  const pos = new Map<number, { x: number; y: number }>();
  order.forEach((cell, k) => {
    const r = Math.floor(k / cols);
    pos.set(cell, { x: 70 + (k % cols) * (boxW + gapX), y: rowY[r] });
  });
  const width = Math.max(760, 70 + cols * (boxW + gapX) + 50);
  const height = Math.max(360, cy + 40);

  const netSource = new Map<string,
    { kind: "cell" | "input"; index: number; port?: string }>();
  vars.forEach((v, i) => netSource.set(v, { kind: "input", index: i }));
  cells.forEach((c, i) => {
    for (const p of outPorts(c))
      if (c.ports[p]) netSource.set(c.ports[p], { kind: "cell", index: i, port: p });
  });
  const netSinks = new Map<string, Array<{ cell: number; pin: string }>>();
  cells.forEach((c, i) => {
    inputsOf(c).forEach((p) => {
      const list = netSinks.get(c.ports[p]) || [];
      list.push({ cell: i, pin: p });
      netSinks.set(c.ports[p], list);
    });
  });

  const displayName = new Map<string, string>();
  let anonNet = 0;
  const disp = (net: string) => {
    const hit = displayName.get(net);
    if (hit) return hit;
    if (vars.indexOf(net) >= 0 || outs.indexOf(net) >= 0) {
      displayName.set(net, net);
      return net;
    }
    const m = /new_(n\d+)$/.exec(net);
    let name = m ? m[1] : "n" + (++anonNet);
    while ([...displayName.values()].indexOf(name) >= 0)
      name = "n" + (++anonNet);
    displayName.set(net, name);
    return name;
  };
  const pinY = (cell: number, pin: string, output: boolean) => {
    const list = output ? outPorts(cells[cell]) : inputsOf(cells[cell]);
    const j = Math.max(0, list.indexOf(pin));
    return pos.get(cell)!.y + 10 +
      (cellH[cell] - 20) * (j + 1) / (list.length + 1);
  };

  const parts: string[] = [];
  parts.push('<svg viewBox="0 0 ' + width + " " + height + '" xmlns="http://www.w3.org/2000/svg">');
  parts.push('<rect width="' + width + '" height="' + height + '" fill="#fff"/>');
  parts.push("<style>");
  parts.push(style());
  parts.push("  .box{fill:#f7f8fa;stroke:#1c1c1e;stroke-width:2}");
  parts.push("  text.lbl,text.small{stroke:#fff;stroke-width:3;paint-order:stroke;stroke-linejoin:round}");
  parts.push("  .pintxt{font-family:-apple-system,Segoe UI,Noto Sans CJK SC,sans-serif;font-size:10px;fill:#505056}");
  parts.push("  .pinline{stroke:#8a8a90;stroke-width:1.2}");
  parts.push("  .netlbl{font-family:-apple-system,Segoe UI,Noto Sans CJK SC,sans-serif;font-size:9.5px;fill:#3a3a40;stroke:#fff;stroke-width:2.5;paint-order:stroke}");
  parts.push("</style>");
  parts.push(text(40, 38,
    title + (d.cost > 0 ? " · " + d.cost + "T" : ""), "title"));
  parts.push(text(40, 62, "multi-stage static CMOS · gate-level netlist"));
  parts.push(text(40, 84,
    "all connections use matching pin labels · inputs: " + vars.join(" ") +
      " · outputs: " + outs.join(" "), "netlbl"));

  // Every net is a label: source pin on the driving cell, sink label on every
  // load.  No wire is routed, so cell placement cannot change connectivity.
  const seenSource = new Set<string>();
  for (const [net, src] of netSource) {
    if (src.kind !== "cell") continue;
    if (!netSinks.has(net) && outs.indexOf(net) < 0) continue;
    if (seenSource.has(net)) continue;
    seenSource.add(net);
    const p = pos.get(src.index)!;
    const port = src.port || outPorts(cells[src.index])[0];
    const oy = pinY(src.index, port, true);
    parts.push(line(p.x + boxW, oy, p.x + boxW + 18, oy, "pinline"));
    parts.push(text(p.x + boxW + 20, oy - 5, disp(net), "netlbl"));
  }
  for (const [net, sinks] of netSinks) {
    for (const k of sinks) {
      const p = pos.get(k.cell)!;
      const sy = pinY(k.cell, k.pin, false);
      parts.push(line(p.x - 18, sy, p.x, sy, "pinline"));
      parts.push(text(p.x - 22, sy + 3, disp(net), "netlbl", "end"));
    }
  }

  cells.forEach((c, i) => {
    const p = pos.get(i)!;
    parts.push('<rect x="' + p.x + '" y="' + p.y + '" width="' + boxW +
      '" height="' + cellH[i] + '" rx="7" class="box"/>');
    parts.push(text(p.x + boxW / 2, p.y + 22, cellLabel(c.type), "small", "middle"));
    const ins = inputsOf(c);
    ins.forEach((pin, j) => {
      const sy = p.y + 10 + (cellH[i] - 20) * (j + 1) / (ins.length + 1);
      parts.push(line(p.x, sy, p.x + 10, sy, "pinline"));
      parts.push(text(p.x + 14, sy + 4, pin, "pintxt"));
    });
    for (const op of outPorts(c)) {
      const oy = pinY(i, op, true);
      parts.push(line(p.x + boxW - 10, oy, p.x + boxW, oy, "pinline"));
      parts.push(text(p.x + boxW - 14, oy + 4, op, "pintxt", "end"));
    }
  });
  parts.push(text(55, height - 28,
    "gate-level view · every net is identified by matching labels; no wires are routed"));
  parts.push("</svg>");
  return parts.join("\n");
}

// Fold parallel N/P pairs that share a source into a single TG for display.
// The solver uses the atomic N/P representation (canonical); this is a view
// transformation only and does not change cost or behavior.
function foldTgBranches(d: Design): Design {
  const used = new Array<boolean>(d.branches.length).fill(false);
  const out: Branch[] = [];
  const comp = (lit: string) => lit.endsWith("b") ? lit.slice(0, -1) : lit + "b";
  for (let i = 0; i < d.branches.length; i++) {
    if (used[i]) continue;
    const a = d.branches[i];
    let foldedLit = "";
    if (a.chain.length === 1 && (a.chain[0].kind === "N" || a.chain[0].kind === "P")) {
      for (let j = i + 1; j < d.branches.length; j++) {
        if (used[j]) continue;
        const b = d.branches[j];
        if (b.source !== a.source || b.chain.length !== 1) continue;
        if (!((a.chain[0].kind === "N" && b.chain[0].kind === "P") ||
              (a.chain[0].kind === "P" && b.chain[0].kind === "N"))) continue;
        const an = a.chain[0], bn = b.chain[0];
        if (comp(an.lit) === bn.lit) foldedLit = bn.lit;
        else if (comp(bn.lit) === an.lit) foldedLit = an.lit;
        else continue;
        used[j] = true;
        break;
      }
    }
    if (foldedLit) {
      out.push({ source: a.source, cost: 2,
        chain: [{ kind: "TGL", lit: foldedLit, cost: 2 }] });
    } else {
      out.push(a);
    }
  }
  return { ...d, branches: out };
}

function renderDesignSvg(d: Design, vars: string[], title: string): { svg: string; inverters: string[] } {
  if (d.kind === "static-cmos")
    return { svg: renderStaticCmosSvg(d, title), inverters: [] };
  if (d.kind === "gate-boxes")
    return { svg: renderGateLevelSvgBoxes(d, vars, title), inverters: [] };
  if (d.kind === "static-cmos-multi") {
    // Standard net-label schematic: placement never implies connectivity.
    return { svg: renderGateLevelSvgBoxes(d, vars, title), inverters: [] };
  }
  if (d.branches.length === 0) throw new Error("design has no branches");
  d = foldTgBranches(d);
  const inverters = Array.from(new Set(d.inverters.map(String)));

  const groups = new Map<string, Branch[]>();
  for (const b of d.branches) {
    const list = groups.get(b.source) ?? [];
    list.push(b);
    groups.set(b.source, list);
  }

  const maxChain = Math.max(...d.branches.map((b) => b.chain.length), 0);
  const railX = 250;
  const deviceX = railX + 90;
  const step = 170;
  const sw = 110;
  const ybus = deviceX + maxChain * step + 80;
  const width = Math.max(980, ybus + 190);

  const parts: string[] = [];
  const svgTitle = title + " · " + d.cost + "T";
  parts.push('<svg viewBox="0 0 ' + width + ' 800" xmlns="http://www.w3.org/2000/svg">');
  parts.push('<rect width="' + width + '" height="800" fill="#fff"/>');
  parts.push("<style>");
  parts.push(style());
  parts.push("</style>");
  parts.push(text(40, 38, svgTitle, "title"));
  const sub = d.cost + "T · " + d.branches.length + " 条支路" +
    (inverters.length ? " · INV(" + inverters.join(",") + ")" : "");
  parts.push(text(40, 62, sub));

  type Row = { branch: Branch; cy: number };
  const rows: Row[] = [];
  const rails: Array<{ source: string; top: number; bottom: number }> = [];
  let y = 100;

  for (const [src, bs] of groups) {
    const inv = isInvertedSource(src, inverters);
    let railTop: number;
    if (inv) {
      const cy = y + 78;
      railTop = cy;
      const input = src.slice(0, -1);
      parts.push(...drawInverter(cy, input, railX));
      y = cy + 50;
    } else {
      railTop = y + 30;
      y = railTop + 40;
    }

    let rowStart = y;
    for (const b of bs) {
      const h = hasTg(b) ? 180 : 130;
      const cy = rowStart + Math.floor(h / 2);
      rows.push({ branch: b, cy });
      rowStart += h + 10;
    }
    const bottom = rows[rows.length - 1].cy + 30;
    rails.push({ source: src, top: railTop, bottom });

    parts.push(line(railX, railTop, railX, bottom, "w"));
    parts.push(text(railX - 12, railTop - 8, sourceDisp(src), "lbl", "end"));
    // source rail taps + branches
    let rowIndex = rows.length - bs.length;
    for (const b of bs) {
      const cy = rows[rowIndex].cy;
      rowIndex++;
      parts.push(circle(railX, cy));
      parts.push(line(railX, cy, deviceX, cy, "w"));
      const chain = b.chain;
      if (chain.length === 0) {
        parts.push(line(deviceX, cy, ybus, cy, "w"));
      }
      for (let j = 0; j < chain.length; j++) {
        const x1 = deviceX + j * step;
        const x2 = x1 + sw;
        const el = chain[j];
        if (el.kind === "N" || el.kind === "P") {
          parts.push(...drawNp(x1, cy, sw, el.kind, litDisp(el.lit)));
        } else if (el.kind === "TGL" || el.kind === "TGH") {
          parts.push(...drawTg(x1, cy, sw, el.kind, el.lit));
        } else {
          throw new Error("unknown switch kind: " + el.kind);
        }
        const next = j + 1 < chain.length ? deviceX + (j + 1) * step : ybus;
        parts.push(line(x2, cy, next, cy, "w"));
      }
      parts.push(circle(ybus, cy));
    }
    y = bottom + 40;
  }

  const yTop = Math.min(...rails.map((r) => r.top)) - 20;
  const yBottom = Math.max(...rails.map((r) => r.bottom)) + 20;
  const yMid = Math.floor((rows[0].cy + rows[rows.length - 1].cy) / 2);
  parts.push(line(ybus, yTop, ybus, yBottom, "w"));
  parts.push(line(ybus, yMid, ybus + 55, yMid, "w"));
  parts.push(text(ybus + 68, yMid + 6, "Y", "lbl"));

  const legendY = y + 10;
  parts.push(text(55, legendY, "● 实心点 = 连接"));
  parts.push(text(55, legendY + 22, "带小圆圈 = PMOS　·　无圆圈 = NMOS　·　TG = 传输门"));
  const height = Math.max(320, legendY + 50);
  // Replace the provisional height with the real one.
  parts[0] = '<svg viewBox="0 0 ' + width + " " + height + '" xmlns="http://www.w3.org/2000/svg">';
  parts[1] = '<rect width="' + width + '" height="' + height + '" fill="#fff"/>';
  parts.push("</svg>");
  return { svg: parts.join("\n"), inverters };
}

function renderInverterSvg(input: string, output: string): string {
  const parts: string[] = [];
  parts.push('<svg viewBox="0 0 560 340" xmlns="http://www.w3.org/2000/svg">');
  parts.push('<rect width="560" height="340" fill="#fff"/>');
  parts.push("<style>");
  parts.push(style());
  parts.push("</style>");
  parts.push(text(30, 32, "2T CMOS 反相器 · " + input + " → " + output, "title"));
  parts.push(text(30, 54, input + " 同时驱动 PMOS 与 NMOS 的栅极"));
  const x = 240;
  const gateX = 150;
  parts.push(line(x, 80, x, 120, "w"));
  parts.push(line(x - 25, 80, x + 25, 80, "plate"));
  parts.push(text(x, 70, "VDD", "small", "middle"));
  parts.push(line(x, 120, x, 160, "ch"));
  parts.push(line(x - 14, 132, x - 14, 148, "plate"));
  parts.push(circle(x - 21, 140, 6, "bub"));
  parts.push(line(x - 27, 140, gateX, 140, "w"));
  parts.push(text(x + 20, 142, "P", "small"));
  parts.push(line(x, 160, x, 200, "w"));
  parts.push(circle(x, 180));
  parts.push(line(x, 180, 350, 180, "w"));
  parts.push(text(362, 186, output, "lbl"));
  parts.push(line(x, 200, x, 240, "ch"));
  parts.push(line(x - 14, 212, x - 14, 228, "plate"));
  parts.push(line(x - 14, 220, gateX, 220, "w"));
  parts.push(text(x + 20, 222, "N", "small"));
  parts.push(line(gateX, 140, gateX, 220, "w"));
  parts.push(line(90, 180, gateX, 180, "w"));
  parts.push(circle(gateX, 180));
  parts.push(text(78, 186, input, "lbl", "end"));
  parts.push(line(x, 240, x, 270, "w"));
  parts.push(line(x - 25, 270, x + 25, 270, "plate"));
  parts.push(line(x - 15, 278, x + 15, 278, "plate"));
  parts.push(line(x - 5, 286, x + 5, 286, "plate"));
  parts.push(text(x + 38, 272, "GND", "small"));
  parts.push("</svg>");
  return parts.join("\n");
}

type LogicInfo = {
  svg: string; tex: string | null; label: string; kind: string;
  generic: boolean;
};

function gateKind(vars: string[], truth: string): string {
  if (vars.length === 1) return truth === "10" ? "not" : "";
  if (vars.length === 2 && truth.length === 4) {
    if (truth === "0001") return "and";
    if (truth === "0111") return "or";
    if (truth === "1110") return "nand";
    if (truth === "1000") return "nor";
    if (truth === "0110") return "xor";
    if (truth === "1001") return "xnor";
  }
  if (vars.length === 3 && truth.length === 8) {
    if (truth === "00000001") return "and";
    if (truth === "01111111") return "or";
    if (truth === "11111110") return "nand";
    if (truth === "10000000") return "nor";
    if (truth === "01101001") return "xor";
    if (truth === "10010110") return "xnor";
  }
  return "";
}

function gateLabel(kind: string): string {
  if (!kind) return "逻辑块";
  return kind.toUpperCase();
}

function texEscape(value: string): string {
  return value.replace(/\\/g, "\\textbackslash{}")
    .replace(/([#$%&_{}])/g, "\\$1")
    .replace(/~/g, "\\textasciitilde{}")
    .replace(/\^/g, "\\textasciicircum{}");
}

function renderLogicBlockSvg(vars: string[], label: string, reason: string): string {
  const width = 520;
  const height = 250;
  const rows = Math.max(1, vars.length);
  const top = 60;
  const step = 34;
  const boxX = 180;
  const boxY = 75;
  const boxW = 190;
  const boxH = Math.max(90, rows * step + 30);
  const parts: string[] = [];
  parts.push('<svg viewBox="0 0 ' + width + ' ' + height + '" xmlns="http://www.w3.org/2000/svg">');
  parts.push('<rect width="' + width + '" height="' + height + '" fill="#fff"/>');
  parts.push("<style>");
  parts.push(style());
  parts.push("</style>");
  parts.push(text(24, 30, "逻辑抽象 · " + label, "title"));
  parts.push('<rect x="' + boxX + '" y="' + boxY + '" width="' + boxW +
    '" height="' + boxH + '" rx="8" fill="#f7f8fa" stroke="#1c1c1e" stroke-width="2"/>');
  parts.push(text(boxX + boxW / 2, boxY + boxH / 2 + 6, "f", "lbl", "middle"));
  vars.forEach((v, i) => {
    const y = boxY + 24 + i * step;
    parts.push(line(boxX - 54, y, boxX, y, "w"));
    parts.push(text(boxX - 64, y + 6, v, "lbl", "end"));
  });
  const yOut = boxY + boxH / 2;
  parts.push(line(boxX + boxW, yOut, boxX + boxW + 60, yOut, "w"));
  parts.push(text(boxX + boxW + 72, yOut + 6, "Y", "lbl"));
  parts.push(text(24, height - 22, "行为等价，不是结构分解 · " + reason));
  parts.push("</svg>");
  return parts.join("\n");
}

function renderLogicPanel(req: Request, out: string): LogicInfo | null {
  if (req.vars.length < 1 || req.vars.length > 3) {
    const svg = renderLogicBlockSvg(req.vars, "逻辑块", "多输入函数以逻辑块表示");
    fs.writeFileSync(path.join(out, "logic.svg"), svg, "utf8");
    return { svg: "logic.svg", tex: null, label: "逻辑块", kind: "", generic: true };
  }
  const kind = gateKind(req.vars, req.truth);
  const label = gateLabel(kind);
  const haveLatex = spawnSync("pdflatex", ["--version"]).status === 0 &&
    spawnSync("pdftocairo", ["-v"]).status === 0;
  const cacheDir = path.join(process.env.DCSTUDY_CACHE_DIR || os.tmpdir(),
    "dcstudy-logic-cache");
  const cacheKey = (kind || "block") + "_" +
    req.vars.join("_").replace(/[^A-Za-z0-9_]/g, "");
  const cachedSvg = path.join(cacheDir, cacheKey + ".svg");
  if (kind && haveLatex && fs.existsSync(cachedSvg)) {
    fs.copyFileSync(cachedSvg, path.join(out, "logic.svg"));
    const cachedTex = path.join(cacheDir, cacheKey + ".tex");
    if (fs.existsSync(cachedTex)) fs.copyFileSync(cachedTex, path.join(out, "logic.tex"));
    const cachedPdf = path.join(cacheDir, cacheKey + ".pdf");
    if (fs.existsSync(cachedPdf)) fs.copyFileSync(cachedPdf, path.join(out, "logic.pdf"));
    return { svg: "logic.svg",
      tex: fs.existsSync(cachedTex) ? "logic.tex" : null,
      label, kind, generic: false };
  }
  if (kind && haveLatex) {
    const texName = "logic.tex";
    const port = kind === "not" ? "not port" : kind + " port";
    const inputsSpec = kind === "not" ? "" : ", number inputs=" + req.vars.length;
    let tex = "\\documentclass[border=6pt]{standalone}\n" +
      "\\usepackage[american]{circuitikz}\n" +
      "\\begin{document}\n\\begin{circuitikz}\n" +
      "\\node[" + port + inputsSpec + "] (G) at (0,0) {};\n";
    if (kind === "not") {
      tex += "\\draw (G.in) -- ++(-0.7,0) node[left]{" + texEscape(req.vars[0]) + "};\n";
    } else {
      for (let i = 0; i < req.vars.length; i++)
        tex += "\\draw (G.in " + (i + 1) + ") -- ++(-0.7,0) node[left]{" +
          texEscape(req.vars[i]) + "};\n";
    }
    tex += "\\draw (G.out) -- ++(0.7,0) node[right]{Y};\n" +
      "\\end{circuitikz}\n\\end{document}\n";
    fs.writeFileSync(path.join(out, texName), tex, "utf8");
    const r = spawnSync("pdflatex",
      ["-interaction=nonstopmode", "-halt-on-error", "-output-directory", out, "logic.tex"],
      { cwd: out, encoding: "utf8" });
    if (r.status === 0) {
      const c = spawnSync("pdftocairo",
        ["-svg", path.join(out, "logic.pdf"), path.join(out, "logic.svg")],
        { cwd: out, encoding: "utf8" });
      if (c.status === 0 && fs.existsSync(path.join(out, "logic.svg"))) {
        for (const junk of ["logic.aux", "logic.log"]) {
          try { fs.unlinkSync(path.join(out, junk)); } catch { /* ignore */ }
        }
        fs.mkdirSync(cacheDir, { recursive: true });
        for (const ext of [".svg", ".tex", ".pdf"]) {
          const src = path.join(out, "logic" + ext);
          if (fs.existsSync(src)) fs.copyFileSync(src, path.join(cacheDir, cacheKey + ext));
        }
        return { svg: "logic.svg", tex: texName, label: label, kind, generic: false };
      }
    }
  }
  const reason = kind ? "LaTeX/circuitikz 不可用，使用逻辑块" : "未识别为标准门";
  const svg = renderLogicBlockSvg(req.vars, label, reason);
  fs.writeFileSync(path.join(out, "logic.svg"), svg, "utf8");
  return { svg: "logic.svg", tex: null, label: label, kind, generic: true };
}

function truthTable(vars: string[], truth: string): string {
  const n = vars.length;
  if (truth.length !== (1 << n)) throw new Error("truth table length does not match vars");
  let head = vars.map((v) => "<th>" + esc(v) + "</th>").join("") + "<th>Y</th>";
  const rows: string[] = [];
  for (let m = 0; m < (1 << n); m++) {
    let cells = "";
    for (let i = 0; i < n; i++) cells += "<td>" + ((m >> (n - 1 - i)) & 1) + "</td>";
    rows.push("<tr>" + cells + "<td>" + truth[m] + "</td></tr>");
  }
  return "<table><thead><tr>" + head + "</tr></thead><tbody>" + rows.join("") + "</tbody></table>";
}

function truthTableMulti(vars: string[], outputs: string[],
                         truths: Record<string, string>): string {
  const n = vars.length;
  const len = 1 << n;
  for (const o of outputs) {
    if ((truths[o] || "").length !== len)
      throw new Error("truth table length does not match vars for " + o);
  }
  let head = vars.map((v) => "<th>" + esc(v) + "</th>").join("") +
    outputs.map((o) => "<th>" + esc(o) + "</th>").join("");
  const rows: string[] = [];
  for (let m = 0; m < len; m++) {
    let cells = "";
    for (let i = 0; i < n; i++) cells += "<td>" + ((m >> (n - 1 - i)) & 1) + "</td>";
    for (const o of outputs) cells += "<td>" + (truths[o] || "")[m] + "</td>";
    rows.push("<tr>" + cells + "</tr>");
  }
  return "<table><thead><tr>" + head + "</tr></thead><tbody>" + rows.join("") + "</tbody></table>";
}

function renderIndexHtml(req: Request, svgNames: string[], inverterNames: Array<{ varName: string; svg: string }>, logic: LogicInfo | null): string {
  const title = req.title || "design";
  const optimum = req.designs.length ? req.designs[0].cost : 0;
  const cards: string[] = [];
  req.designs.forEach((d0, i) => {
    const d = d0.kind ? d0 : foldTgBranches(d0);
    const isStatic = d.kind === "static-cmos" || d.kind === "static-cmos-multi";
    const lines = d.kind === "static-cmos-multi"
      ? ["cells: " + (d.topology || "")]
      : (d.kind === "static-cmos"
        ? ["PDN: " + (d.pull_down || ""), "PUN: " + (d.pull_up || "")]
        : d.branches.map(branchText));
    const branches = lines.map((line) => "<li><code>" + esc(line) + "</code></li>").join("");
    const invs = d.inverters.length ? d.inverters.join(", ") : "none";
    const cap = isStatic ? (d.topology || lines.join(" · ")) : d.branches.map(branchText).join(" · ");
    cards.push(
      "<h2><span class=\"num\">" + String.fromCharCode(65 + i) + "</span>方案 " + (i + 1) + " · " + d.cost + "T</h2>" +
      "<div class=\"fig\"><img src=\"" + esc(svgNames[i]) + "\" alt=\"schematic " + i + "\">" +
      "<div class=\"cap\">图 " + String.fromCharCode(65 + i) + "　" + esc(cap) + "</div></div>" +
      (d.kind === "static-cmos-multi" && d.outputs && d.outputs.length > 1
        ? "<p class=\"small\">多输出门级图：两级网络画成输入/反相轨 + 乘积项阵列，多级网络画成局部通道总线；实心点 = 连接，交叉无点 = 不连接。</p>"
        : "") +
      (isStatic ? "" : "<p class=\"small\">反相器：" + esc(invs) + "</p>") +
      "<ul>" + branches + "</ul>");
  });
  let invHtml = "";
  if (inverterNames.length) {
    invHtml = "<h2><span class=\"num\">C</span>2T 反相器细节</h2>";
    for (const item of inverterNames) {
      invHtml += "<div class=\"fig\"><img src=\"" + esc(item.svg) + "\" alt=\"inverter " + esc(item.varName) + "\"></div>";
    }
  }
  const isStatic = req.designs.some((d) => d.kind === "static-cmos" || d.kind === "static-cmos-multi");
  const isMulti = req.designs.some((d) => d.kind === "static-cmos-multi");
  const multiOutputs = req.outputs && req.outputs.length
    ? req.outputs
    : (isMulti && req.designs[0].outputs ? req.designs[0].outputs : []);
  const multiTruths = req.truths || (req.designs[0] && req.designs[0].truths) || null;
  const canonicalText = isMulti
    ? "多级静态互补 CMOS：每个标准单元都是静态 CMOS"
    : (isStatic
      ? "静态互补 CMOS：PUN 是 PDN 的 De Morgan 对偶"
      : (req.canonical ? "方案按保 f 的输入置换归一化" : "方案为原始最优解，未做对称归一化"));
  return "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n<meta charset=\"utf-8\">\n<title>" +
    esc(title) + " · 电路渲染</title>\n<style>\n" +
    ":root{--ink:#1c1c1e;--mute:#6b6b70;--line:#e2e2e6;--blue:#1c6dd0;--green:#1a7f45}\n" +
    "*{box-sizing:border-box}\n" +
    "body{margin:0;padding:0 0 70px;background:#fff;color:var(--ink);font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",\"Noto Sans CJK SC\",\"PingFang SC\",sans-serif;font-size:16.5px;line-height:1.75}\n" +
    ".wrap{max-width:1040px;margin:0 auto;padding:0 28px}\n" +
    "header{background:linear-gradient(135deg,#0f2540 0%,#1c4a75 55%,#2b6a9e 100%);color:#fff;padding:46px 28px 38px;margin-bottom:34px}\n" +
    "header h1{margin:0 0 10px;font-size:2em}\n" +
    "header p{margin:4px 0;color:#cfe0f0}\n" +
    "header code{background:rgba(0,0,0,.28);color:#fff;padding:1.5px 6px;border-radius:5px}\n" +
    "h2{font-size:1.36em;margin:50px 0 16px;padding-bottom:9px;border-bottom:3px solid var(--ink)}\n" +
    "h2 .num{display:inline-block;background:var(--ink);color:#fff;border-radius:7px;padding:1px 10px;margin-right:10px;font-size:.78em;vertical-align:3px}\n" +
    ".fig{margin:24px 0;text-align:center}\n" +
    ".fig img{width:100%;max-width:100%;height:auto;border:1px solid var(--line);border-radius:12px;background:#fff}\n" +
    ".cap{font-size:.88em;color:var(--mute);margin-top:9px}\n" +
    "table{border-collapse:collapse;width:100%;margin:18px 0;font-size:.94em}\n" +
    "th,td{border:1px solid var(--line);padding:8px 12px;text-align:center}\n" +
    "th{background:#f5f6f8}\n" +
    "tbody tr:nth-child(even){background:#fafbfc}\n" +
    "code{background:#f0f1f3;padding:1.5px 6px;border-radius:5px;font-family:Menlo,Consolas,monospace;font-size:.9em}\n" +
    "ul{margin:8px 0 0;padding-left:22px}\n" +
    "li{margin:4px 0}\n" +
    ".small{font-size:.9em;color:var(--mute)}\n" +
    ".box{border-left:5px solid var(--green);background:#edf9f1;padding:14px 18px;border-radius:0 10px 10px 0;margin:22px 0}\n" +
    ".box p:first-child{margin-top:0}.box p:last-child{margin-bottom:0}\n" +
    ".grid{display:grid;grid-template-columns:1.1fr .9fr;gap:18px;align-items:start}\n" +
    ".logic{text-align:center}.logic img{display:block;width:100%;max-width:360px;height:auto;margin:8px auto;background:#fff}\n" +
    "@media(max-width:760px){.grid{grid-template-columns:1fr}}\n" +
    "</style>\n</head>\n<body>\n<header><div class=\"wrap\">\n<h1>" + esc(title) + " · 电路渲染</h1>\n" +
    "<p><code>dcstudy</code> 自动生成 · " +
    (multiOutputs.length
      ? "outputs = " + esc(multiOutputs.join(", ")) + " · multi-output"
      : req.truth.length ? "truth = " + esc(req.truth) : "gate-level netlist") +
    " · optimum = " + optimum + "T</p>\n" +
    "<p>" + esc(canonicalText) + "；晶体管符号与讲义一致。</p>\n</div></header>\n<div class=\"wrap\">\n" +
    "<div class=\"grid\"><div class=\"box\"><p><strong>真值表</strong>（输入顺序 " + esc(req.vars.join(", ")) + "）</p>" +
    (multiOutputs.length && multiTruths
      ? truthTableMulti(req.vars, multiOutputs, multiTruths)
      : req.truth.length ? truthTable(req.vars, req.truth)
                         : "<p>来自映射网表的门级视图</p>") + "</div>" +
    (logic ? "<div class=\"box logic\"><p><strong>逻辑抽象</strong> · " + esc(logic.label) + "</p>" +
      "<img src=\"" + esc(logic.svg) + "\" alt=\"logic abstraction\">" +
      "<p class=\"small\">行为等价，不是结构分解。" +
      (logic.generic ? "未识别为标准门，使用逻辑块表示。" : "标准门符号由 circuitikz 生成。") +
      "</p></div>" : "") + "</div>\n" + cards.join("\n") + "\n" + invHtml +
    "\n<div class=\"box\"><p><strong>图例：</strong>带小圆圈的栅极 = PMOS，无圆圈 = NMOS；实心点 = 电气连接；TG = 传输门（PMOS || NMOS，两相控制）。</p></div>\n" +
    "</div>\n</body>\n</html>\n";
}

function main(): number {
  try {
    const req = JSON.parse(fs.readFileSync(0, "utf8")) as Request;
    if (req.op !== "render") throw new Error("unsupported op");
    const out = path.resolve(req.out);
    fs.mkdirSync(out, { recursive: true });

    const manifestPath = path.join(out, "manifest.json");
    if (fs.existsSync(manifestPath)) {
      try {
        const old = JSON.parse(fs.readFileSync(manifestPath, "utf8")) as { files?: string[] };
        const outAbs = path.resolve(out);
        for (const f of old.files || []) {
          const p = path.resolve(f);
          if (p === outAbs || p.startsWith(outAbs + path.sep)) {
            try { fs.unlinkSync(p); } catch { /* ignore */ }
          }
        }
      } catch { /* ignore malformed manifest */ }
    }

    const inverters: string[] = [];
    for (const d of req.designs) for (const v of d.inverters) {
      if (inverters.indexOf(v) < 0) inverters.push(v);
    }

    const files: string[] = [];
    const svgNames: string[] = [];
    const designResults: Array<Record<string, unknown>> = [];
    const rsvg = req.png ? (process.env.RSVG_CONVERT || "rsvg-convert") : null;
    const haveRsvg = rsvg ? spawnSync(rsvg, ["--version"]).status === 0 : false;

    req.designs.forEach((d, i) => {
      const rendered = renderDesignSvg(d, req.vars, req.title || "design");
      const svgPath = path.join(out, "scheme-" + i + ".svg");
      fs.writeFileSync(svgPath, rendered.svg, "utf8");
      files.push(path.resolve(svgPath));
      svgNames.push("scheme-" + i + ".svg");
      let pngPath: string | null = null;
      if (haveRsvg) {
        const p = path.join(out, "scheme-" + i + ".png");
        const r = spawnSync(rsvg as string, ["-w", "1600", "-b", "white", svgPath, "-o", p], { encoding: "utf8" });
        if (r.status === 0) {
          pngPath = path.resolve(p);
          files.push(pngPath);
        }
      }
      designResults.push({ svg: path.resolve(svgPath), png: pngPath, cost: d.cost, branches: d.branches.length });
    });

    const inverterNames: Array<{ varName: string; svg: string }> = [];
    inverters.forEach((v, i) => {
      const svg = renderInverterSvg(v, "~" + v);
      const p = path.join(out, "inverter-" + i + ".svg");
      fs.writeFileSync(p, svg, "utf8");
      inverterNames.push({ varName: v, svg: "inverter-" + i + ".svg" });
      files.push(path.resolve(p));
    });

    const logic = req.logic === false || req.truths ? null : renderLogicPanel(req, out);
    if (logic) {
      files.push(path.resolve(path.join(out, logic.svg)));
      if (logic.tex) files.push(path.resolve(path.join(out, logic.tex)));
      const pdf = path.join(out, "logic.pdf");
      if (fs.existsSync(pdf)) files.push(path.resolve(pdf));
    }

    const index = path.join(out, "index.html");
    fs.writeFileSync(index, renderIndexHtml(req, svgNames, inverterNames, logic), "utf8");
    files.push(path.resolve(index));

    fs.writeFileSync(manifestPath, JSON.stringify({ files }, null, 2), "utf8");
    files.push(path.resolve(manifestPath));
    process.stdout.write(JSON.stringify({
      ok: true, out, index: path.resolve(index), files,
      designs: designResults,
      inverters: inverterNames.map((x) => ({ var: x.varName, svg: x.svg })),
    }) + "\n");
    return 0;
  } catch (e) {
    process.stdout.write(JSON.stringify({ ok: false, error: String(e) }) + "\n");
    return 1;
  }
}

process.exit(main());
