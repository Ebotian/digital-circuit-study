#!/usr/bin/env python3
"""RVTM (requirements verification traceability matrix) for REQ-SEQ-*.

The design doc is the source of truth for the requirement set and phases; the
JSON file records status, evidence and notes.  This tool validates both and
generates docs/sequential-rvtm.md, so the matrix cannot drift from the data or
from the requirement list.
"""
import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DESIGN = ROOT / "docs" / "sequential-design.md"
DATA = ROOT / "requirements" / "rvtm.json"
OUT = ROOT / "docs" / "sequential-rvtm.md"
BT = chr(96)
STATUSES = ("not_implemented", "in_progress", "verified", "waived")
REQ_RE = re.compile(r"\*\*(REQ-SEQ-([0-9]{3}))（([^）]*)）\*\*")
ISO_RE = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}$")
EVIDENCE_KINDS = ("test", "doc", "artifact", "commit")


def load_design():
    text = DESIGN.read_text()
    reqs = {}
    for m in REQ_RE.finditer(text):
        rid = m.group(1)
        if rid in reqs:
            raise SystemExit("rvtm: duplicate requirement " + rid + " in the design doc")
        parts = [part.strip() for part in m.group(3).split("·")]
        reqs[rid] = {
            "phase": parts[1] if len(parts) > 1 else "?",
            "level": parts[0] if parts else "",
        }
    if not reqs:
        raise SystemExit("rvtm: no REQ-SEQ requirements found in " + str(DESIGN))
    return reqs


def load_data():
    def no_dups(pairs):
        seen = {}
        for key, value in pairs:
            if key in seen:
                raise SystemExit("rvtm: duplicate JSON key " + key)
            seen[key] = value
        return seen

    return json.loads(DATA.read_text(), object_pairs_hook=no_dups)


def check_evidence(rid, ev):
    kind = ev.get("kind")
    ref = ev.get("ref", "")
    if kind not in EVIDENCE_KINDS:
        return rid + ": unknown evidence kind " + str(kind)
    if not ref:
        return rid + ": evidence needs a ref"
    if kind == "commit":
        out = subprocess.run(
            ["git", "-C", str(ROOT), "cat-file", "-e", ref + "^{commit}"],
            capture_output=True,
        )
        if out.returncode != 0:
            return rid + ": evidence commit does not exist: " + ref
        return None
    path = ROOT / ref
    if not path.exists():
        return rid + ": evidence path does not exist: " + ref
    if kind == "test":
        case = ev.get("case")
        if case and case not in path.read_text(errors="replace"):
            return rid + ": test case not found in " + ref + ": " + case
    return None


def validate(data, doc):
    errors = []
    baseline = data.get("baseline", {})
    if not baseline:
        errors.append("missing baseline object")
    design = baseline.get("design", "")
    if not design or not (ROOT / design).exists():
        errors.append("baseline.design must be an existing path")
    if not ISO_RE.match(baseline.get("date", "")):
        errors.append("baseline.date must be YYYY-MM-DD")
    if data.get("defaults", {}).get("status") != "not_implemented":
        errors.append("defaults.status must be not_implemented")
    entries = data.get("requirements", {})
    if not isinstance(entries, dict):
        errors.append("requirements must be an object")
        return errors, {}
    norm = {}
    for rid, entry in entries.items():
        if rid not in doc:
            errors.append("unknown requirement in RVTM: " + rid)
            continue
        status = entry.get("status", "not_implemented")
        if status not in STATUSES:
            errors.append(rid + ": unknown status " + str(status))
        if "phase" in entry and entry["phase"] != doc[rid]["phase"]:
            errors.append(
                rid
                + ": phase "
                + str(entry["phase"])
                + " does not match the design doc ("
                + doc[rid]["phase"]
                + ")"
            )
        if not ISO_RE.match(entry.get("updated", "")):
            errors.append(rid + ": updated must be YYYY-MM-DD")
        if status == "verified" and not entry.get("evidence"):
            errors.append(rid + ": verified needs at least one evidence entry")
        if status == "waived" and not entry.get("reason"):
            errors.append(rid + ": waived needs a reason")
        for ev in entry.get("evidence", []):
            err = check_evidence(rid, ev)
            if err:
                errors.append(err)
        norm[rid] = entry
    return errors, norm


def evidence_text(ev):
    kind = ev.get("kind")
    ref = ev.get("ref", "")
    if kind == "commit":
        return "commit " + ref
    if kind == "test" and ev.get("case"):
        return ref + "（" + ev["case"] + "）"
    return ref


def render(doc, norm, baseline):
    ids = sorted(doc, key=lambda r: int(r[-3:]))
    counts = {status: 0 for status in STATUSES}
    rows = []
    unimplemented = {}
    for rid in ids:
        entry = norm.get(rid, {})
        status = entry.get("status", "not_implemented")
        counts[status] += 1
        if status == "not_implemented":
            unimplemented.setdefault(doc[rid]["phase"], []).append(rid)
        evidence = "；".join(evidence_text(ev) for ev in entry.get("evidence", []))
        note = entry.get("note", "")
        if status == "waived":
            note = "理由：" + entry.get("reason", "")
        rows.append(
            "| "
            + rid
            + " | "
            + doc[rid]["phase"]
            + " | "
            + status
            + " | "
            + (evidence or "—")
            + " | "
            + (note or "—")
            + " |"
        )
    lines = [
        "# 时序逻辑 RVTM（Requirements Verification Traceability Matrix）",
        "",
        "> 本文件由 " + BT + "tools/rvtm.py" + BT + " 生成，请勿手改：改 "
        + BT + "requirements/rvtm.json" + BT + " 后运行 " + BT + "make rvtm-gen" + BT + "。",
        "> 设计基线：" + BT + baseline.get("design", "") + BT
        + "；基线日期：" + baseline.get("date", "") + "；需求数：" + str(len(ids)) + "。",
        "> 状态：verified（有证据）/ in_progress / waived（有理由）/ not_implemented（默认）。",
        "",
        "## 汇总",
        "",
        "| 状态 | 数量 |",
        "|---|---|",
    ]
    for status in STATUSES:
        lines.append("| " + status + " | " + str(counts[status]) + " |")
    lines += ["", "## 未实现清单（按阶段）", "", "| 阶段 | 需求 |", "|---|---|"]
    for phase in sorted(unimplemented):
        lines.append("| " + phase + " | " + "、".join(unimplemented[phase]) + " |")
    lines += [
        "",
        "## 明细",
        "",
        "| REQ | 阶段 | 状态 | 证据 | 说明 |",
        "|---|---|---|---|---|",
    ]
    lines += rows
    lines.append("")
    return chr(10).join(lines)


def main():
    parser = argparse.ArgumentParser(description="REQ-SEQ RVTM checker/generator")
    parser.add_argument("command", choices=["check", "generate"])
    args = parser.parse_args()
    doc = load_design()
    data = load_data()
    errors, norm = validate(data, doc)
    if errors:
        for err in errors:
            print("rvtm: " + err, file=sys.stderr)
        return 1
    text = render(doc, norm, data.get("baseline", {}))
    if args.command == "generate":
        OUT.write_text(text)
        print("rvtm: wrote " + str(OUT.relative_to(ROOT)) + " (" + str(len(doc)) + " requirements)")
        return 0
    current = OUT.read_text() if OUT.exists() else ""
    if current != text:
        print(
            "rvtm: " + str(OUT.relative_to(ROOT)) + " is stale; run make rvtm-gen",
            file=sys.stderr,
        )
        return 1
    verified = sum(1 for rid in doc if norm.get(rid, {}).get("status") == "verified")
    print(
        "rvtm: ok ("
        + str(len(doc))
        + " requirements, "
        + str(verified)
        + " verified)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
