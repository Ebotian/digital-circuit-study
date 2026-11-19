"""
完整流水线:枚举 -> 生成 Verilog -> Verilator 判定 -> 只报告通过的

设计空间 D-2(修正版):
  单管  N(g) 1T  en:g=1  strong:v=0     P(g) 1T  en:g=0  strong:v=1
  传门  TGL(g) 2T  en:g=0  strong:1      TGH(g) 2T  en:g=1  strong:1
  【新约束】传输门必须两相驱动:TGL/TGH(g) 仅当 ~g 也在可用文字集里
  结构  Y 由若干支路(源 -> 串联链 -> Y)并联驱动, 单级
  判据  所有导通支路电平一致, 且至少一个强驱动
"""
import itertools, os, subprocess, sys

INPUTS = [(0,0),(0,1),(1,0),(1,1)]
NEG = {'A':'Ab','Ab':'A','B':'Bb','Bb':'B'}
def V(s,a,b): return {'0':0,'1':1,'A':a,'B':b,'Ab':1-a,'Bb':1-b}[s]
def G(lit,a,b): return V(lit,a,b)
COST = {'N':1,'P':1,'TGL':2,'TGH':2}

def sw(gkind, lit, v, a, b):
    """返回 (en, strong)"""
    g = G(lit,a,b)
    if gkind=='N':   return (g==1, v==0)
    if gkind=='P':   return (g==0, v==1)
    if gkind=='TGL': return (g==0, True)
    return (g==1, True)

def behav(src, chain):
    out=[]
    for (a,b) in INPUTS:
        v = V(src,a,b); en=True; st=True
        for (k,lit,c) in chain:
            e,s = sw(k,lit,v,a,b)
            en = en and e; st = st and s
        out.append(None if not en else (v,st))
    return tuple(out)

def chains(maxc, lits, tg):
    res=[]; fr=[()]
    while fr:
        nx=[]
        for ch in fr:
            for k in ['N','P','TGL','TGH']:
                pool = tg if k in ('TGL','TGH') else lits
                for l in pool:
                    if sum(x[2] for x in ch)+COST[k] <= maxc:
                        nx.append(ch+((k,l,COST[k]),))
        fr=nx; res+=nx
    return res

def build(lits, maxchain=4):
    tg = [l for l in lits if NEG[l] in lits]        # 两相都可用才允许传门
    best={}
    for src in ['0','1','A','B','Ab','Bb']:
        if src not in lits and src not in ('0','1'): continue
        for ch in chains(maxchain, lits, tg):
            c=sum(x[2] for x in ch); bh=behav(src,ch)
            if bh not in best or c<best[bh][0]:
                best[bh]=(c,[(src,ch)])
            elif c==best[bh][0]:
                best[bh][1].append((src,ch))
    return [(bh,c,r) for bh,(c,r) in best.items()]

def solve(tgt, lits, budget, br):
    """判据(修正): 对每种输入, 必须至少一条支路【强驱动】且值正确;
       同时任何一条在该输入上导通的支路, 其值也必须正确(这就是"电平一致")。
       注意允许多条支路重叠导通 —— 只要值相同, 弱驱动是良性的。"""
    sols=[]
    def okvals(b):
        for d in range(4):
            f=b[0][d]
            if f is not None and f[0]!=tgt[d]: return False
        return True
    usable=[b for b in br if okvals(b)]
    def dfs(mask, chosen, cost):
        if mask==15: sols.append((cost,tuple(chosen))); return
        pick=None
        for c in range(4):
            if (mask>>c)&1: continue
            cand=[b for b in usable if cost+b[1]<=budget
                  and b[0][c] is not None and b[0][c][1] and b[0][c][0]==tgt[c]]
            if not cand: return
            if pick is None or len(cand)<len(pick[1]): pick=(c,cand)
        for b in pick[1]:
            nm=mask
            for d in range(4):
                f=b[0][d]
                if f is not None and f[1]: nm|=1<<d
            dfs(nm, chosen+[b], cost+b[1])
    dfs(0,[],0)
    return sols

def design(f, name, maxshow=6):
    tgt=[f(a,b) for (a,b) in INPUTS]
    best=None; data=None
    for inv in [[],['A'],['B'],['A','B']]:
        lits=['A','B']+[x+'b' for x in inv]; base=2*len(inv)
        br=build(lits)
        for bud in range(0, 9-base):
            s=solve(tgt,lits,bud,br)
            if not s: continue
            c0=min(x[0] for x in s)
            if best is None or base+c0<best:
                best=base+c0; data=(inv,s)
            break
    inv,s = data
    mins=[x for x in s if 2*len(inv)+x[0]==best]
    groups={}
    for cost,ch in mins:
        key=tuple(sorted(repr((b[0],b[1])) for b in ch))
        groups.setdefault(key,[]).append(ch)
    print("  %-7s 最少 %dT  反相器=%s   语义不同的最小解 %d 个"
          % (name, best, (','.join(inv) if inv else '无'), len(groups)))
    return dict(name=name, cost=best, inv=inv, groups=groups, tgt=tgt)

def emit(design, tag, outdir):
    """把每个最小解写成 Verilog + TB"""
    tgt = design['tgt']; inv = design['inv']
    os.makedirs(outdir, exist_ok=True)
    made=[]
    for gi, key in enumerate(design['groups']):
        ch = design['groups'][key][0]
        body=[]
        for bi,b in enumerate(ch):
            src,chain = b[2][0]
            vs = {'0':"1'b0",'1':"1'b1",'A':'A','B':'B','Ab':'~A','Bb':'~B'}[src]
            ens=[]; sts=[]
            for (k,lit,_) in chain:
                gl = {'A':'A','B':'B','Ab':'~A','Bb':'~B'}[lit]
                if k=='N':   ens.append(gl);              sts.append("(%s==1'b0)"%vs)
                elif k=='P': ens.append("~%s"%gl);        sts.append("(%s==1'b1)"%vs)
                elif k=='TGL': ens.append("~%s"%gl);      sts.append("1'b1")
                else:        ens.append(gl);              sts.append("1'b1")
            body.append("  wire en%d = %s;\n  wire st%d = %s;\n  wire [0:0] v%d = %s;"
                        % (bi+1, " & ".join(ens), bi+1, " & ".join(sts), bi+1, vs))
        n=len(ch)
        enall = " | ".join("en%d"%i for i in range(1,n+1))
        strg  = " | ".join("(en%d & st%d)"%(i,i) for i in range(1,n+1))
        # 只比较【都在导通】的支路对 —— 之前的写法拿 v1 当基准, 支路1 关掉时会误判
        pairs=[]
        for i in range(1,n+1):
            for j in range(i+1,n+1):
                pairs.append("  if (en%d && en%d && (v%d !== v%d)) agree = 1'b0;" % (i,j,i,j))
        agree = "\n".join(pairs) if pairs else "  // 只有一条支路, 无需比较"
        pick  = "\n".join("  else if (en%d & st%d) Y = v%d;" % (i,i,i) for i in range(1,n+1))
        verilog = (
"module dut (input logic A, B, output logic Y, output logic ok);\n" +
"\n".join(body) + "\n" +

"  wire any_en = %s;\n  wire any_st = %s;\n" % (enall, strg) +
"  logic agree;\n  always_comb begin\n    agree = 1'b1;\n%s\n  end\n" % agree +
"  assign ok = any_en & any_st & agree;\n" +
"  always_comb begin\n    if (1'b0) Y = 1'b0;\n%s\n    else Y = 1'b0;\n  end\n" % pick +
"endmodule\n")
        expbits = "".join(str(tgt[3-i]) for i in range(4))   # EXP[i] = tgt[i]
        tb = ("module tb;\n  logic A,B,Y,ok;\n  dut u(.A(A),.B(B),.Y(Y),.ok(ok));\n"
"  int f=0;\n  logic [3:0] EXP = 4'b" + expbits + ";\n  initial begin\n"
"    for (int i=0;i<4;i++) begin\n      A=i[1]; B=i[0]; #1ns;\n"
"      if (!ok || (Y !== EXP[i])) f++;\n    end\n"
"    if (f==0) $display(\"PASS\"); else $display(\"FAIL %0d/4\", f);\n    $finish;\n  end\nendmodule\n")
        fn = os.path.join(outdir, "%s_%d.v" % (tag, gi))
        open(fn,'w').write(verilog + tb)
        made.append((gi, fn, ch))
    return made

def verify(fn):
    d = os.path.dirname(fn)
    od = fn.replace(".v",".obj")
    r = subprocess.run(["verilator","--binary","--timing","-Wno-fatal","-Wno-WIDTH",
                        "-Wno-UNUSEDSIGNAL","-Wno-DECLFILENAME","--top-module","tb",
                        "-o","simv","-Mdir",od, fn], capture_output=True, text=True)
    if r.returncode != 0:
        return None, ((r.stderr or "") + (r.stdout or ""))[-400:]
    exe = os.path.join(od,"simv")
    s = subprocess.run([exe], capture_output=True, text=True)
    ok = "PASS" in s.stdout
    return ok, s.stdout.strip().splitlines()[-1] if s.stdout.strip() else ""

def describe(ch, inv):
    parts=[]
    for b in ch:
        src,chain = b[2][0]
        sws = " · ".join("%s(%s)"%(k,l) for k,l,_ in chain)
        parts.append("%s --[%s]-- Y" % (src, sws))
    return ("(含反相器 %s)" % ",".join(inv)) if inv else "(无额外反相器)", parts

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = os.path.join(here, "..", "out", "cands")
    print("="*66)
    print("流水线:枚举 -> 生成 Verilog -> Verilator 判定")
    print("="*66)
    for f,name in [(lambda a,b:a^b, "XOR2"), (lambda a,b:1-(a&b), "NAND2"),
                   (lambda a,b:a&b, "AND2")]:
        d = design(f,name)
        made = emit(d, name.lower(), outdir)
        print("        -> 生成 %d 份 Verilog,交给 Verilator:" % len(made))
        npass=0
        for gi, fn, ch in made:
            ok, msg = verify(fn)
            tag, parts = describe(ch, d['inv'])
            status = "PASS" if ok else ("FAIL" if ok is False else "ERROR")
            if ok is None and gi==0: print("            [verilator] " + msg.replace(chr(10)," | ")[:300])
            if ok: npass+=1
            if gi < 3:
                print("           [%d] %-6s %s %s" % (gi+1, status, tag, parts[0]))
                for p in parts[1:]: print("                %s" % p)
            elif ok:
                print("           [%d] %-6s %s" % (gi+1, status, tag))
        print("        合计:%d/%d 通过 Verilator" % (npass, len(made)))
        print()
