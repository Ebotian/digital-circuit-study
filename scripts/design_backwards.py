
"""逆向:给定逻辑函数,自动设计互补 CMOS 网络。"""
from itertools import combinations, product

def tt_of(f, n):
    """真值表,下标 m 的第 i 位 = 输入 i 的取值"""
    return [f(*[(m >> i) & 1 for i in range(n)]) for m in range(1 << n)]

def mono_inc(g, n):
    """g 对每个输入是否单调增"""
    for m in range(1 << n):
        for i in range(n):
            if not (m >> i) & 1 and g[m] > g[m | (1 << i)]:
                return False
    return True

def realizable(tt, n):
    """单级互补 CMOS 可实现 ⟺ (not f) 单调增"""
    return mono_inc([1 - x for x in tt], n)

def count_realizable(n):
    return sum(1 for code in range(1 << (1 << n))
               if realizable([(code >> m) & 1 for m in range(1 << n)], n))

def prime_implicants(g, n, want=1):
    onset = frozenset(m for m in range(1 << n) if g[m] == want)
    if not onset: return []
    cubes = []
    for k in range(n, -1, -1):
        for vs in combinations(range(n), k):
            for pat in product([0, 1], repeat=k):
                mask = val = 0
                for i, p in zip(vs, pat):
                    mask |= 1 << i
                    if p: val |= 1 << i
                mem = frozenset(m for m in range(1 << n) if (m & mask) == val)
                if mem <= onset: cubes.append((mask, val, mem))
    primes = []
    for c in cubes:
        if not any(d[0] != c[0] and (d[0] & c[0]) == d[0] and (c[1] & d[0]) == d[1]
                   and d[2] > c[2] for d in cubes):
            primes.append(c)
    return list({(c[0], c[1], c[2]) for c in primes})

def min_sop(g, n, names):
    """最小化积之和(贪心覆盖)"""
    onset = set(m for m in range(1 << n) if g[m])
    if not onset: return ["0"]
    primes = prime_implicants(g, n)
    chosen, left = [], set(onset)
    while left:
        best = max(primes, key=lambda c: len(c[2] & left))
        chosen.append(best); left -= best[2]
    out = []
    for mask, val, _ in sorted(chosen, key=lambda c: bin(c[0]).count("1"), reverse=True):
        lit = [names[i] if (val >> i) & 1 else names[i] + "'"
               for i in range(n) if (mask >> i) & 1]
        out.append("".join(lit) if lit else "1")
    return out

def design(f, n, names, label):
    tt = tt_of(f, n)
    ok = realizable(tt, n)
    print(f"--- {label} ---")
    if not ok:
        print("  单级互补 CMOS: ✗ 不可实现  (¬f 非单调增)")
        return
    pd = [1 - x for x in tt]                      # 下拉 = ¬f
    terms = min_sop(pd, n, names)
    nfet = sum(len(t.replace("'", "")) for t in terms if t != "0")
    print(f"  单级互补 CMOS: ✓ 可实现")
    print(f"  下拉网络 f_pd = ¬f = " + " ∨ ".join(terms))
    print(f"  → NMOS {nfet} 个(每个乘积项一条串联链,项间并联)")
    print(f"  → PMOS {nfet} 个(串并联对偶)")
    print(f"  → 合计 {2*nfet} 个晶体管")

print("=== 一、单级 CMOS 的'可达集'有多大 ===")
print(f"{'输入数 n':>7} {'全部函数 2^(2^n)':>18} {'单级可实现':>12} {'占比':>12}")
for n in range(1, 5):
    total = 1 << (1 << n); r = count_realizable(n)
    print(f"{n:>7} {total:>18} {r:>12} {r/total:>11.2%}")

print()
print("=== 二、逆向设计演示(n=2,3) ===")
A, B, C = "A", "B", "C"
design(lambda a,b: 1-(a&b), 2, [A,B], "NAND2  ¬(AB)")
design(lambda a,b: 1-(a|b), 2, [A,B], "NOR2   ¬(A∨B)")
design(lambda a,b: a&b,     2, [A,B], "AND2   AB")
design(lambda a,b: a^b,     2, [A,B], "XOR2   A⊕B")
design(lambda a,b: 1-(a^b), 2, [A,B], "XNOR2  ¬(A⊕B)")
design(lambda a,b,c: 1-(a&b&c), 3, [A,B,C], "NAND3  ¬(ABC)")
design(lambda a,b,c: 1-(a|b|c), 3, [A,B,C], "NOR3   ¬(A∨B∨C)")
design(lambda a,b,c: (a&b)|(a&c)|(b&c), 3, [A,B,C], "MAJ3   多数表决")
