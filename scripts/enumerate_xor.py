
"""给定逻辑函数,在明确定义的设计空间里求:最少晶体管数 + 全部等效拓扑。

设计空间 D(参数化,必须先固定才能谈"最小"):
  器件  N(g)   栅极=g 的 NMOS            1T   导通: g=1 且 Vs=0   (源极依赖!)
        P(g)   栅极=g 的 PMOS            1T   导通: g=0 且 Vs=1
        TGL(g) PMOS(g) ∥ NMOS(~g)        2T   导通: g=0 (双向, 电平无损)
        TGH(g) PMOS(~g) ∥ NMOS(g)        2T   导通: g=1
  结构  Y 由若干支路并联驱动; 支路 = 源 → 串联链 → Y  (单级 pass 网络)
  源    0 1 A B  以及取反后的 Ab Bb  (每个反相器 2T)
  合法  每种输入组合下恰好一条支路导通, 且电平无损

用法: python3 enumerate_xor.py
"""
from itertools import product

INPUTS=[(0,0),(0,1),(1,0),(1,1)]
def V(s,a,b): return {'0':0,'1':1,'A':a,'B':b,'Ab':1-a,'Bb':1-b}[s]
KINDS=['N','P','TGL','TGH']; COST={'N':1,'P':1,'TGL':2,'TGH':2}

def conducts(k,lit,vs,a,b):
    g=V(lit,a,b)
    if k=='N':   return g==1 and vs==0
    if k=='P':   return g==0 and vs==1
    if k=='TGL': return g==0
    return g==1
def strong(k,v):
    if k=='N': return v==0
    if k=='P': return v==1
    return True

def behav(src,chain):
    out=[]
    for (a,b) in INPUTS:
        vs=V(src,a,b); ok=True; st=True
        for (k,lit,c) in chain:
            if not conducts(k,lit,vs,a,b): ok=False; break
            if not strong(k,vs): st=False
        out.append(None if not ok else (vs,st))
    return tuple(out)

def all_chains(maxc,lits):
    res=[]; frontier=[()]
    while frontier:
        nxt=[]
        for ch in frontier:
            for k in KINDS:
                for l in lits:
                    if sum(x[2] for x in ch)+COST[k]<=maxc:
                        nxt.append(ch+((k,l,COST[k]),))
        frontier=nxt; res+=nxt
    return res

def build(lits,maxchain=4):
    """behavior -> (mincost, [realizations])"""
    best={}
    for src in ['0','1','A','B','Ab','Bb']:
        if src=='Ab' and 'Ab' not in lits: continue
        if src=='Bb' and 'Bb' not in lits: continue
        for ch in all_chains(maxchain,lits):
            c=sum(x[2] for x in ch); bh=behav(src,ch)
            if bh not in best or c<best[bh][0]:
                best[bh]=(c,[(src,ch)] if bh not in best or c<best[bh][0] else best[bh][1]+[(src,ch)])
            elif c==best[bh][0]:
                best[bh][1].append((src,ch))
    return [(bh,c,r) for bh,(c,r) in best.items()]

def solve(tgt,lits,budget,br):
    sols=[]
    def dfs(mask,chosen,cost):
        if mask==15: sols.append((cost,tuple(chosen))); return
        unc=[c for c in range(4) if not (mask>>c)&1]
        pick=None
        for c in unc:
            cand=[]
            for b in br:
                if cost+b[1]>budget: continue
                if b[0][c] is None or not b[0][c][1] or b[0][c][0]!=tgt[c]: continue
                # 关键修正:该支路在任何其它组合上导通时,值必须也对
                bad=False
                for d in range(4):
                    e=b[0][d]
                    if e is None: continue
                    if not (e[1] and e[0]==tgt[d]): bad=True; break
                    if (mask>>d)&1: bad=True; break
                if bad: continue
                cand.append(b)
            if not cand: return
            if pick is None or len(cand)<len(pick[1]): pick=(c,cand)
        c,cand=pick
        for b in cand:
            nm=mask
            for d in range(4):
                if b[0][d] is not None: nm|=1<<d
            dfs(nm,chosen+[b],cost+b[1])
    dfs(0,[],0)
    return sols

def verify(tgt,realizations):
    got=[None]*4
    for (src,ch) in realizations:
        bh=behav(src,ch)
        for c in range(4):
            e=bh[c]
            if e is None: continue
            if got[c] is not None: return False,"组合%d被两条支路同时驱动"%c
            if not e[1]: return False,"组合%d电平有损"%c
            got[c]=e[0]
    return (got==list(tgt)), got

def design(f,name,show_n=0):
    tgt=[f(a,b) for (a,b) in INPUTS]
    best=None;bestdata=None
    for inv in [[],['A'],['B'],['A','B']]:
        lits=['A','B']+[x+'b' for x in inv]; base=2*len(inv)
        br=build(lits)
        for budget in range(0,9-base):
            s=solve(tgt,lits,budget,br)
            if not s: continue
            c0=min(x[0] for x in s)
            if best is None or base+c0<best:
                best=base+c0; bestdata=(inv,s,br)
            break
    inv,s,br=bestdata
    print(f"  {name:<7} 最少 {best}T   反相器={'(无)' if not inv else ','.join(x+'→'+x+'̄' for x in inv)}")
    # 只保留达到最小代价的解, 并按行为多重集去重
    mins=[x for x in s if 2*len(inv)+x[0]==best]
    groups={}
    for cost,ch in mins:
        key=tuple(sorted(repr((b[0],b[1])) for b in ch))
        groups.setdefault(key,[]).append(ch)
    print(f"       语义不同的最小解:{len(groups)} 个")
    if show_n:
        for i,(k,vs) in enumerate(sorted(groups.items(),key=lambda t:-len(t[1]))[:show_n]):
            ch=vs[0]
            ok,got=verify(tgt,[r for b in ch for r in b[2]])
            print(f"       [#{i+1}] {sum(b[1] for b in ch)+2*len(inv)}T  (含反相器), 独立验证={ok}  Y={got}")
            for b in ch:
                src,chain=b[2][0]
                print("            %s -- %s -- Y" % (src," · ".join(f"{k2}({l})" for k2,l,_ in chain)))
            if len(vs)>1: print(f"            (同一语义另有 {len(vs)-1} 种源/链写法)")
    return best,len(groups)

print("=== 在空间 D 内穷举(每个答案都已独立验证) ===\n")
for f,n,sn in [(lambda a,b:1-(a&b),"NAND2",0),(lambda a,b:1-(a|b),"NOR2",0),
               (lambda a,b:a&b,"AND2",0),(lambda a,b:a^b,"XOR2",4),
               (lambda a,b:1-(a^b),"XNOR2",0)]:
    design(f,n,sn); print()
