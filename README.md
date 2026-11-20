# digital-circuit-study

> 从一张 4 个管子的 NOR 图出发,一路走到用真实 PDK 量出成本函数。
> From a 4-transistor NOR gate to measured cost functions on real PDKs.

三本 HTML 讲义 + 一个 C++20 命令行工具 + 一个 TS/Node 渲染 worker + 全部原始数据来源说明。

旧的 `scripts/*.py` 已经退役;功能由解耦后的 C++ 核心和统一的 `dcstudy` CLI 提供。
所有结论都用独立工具复核:Z3 做组合优化,**Yosys SAT 做形式等价证明**,Verilator 做第二实现的仿真。

---

## 讲义

| # | 文件 | 讲什么 | 前置 |
|---|---|---|---|
| 0 | `docs/intuition-zero.html` | **从一个 NOR 出发** — 直觉篇。10 步「观察 → 提问」,自己走出一套代数 | 只要知道 MOS 能当开关 |
| 1 | `docs/formal-analysis.html` | **从电路到代数** — 正向:电路 → 函数。矩阵、布尔闭包、良构性判据 | 矩阵乘法 |
| 2 | `docs/synthesis-inverse.html` | **从代数到电路** — 逆向:函数 → 电路。单调性判据、Dedekind 数、纤维与成本。**第 8 章是全部实测数据** | 读完讲义一 |

`docs/index.html` 是总目录页。启用 GitHub Pages 后(Source 选 `main` 分支的 `/docs` 目录)可直接在线阅读。

---

## 已经用真实工具量出来的结论

| 结论 | 证据 |
|---|---|
| 晶体管数 $2L$ 公式**只对单调函数**成立 | Nangate45 CDL:9 个单调单元全中,XOR2 一个失手(预测 12,实测 **10**) |
| 单级 CMOS 的可达集 **= Dedekind 数** | `dcstudy bool count`:3, 6, 20, 168, 7581;$n=6$ 时占比 $4\times10^{-13}$ |
| 非单调函数的最少管数**取决于设计空间** | 精确覆盖 + 形式证明:**XOR2 = 6T(1 类拓扑)**、**AND2 = 5T**、NAND2 = 4T |
| 「最少管数 = 最小面积」**并非普适** | Nangate45 满足 $0.133(k+2)$;ICsprout55 每管面积 0.54→0.24 乱跳 |
| 面积对**阈值电压完全瞎** | 同一 XOR2 三档 VT 面积都是 3.96;漏电差 **104×**,延时差 2.4× |
| 「换 HVT 省功耗」**不无条件成立** | GCD 实测:HVT 漏电降 64×,但总功耗**反升 22%**(漏电只占 0.05%) |
| 工具的成本函数里**没有漏电** | 三库并列喂 ABC,无约束/紧约束/松约束都选 HVT —— 靠 tie-break 乱挑 |
| 同一 RTL,**面积可差 50%** | 裸 `abc -liberty` 给 1.596;完整 `synth` 给 2.394(延时驱动升级) |

> 「1 类拓扑」指 **TG 规范化 + 输入重命名自同构** 后的类数。完整 TG 在求解空间里用 N/P 对表示(更一般的规范形),渲染时再折叠成 TG;原始最优解通常还有 A/B 镜像(例如 XOR2 的 2 个原始解)。

---

## 依赖

| 工具 | 已验证版本 | 用途 |
|---|---|---|
| [clang++](https://clang.llvm.org/) / CMake | 23.1.5 / 4.5 | C++20 核心与构建 |
| [Z3](https://github.com/Z3Prover/z3) | 4.16(C++ 头文件 + libz3) | 仅 `dcstudy-z3` worker 链接:Optimize 与开关级精确求解 |
| [Catch2](https://github.com/catchorg/Catch2) | 3.16.1 | 单元测试 |
| [bliss](https://users.aalto.fi/tjunttil/bliss/) | 0.77 | 输入自同构群(彩色二部图) |
| [Fil-C](https://fil-c.org/)(`perefil-bin`) | 0.687 | 可选:完整 safe CLI 构建 + 内核内存安全冒烟 |
| Node.js | 26.13 | 默认渲染 worker(`workers/render_worker.ts`,直接运行 TS) |
| Python | 3.14 | 可选:旧渲染 worker / 性能对照(`workers/render_worker.py`) |
| rsvg-convert | 2.x | 可选:渲染 worker 直接输出 PNG |
| pdflatex + circuitikz | TeX Live | 可选:标准逻辑门符号(缺失时退回逻辑块) |
| [Yosys](https://yosyshq.net/yosys/) | 0.66 | **形式等价证明**(SAT miter) |
| [Verilator](https://verilator.org) | 5.054 | 独立第二实现的**功能判定** |
| [iEDA](https://github.com/OSCC-Project/iEDA)(iSTA / iPA) | git 071226f | 讲义第 8 章的 STA 延时 + 功耗 |
| [Nangate45](https://github.com/The-OpenROAD-Project/OpenROAD-flow-scripts) | 45nm 学术预测库 | **带 CDL** → 标准单元晶体管数的真相来源 |
| [ICsprout55 v2](https://github.com/openecos-projects/icsprout55-pdk) | 55nm 真实工艺 9T | SVT / LVT / HVT 三档 VT × 32 个角 |

> **两个 PDK 都不在本仓库内**(体积与许可原因)。CLI 通过环境变量找它们。

### 构建

```bash
make                 # 主程序 + dcstudy-z3 worker
make test            # Catch2 单元测试(会启动 worker)
make integration     # 黑盒 CLI + Yosys/Verilator 集成测试
make filc            # 用 Fil-C 编完整 CLI(不链 Z3)
make filc-smoke      # 可选:用 Fil-C 跑 Z3-free 内核(约 15 s)
```

渲染命令默认用 Node(直接跑 `workers/render_worker.ts`);`--png` 额外需要 `rsvg-convert`,`logic.svg` 额外需要 `pdflatex` + `circuitikz`(缺失时退回逻辑块)。Python worker 保留作对照,可用 `DCSTUDY_RENDER_HELPER=workers/render_worker.py` 切回;Python 版没有逻辑面板。

没有 Catch2 时 CMake 仍会构建主程序,只是跳过单元测试。**Z3 只被 worker 链接**:主程序本身不依赖 `z3++.h`/`libz3`;构建 worker 才需要它们。

### Worker 进程隔离

```text
+----------------------+   JSON / temp files   +---------------------+
| dcstudy (safe CLI)   | <-------------------> | dcstudy-z3 (Z3)     |
| Fil-C / ASan clean   |                       | C++ + libz3         |
+----------------------+                       +---------------------+
```

- 主程序先尝试 `$DCSTUDY_Z3_HELPER`,否则找自己旁边的 `dcstudy-z3`。
- worker 每次调用一个请求,120 秒超时;Z3 崩溃/越界不会带走主进程。
- 协议是普通 JSON,见 `include/dcstudy/z3_protocol.hpp`;worker 也可以单独替换成 mock 做测试。
- `render` 复用同一套 IPC:主进程把已经验证过的设计拓扑交给 `workers/render_worker.ts`,Node 只负责画 SVG/HTML,不参与正确性判定。`workers/render_worker.py` 是保留的对照实现。

> **Fil-C 说明**:`make filc` 已能编出完整的 `build/dcstudy-filc`(约 40 s),它通过 `/proc/self/exe` 找到 host 编的 `build/dcstudy-z3` 并进程调用。`make filc-smoke` 是更小的纯内核冒烟;完整 CLI 也已在 Fil-C 下跑通 `bool` / `cdl --exact` / `design min --verify yosys|both` / `render --png`。`render` 命令额外调用 Node TS worker;如果二进制被搬走,用 `DCSTUDY_RENDER_HELPER=/path/to/render_worker.ts` 指定(也可以指向 Python 版本做对照)。

完整 CLI 的内存检查用 ASan/UBSan 补上:

```bash
cmake -S . -B build-asan -DCMAKE_CXX_COMPILER=clang++ -DDCSTUDY_SANITIZE=ON
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=0 ./build-asan/dcstudy_tests
ASAN_OPTIONS=detect_leaks=0 ./build-asan/dcstudy design min --expr 'A^B' --max-cost 6 --max-chain 3 --verify yosys
```

### 装 Nangate45(只有它带 CDL)

本仓库的 `cdl solve` 需要标准单元的 CDL 网表。推荐走 [yosys-sta](https://github.com/pku-liang/yosys-sta) 的预编译包:

```bash
git clone https://github.com/pku-liang/yosys-sta
cd yosys-sta
mkdir -p bin pdk
(cd bin && curl -sL https://ysyx.oscc.cc/slides/resources/archive/ieda.tar.bz2        | tar xjf -)
(cd pdk && curl -sL https://ysyx.oscc.cc/slides/resources/archive/nangate45.tar.bz2   | tar xjf -)
```

### 装 ICsprout55 v2

```bash
git clone https://github.com/openecos-projects/icsprout55-pdk
cd icsprout55-pdk && make unzip     # 下载 pinned v2 asset set(约 40GB)
```

`tools/pdk/*.tcl` 是本仓库为 yosys-sta 写的 PDK 描述,复制到 `yosys-sta/scripts/pdk/` 即可用 `make syn PDK=icsprout55_SVT` 之类的方式调用。

---

## 快速开始

所有命令都可以加 `--json`;返回码约定:**0 成功,1 证明/可行性失败,2 用法或运行时错误**。

### 1. 布尔函数与可达集

```bash
./build/dcstudy bool info --expr 'A^B'
# truth: 0110
# monotone: no
# self-dual: no
# single-stage CMOS: no

./build/dcstudy bool count --n 5
# n=5  monotone functions=7581
```

`n=6` 也实现了(7,828,354),但约 40 秒 / 10 GB,需要显式 `--heavy`。

### 2. 单级互补 CMOS 的精确综合

```bash
./build/dcstudy cmos synth --expr '~(A&B)'
# pull-down = (A & B)
# NMOS=2  PMOS=2  total=4T

./build/dcstudy cmos synth --expr 'A^B'
# single-stage complementary CMOS: no (complement is not monotone increasing)
```

下拉网络用 **Quine–McCluskey 质蕴含项 + 精确 Petrick 覆盖**,按文字数(即晶体管数)最小化,不是贪心覆盖。

### 3. 读真实标准单元的晶体管级网表

```bash
export PDK_ROOT=~/yosys-sta/pdk          # 或者直接给 NANGATE45_CDL
./build/dcstudy cdl solve --cell XOR2_X1
```

```text
cell XOR2_X1: 10 devices, mode=fixpoint
  A B | Z
  0 0 | 0
  0 1 | 1
  1 0 | 1
  1 1 | 0
  Z = 0110
```

默认走**单调不动点 + 布尔矩阵可达闭包**;`--exact` 用 Z3 对导通器件建模并展开 VDD/VSS 的可达路径。遇到浮空栅极时自动退回 Z3,不会把「未证明」当「已证明」。

### 4. 最小晶体管数设计 + 形式证明

```bash
./build/dcstudy design min --expr 'A^B' --max-cost 6 --max-chain 3 --verify yosys
```

```text
optimum: 6T  designs=...
  [0] 6T  inverters=B  branches=3
       A --[TGL(B)]-- Y
       B --[P(A)]-- Y
       Bb --[N(A)]-- Y
       yosys: PASS
```

- `--all` 枚举全部最优解(默认最多 8 个);`--canonical` 按保 f 的输入置换商掉对称解。
- `--emit DIR` 会写出 `dut.v` / `ref.v` / `miter.v` / `tb.v`。
- `--verify yosys|sim|both` 选择证明方式;默认 `yosys`。

设计空间 D:N/P 各 1T,传输门 2T;**传输门必须两相控制都可用**。选择问题编码成 0/1 整数规划交给 **Z3 Optimize**;不是手写 DFS 穷举。

#### 静态互补 CMOS 限制（`--static-cmos`）

这个 flag 不是"函数 ANF 是否仿射"，而是 **κ 是否仿射** + PDN/PUN 对偶结构。单级不可实现时会自动退回**多级静态 CMOS**（Yosys/ABC + Nangate45 静态单元）：

```bash
./build/dcstudy design min --expr '~(A&B)' --static-cmos --verify static
# 单级 4T, PDN=A&B, PUN=(~A|~B), switch-level PASS

./build/dcstudy design min --expr 'A^B' --static-cmos --verify static
# 单级不可行 → 多级 14T: NOR2_X1 → AND2_X1 → NOR2_X1
# 每个单元都是静态互补 CMOS，flattened switch-level PASS
```

- 单级判据：`¬f` 单调递增；PDN 是 NMOS 串并联，PUN 是它的 De Morgan 对偶
- 多级：Yosys/ABC 映射到静态单元白名单（NAND/NOR/AND/OR/AOI/OAI/INV/BUF），禁止 XOR/XNOR/MUX/TG 单元；静态模式默认预算 `--max-cost 32`
- `--max-fanin 2` 可强制只用 2 输入标准单元（例如 NAND3 → AND2+NAND2 = 10T；不加约束则单级 NAND3 = 6T）
- 每个器件 gate-only；`--no-tg` 不能替代 `--static-cmos`
- `--verify static` 对生成的多级网表做 **CDL flatten → Z3 switch-level** 全输入证明
- `--emit` 单级写 `static-cmos.json/cdl`；多级写 `gate-level.v`、`netlist.json`、`flattened.cdl`
- `render --static-cmos` 单级画 PUN/PDN 互补图，多级画门级静态单元图 + 行为等价逻辑符号；多级门级图的 AND/NAND/NOR 等直接复用 circuitikz 标准端口（AOI/OAI 等无单端口标准符号时才退回方块）

```bash
./build/dcstudy render --expr '~(A&B)' --static-cmos --out /tmp/nand-static --png
./build/dcstudy render --expr 'A^B' --static-cmos --out /tmp/xor-static --png
```

三输入 NAND 的课本案例也可以自动复现：

```bash
# 单级静态 CMOS：3 PMOS 并联 + 3 NMOS 串联 = 6T
./build/dcstudy design min --expr '~(A&B&C)' --vars A,B,C --static-cmos --verify static

# 只用 2 输入单元：AND2_X1 (6T) + NAND2_X1 (4T) = 10T
./build/dcstudy design min --expr '~(A&B&C)' --vars A,B,C \
  --static-cmos --max-fanin 2 --verify static

# 两种视图都可以出图
./build/dcstudy render --expr '~(A&B&C)' --vars A,B,C --static-cmos --out /tmp/nand3-6t --png
./build/dcstudy render --expr '~(A&B&C)' --vars A,B,C --static-cmos --max-fanin 2 --out /tmp/nand3-10t --png
```

### 5. 一条命令复现讲义数字

```bash
./build/dcstudy pipeline --funcs 'xor2=A^B,nand2=~(A&B),and2=A&B' \
  --max-cost 6 --max-chain 3 --verify both --out out/cands
```

每个最优解都会生成独立的 `dut.v` 和 testbench,并通过 Yosys SAT + Verilator 两道关。Verilator 编译较慢,只要快速回归可以写 `--verify yosys`。

### 6. 自动渲染电路图

```bash
./build/dcstudy render --expr '~(A^B)' \
  --max-cost 6 --max-chain 3 --all --canonical \
  --out /tmp/xnor-render --png
xdg-open /tmp/xnor-render/index.html
```

会写出 `scheme-*.svg` / `scheme-*.png`、`inverter-*.svg`、`logic.svg`(标准门符号,来自 circuitikz)、`logic.tex` 和 `index.html`(真值表 + 逻辑抽象 + 方案 + 2T 反相器细节)。渲染 worker 是独立的 Node 进程,只负责画图,不参与正确性判定;布局使用源极轨道 + 实际 2T 反相器 + 共享 Y 总线。逻辑符号明确标注“行为等价,不是结构分解”;`--no-logic` 可以跳过它和 LaTeX 编译。

> 渲染性能(同请求,hyperfine 20 次,仅 worker):Python 22.8 ms / 15.9 MiB;TS/Node 无逻辑面板 42.1 ms / 89.2 MiB;TS + circuitikz 热缓存 47.9 ms / 89.5 MiB,首次编译约 324 ms。Node 的启动和堆是固定成本,渲染又是可选功能,所以优先保留更好读的 TS 布局。

### 7. 形式等价检查(含负对照)

```bash
./build/dcstudy verify equiv --dut out/cands/xor2_0/dut.v --expr 'A^B'
./build/dcstudy verify sim  --dut out/cands/xor2_0/dut.v --expr 'A^B'
```

Yosys 侧不是仿真:它把候选和**由真值表机械生成**的参考电路放进同一个 miter,令 `mismatch = (Y_dut !== Y_ref) | ~ok`,再用 `sat -prove mismatch 0` 对**所有输入组合**证明。失败时会打印反例。

### 8. Verilator 回归(含负对照)

```bash
cd verilog
verilator --binary --timing -Wno-fatal -Wno-WIDTH -Wno-UNUSEDSIGNAL \
          -Wno-DECLFILENAME --top-module tb -o sim xor6.v tb_xor6.v
./obj_dir/sim
```

`xor6.v` 里的 `BREAK=1` 是**故意接错**的版本(把 N3 的栅极错接到 GND),用来确认检查器真的会报错。

---

## 代码结构

```text
.
├── CMakeLists.txt / Makefile
├── include/dcstudy/
│   ├── bits.hpp          动态位集(真值表 / 覆盖集 / 矩阵行)
│   ├── boolfunc.hpp      布尔函数、表达式解析、ANF、Dedekind 计数
│   ├── boolmatrix.hpp    布尔半环矩阵乘法 + Warshall 传递闭包
│   ├── cdl.hpp           CDL 标准单元网表解析
│   ├── switchlevel.hpp   开关级不动点(精确解走 worker)
│   ├── cmos.hpp          Quine–McCluskey + 精确 Petrick 覆盖
│   ├── designspace.hpp   pass-network 设计空间 D(含传输门两相约束)
│   ├── synthesis.hpp     最小成本设计的约束/解码层
│   ├── z3_protocol.hpp   Z3 worker 的 JSON 协议
│   ├── z3_client.hpp     Z3 worker 查找 + 进程调用
│   ├── z3_solvers.hpp    Z3 worker 内实现(Optimize / SAT)
│   ├── symmetry.hpp       bliss 输入自同构群
│   ├── worker_client.hpp 通用 worker 查找 / JSON IPC
│   ├── render_client.hpp 渲染 worker 客户端
│   ├── verilog.hpp       DUT / 参考 / miter / testbench 生成
│   ├── verify.hpp        Yosys SAT 证明 + Verilator 仿真
│   └── json.hpp          有序 JSON 输出 + 解析
├── src/                  对应实现 + main.cpp(CLI)
│   ├── z3_helper_main.cpp worker 入口(Z3 + bliss)
│   ├── synthesis_z3.cpp   Z3 Optimize / orbit blocking
│   ├── symmetry.cpp       bliss 彩色图自同构
│   ├── switchlevel_z3.cpp 开关级精确 SAT 模型
│   ├── worker_client.cpp  通用 JSON worker IPC
│   ├── render_client.cpp  渲染 worker 客户端
│   └── z3_client.cpp      Z3 worker 客户端
├── workers/
│   ├── render_worker.ts   TS/Node SVG/HTML 渲染 worker(默认)
│   └── render_worker.py   Python 渲染 worker(对照/fallback)
├── tests/
│   ├── unit_tests.cpp    Catch2 单元测试
│   ├── filc_smoke.cpp    Fil-C 内存安全冒烟(Z3-free 内核)
│   └── integration.sh    黑盒 CLI / 形式工具集成测试
├── docs/                 三本 HTML 讲义(见上)
├── verilog/              Verilator 手工回归(含负对照)
├── rtl/                  讲义实验用 RTL
└── tools/pdk/            yosys-sta 的 PDK 描述文件
```

### 为什么不是「手工穷举」

| 旧做法 | 现在 |
|---|---|
| 枚举内部节点 2^k 种赋值 | 单调不动点 + 布尔矩阵闭包;必要时 Z3 精确建模 |
| 扫描全部 $2^{2^n}$ 个函数数单调函数 | 按偏序层做带约束的 DFS,只枚举反链;$n=5$ 约 3 ms |
| Quine–McCluskey 后贪心覆盖 | 质蕴含项 + 精确 Petrick 分支定界,最小化文字数 |
| 手写 DFS 搜支路组合 | 0/1 整数规划,Z3 Optimize 求全局最优 |
| 只靠 Verilator 仿真 | Yosys SAT 对所有输入证明等价 + Verilator 第二实现 |
| 手写 A/B 镜像去重 | bliss 自同构群 + solver 内 orbit blocking |
| 自己的模型自己验 | 参考电路由真值表机械生成,不手写 |

Dedekind 计数本质上是指数级的(输出就那么大),这里做的是**约束搜索**而不是无脑扫描;`n=6` 的 10 GB 是搜索状态的代价,所以默认要求 `--heavy`。

进程隔离并不改变算法:主进程仍然生成 branch 表、coverage 约束和 canonical key;worker 只返回最优 branch 索引集合。开关级的 `--exact` 也是把 netlist + 输入行发给 worker,拿回每行的 `0/1/Z/X`。

输入自同构群由 bliss 在 worker 里对彩色二部图计算;主进程把 branch 的置换作用一并传给 Z3 worker,枚举时直接 **block 整个对称轨道**,而不是输出后过滤。这样 A/B 对偶、镜像拓扑在搜索阶段就被商掉。

---

## 许可

**本仓库两种许可并存,按文件类型划分:**

| 部分 | 许可 |
|---|---|
| `docs/*.html`(讲义文档) | [CC BY-NC 4.0](LICENSE-docs) |
| `include/` `src/` `tests/` `workers/` `verilog/` `rtl/` `tools/`(代码) | [GPL-3.0-only](LICENSE) |

> ⚠️ **请留意**:CC BY-NC **禁止商业性使用**,它不是 OSI 认可的开源许可。所以本仓库**整体上不是一个开源仓库** —— 只有代码部分是 GPL-3.0 开源。
> 两类文件是分开的,不构成混合作品;但如果你要商用,请注意讲义部分不可用于商业目的。

---

## 致谢

- 讲义里的公式与推导是重新组织的,但相关理论分别来自 **Boole(1854)**、**Shannon(1938,继电器与开关电路的符号分析)**、**Bryant(1984,switch-level model)**。
- 工具:[Z3](https://github.com/Z3Prover/z3)、[yosys](https://yosyshq.net/yosys/)、[Verilator](https://verilator.org)、[iEDA](https://github.com/OSCC-Project/iEDA)、[yosys-sta](https://github.com/pku-liang/yosys-sta)。
- 工艺库:imec / Nangate(学术用途)、[openecos-projects/icsprout55-pdk](https://github.com/openecos-projects/icsprout55-pdk)。

## 相关

- 单级 CMOS 可达集的一个经典结论是「单调函数」,其计数即 Dedekind 数,见 OEIS [A000372](https://oeis.org/A000372)。
- 精确综合(SAT/ILP)方向可以继续查 *exact synthesis* 相关文献。
