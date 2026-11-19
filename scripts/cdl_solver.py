"""
通用开关级求解器(第三版)
关键修正:内部节点若其连通分量里没有任何"被驱动源",它只是"电荷节点"——
           静态分析里应视为【不受约束】,而不是"不可定值"。
           只有【输出端口】必须有确定来源。
"""
import re, itertools

import os, sys

# 标准单元 CDL 网表的位置:用环境变量指定,或放在 $PDK_ROOT/nangate45/cdl/ 下
CDL = os.environ.get("NANGATE45_CDL") or os.path.join(
    os.environ.get("PDK_ROOT", os.path.expanduser("~/yosys-sta/pdk")),
    "nangate45", "cdl", "NangateOpenCellLibrary.cdl")
if not os.path.exists(CDL):
    sys.exit("找不到 CDL 网表: " + CDL + "  (请设置 NANGATE45_CDL 或 PDK_ROOT,见 README)")

def read_cell(cell, path=CDL):
    txt = open(path, errors='ignore').read()
    head = re.search(r'\.SUBCKT\s+' + cell + r'\s+([^\n]*)', txt, re.I).group(1).split()
    body = re.search(r'\.SUBCKT\s+' + cell + r'\b(.*?)\.ENDS', txt, re.S | re.I).group(1)
    devs = []
    for line in body.splitlines():
        f = line.split()
        if len(f) >= 6 and f[0].startswith('M'):
            devs.append((f[1], f[2], f[3], 'P' if 'PMOS' in f[5].upper() else 'N'))
    return head, devs

def conducts(t, gv, dv, sv):
    if t == 'N': return gv == 1 and (dv == 0 or sv == 0)
    return gv == 0 and (dv == 1 or sv == 1)

def evaluate(devs, base, unknowns, outs):
    """返回 (是否合法, 输出取值)"""
    nets = set()
    for d,g,s,t in devs: nets |= {d,g,s}
    nets |= set(outs)
    unk = [n for n in sorted(nets) if n not in base]
    results = set()
    for asg in itertools.product([0,1], repeat=len(unk)):
        val = dict(base); val.update(dict(zip(unk, asg)))
        par = {n:n for n in val}
        def find(x):
            while par[x]!=x: par[x]=par[par[x]]; x=par[x]
            return x
        for d,g,s,t in devs:
            if conducts(t, val.get(g), val.get(d), val.get(s)):
                ra, rb = find(d), find(s)
                if ra!=rb: par[ra]=rb
        comp = {}
        for n in val: comp.setdefault(find(n), []).append(n)
        res = {}; bad = False
        for root, mem in comp.items():
            srcs = {val[n] for n in mem if n in base}
            if len(srcs) > 1: bad = True; break          # 冲突:两个源对拉
            if len(srcs) == 1:
                v = next(iter(srcs))
                for n in mem: res[n] = v
            # len == 0: 电荷节点,不受约束(不写 res)
        if bad: continue
        if any(res.get(n) != val[n] for n in res if n not in base): continue   # 自洽性
        if any(o not in res for o in outs): continue                            # 输出浮空
        results.add(tuple(res[o] for o in outs))
    return results

def report(cell, inputs):
    ports, devs = read_cell(cell)
    base0 = {'VDD':1, 'VSS':0}
    outs = [p for p in ports if p not in base0 and p not in inputs]
    print("单元 %s : %d 管, 输出 %s" % (cell, len(devs), outs))
    print()
    print("  A B   自洽输出       判定")
    print("  " + "-"*42)
    tr = []
    for combo in itertools.product([0,1], repeat=len(inputs)):
        base = dict(base0); base.update(dict(zip(inputs, combo)))
        r = evaluate(devs, base, None, outs)
        s = " | ".join(str(x) for x in sorted(r, key=str)) if r else "(无解)"
        v = "" if len(r)==1 else ("  <-- 多解/竞争" if len(r)>1 else "  <-- 浮空")
        print("  %s    %-14s %s" % ("".join(map(str,combo)), s, v))
        if len(r)==1: tr.append(r.pop()[0])
    print()
    exp = [a^b for a in (0,1) for b in (0,1)]
    if 'XNOR' in cell: exp = [1-x for x in exp]
    print("  实测   =", "".join(map(str,tr)) if len(tr)==4 else "-")
    print("  应为   =", "".join(map(str,exp)))
    if len(tr)==4:
        print("  结论:", "✓ 与 %s 一致,CDL 解读正确" % cell if tr==exp else "✗ 不符")
    print()

report('XOR2_X1', ['A','B'])
report('XNOR2_X1', ['A','B'])
