# digital-circuit-study

> 从一张 4 个管子的 NOR 图出发,一路走到用真实 PDK 量出成本函数。
> From a 4-transistor NOR gate to measured cost functions on real PDKs.

四本 HTML 讲义 + 一个 C++20 命令行工具 + 一个 TS/Node 渲染 worker + 全部原始数据来源说明。

旧的 `scripts/*.py` 已经退役;功能由解耦后的 C++ 核心和统一的 `dcstudy` CLI 提供。
所有结论都用独立工具复核:Z3 做组合优化,**Yosys SAT 做形式等价证明**,Verilator 做第二实现的仿真。

---

## 讲义

| # | 文件 | 讲什么 | 前置 |
|---|---|---|---|
| 0 | `docs/intuition-zero.html` | **从一个 NOR 出发** — 直觉篇。10 步「观察 → 提问」,自己走出一套代数 | 只要知道 MOS 能当开关 |
| 1 | `docs/formal-analysis.html` | **从电路到代数** — 正向:电路 → 函数。矩阵、布尔闭包、良构性判据 | 矩阵乘法 |
| 2 | `docs/synthesis-inverse.html` | **从代数到电路** — 逆向:函数 → 电路。单调性判据、Dedekind 数、纤维与成本。**第 8 章是全部实测数据** | 读完讲义一 |
| 3 | `docs/multi-output.html` | **从函数元组到网络** — 多输出:共享结构、三态、路径搜索。译码器只是 spec 实例 | 读完讲义二 |

`docs/index.html` 是总目录页。启用 GitHub Pages 后(Source 选 `main` 分支的 `/docs` 目录)可直接在线阅读。

相关调研：[`docs/transistor-synthesis-survey.md`](docs/transistor-synthesis-survey.md) — 晶体管级/多输出/pass-transistor 综合的现有实现与来源。

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
| [SuiteSparse:GraphBLAS](https://graphblas.org/) | 7.14 / GraphBLAS 10.5 | 布尔半环可达闭包(worker) |
| [FLINT](https://flintlib.org/) | 3.6 | GF(2) 秩与零空间(worker) |
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


### 可选依赖：mockturtle（库映射实验）

`third_party/mockturtle` 以 **git submodule** 管理（pinned commit `a3f817d`，MIT），默认不参与构建：

```bash
git submodule update --init --recursive
cmake -S . -B build-epfl -DDCSTUDY_WITH_EPFL=ON
```

它自带 kitty/lorina/percy 等依赖（都在子模块的 `lib/` 下），不需要 pacman 安装。`make format`/`lint` 只覆盖 `src include tests`，不会改子模块文件。

### 构建

```bash
make                 # 主程序 + dcstudy-z3 worker
make test            # 单元测试(按模块) + CLI 烟测
make smoke           # CLI 烟测 + (有 Fil-C 时)Fil-C 烟测
make verify          # 实例验证(见 tests/verify/README.md)
make render          # 可选: 每个实例的默认实现(含组合体用到的原子)出图
make render INSTANCE=fa1          # 该器件的全部实现
make render INSTANCE=fa1 VARIANT=ha  # 指定实现
make verify-all      # 实例 + 形式化(ctest) + 完整 CLI 套件
make integration     # 黑盒 CLI + Yosys/Verilator 集成测试
make filc            # 用 Fil-C 编完整 CLI(不链 Z3)
make smoke-filc      # 可选:用 Fil-C 跑 Z3-free 内核(约 15 s;旧名 filc-smoke 仍可用)
make format          # clice 默认风格格式化(需要 clice)
make format-check    # 只检查, CI 用
make lint            # clice lint(有 finding 即失败)
make tidy            # clang-tidy(tools/clang-tidy.yaml 记录刻意排除项)
make ci              # format-check + lint + test + verify-all
# 扩展质量门禁(make ci-full 全跑)
make asan            # ASan/UBSan 构建 + ctest
make werror          # -Werror 构建
make coverage        # LLVM 覆盖率(build-cov/coverage.txt)
make test-gcc        # gcc 构建 + ctest
make fuzz            # libFuzzer: JSON/表达式(FUZZ_TIME=30)
make shellcheck      # 15 个 shell 脚本
make pylint          # mypy on workers/render_worker.py
make ci-full         # ci + 以上全部(tslint 需 workers/node_modules)
make bench           # 基准: core/exact/compose/synth (hyperfine)
make bench-save      # 更新 bench/baselines/
make bench-micro     # BoolFunc/Bits 进程内微基准
make bench-perf      # perf stat (用户态计数器)
make bench-profile   # callgrind on multi compose
make bench-size      # bloaty 段体积

> GitHub CI 只跑不依赖重型库的部分：Release/-Werror、ASan、gcc、覆盖率、fuzz、shellcheck、mypy，以及 clice format/lint（从 [clice-io/clice](https://github.com/clice-io/clice) 的 v1.0.0 release 安装；`-DDCSTUDY_BUILD_Z3_HELPER=OFF`，Z3 用例自动 skip）。Yosys/Verilator/PDK、Fil-C 仍走本地 `make ci-full`。
```

渲染命令默认用 Node(直接跑 `workers/render_worker.ts`);`--png` 额外需要 `rsvg-convert`,`logic.svg` 额外需要 `pdflatex` + `circuitikz`(缺失时退回逻辑块)。Python worker 保留作对照,可用 `DCSTUDY_RENDER_HELPER=workers/render_worker.py` 切回;Python 版没有逻辑面板。

没有 Catch2 时 CMake 仍会构建主程序,但会注册一个**必定失败**的 `unit_missing_catch2` 测试,`ctest` 不会出现“零测试=通过”。集成测试缺少 CDL/Yosys/Node 时本地只打印 skip,**设了 `CI` 环境变量则直接 fail**,避免关键路径在 CI 里静默跳过。**Z3 只被 worker 链接**:主程序本身不依赖 `z3++.h`/`libz3`;构建 worker 才需要它们。

### Worker 进程隔离

```text
+----------------------+   JSON / temp files   +---------------------+
| dcstudy (safe CLI)   | <-------------------> | dcstudy-z3 (Z3)     |
| Fil-C / ASan clean   |                       | C++ + libz3         |
+----------------------+                       +---------------------+
```

- 主程序先尝试 `$DCSTUDY_Z3_HELPER`,否则找自己旁边的 `dcstudy-z3`。
- worker 每次调用一个请求,120 秒超时;Z3 崩溃/越界不会带走主进程。
- 协议是普通 JSON,见 `include/z3/z3_protocol.hpp`;worker 也可以单独替换成 mock 做测试。
- `render` 复用同一套 IPC:主进程把已经验证过的设计拓扑交给 `workers/render_worker.ts`,Node 只负责画 SVG/HTML,不参与正确性判定。`workers/render_worker.py` 是保留的对照实现。

### CPU 并行

默认使用所有硬件线程;用 `DCSTUDY_JOBS=N` 限制进程/线程数。当前并行点是:

- `multi exact` 的 `2^n` 个输入反相器集合;
- 静态多输出的逐输出 cone 验证(每个输出一个独立 Z3 请求);
- `analyze_switch` 精确 Z3 的 row × output 分块。

worker 仍然是“一次请求一个进程”,所以并行是进程级的;GPU 没有参与计算。

### 内存上限

`dcstudy` 启动时给**每个进程**装一个 `RLIMIT_AS`:yosys、`dcstudy-z3`、Node 渲染 worker 都继承同一个上限值。注意这不是共享配额——N 个 worker 各自都可以逼近上限,整棵树的理论峰值是 N×cap;要按进程树总量限额只能用 cgroup(见下)。

- `--mem-limit MB`(或 `DCSTUDY_MEM_LIMIT_MB=MB`)显式设定上限;`0` 表示只受硬 rlimit 约束(通常是无限)。
- 默认值一次性算好、取到最大:`MemAvailable - 2048`。唯一的约束就是**结束时还给系统留 2 GiB**,剩下的随便用,没有别的保守余量。30 GiB 空闲的机器上默认上限约 21 GB。
- 可用内存已经不足 2 GiB 时,cap 定为 1 MB 并告警:命令会立刻失败,而不是把最后那 2 GiB 也吃掉。读不到 `/proc/meminfo` 时告警一次、不加 cap。
- 超限时分配直接失败,CLI 输出 `memory limit reached (N MB, --mem-limit)`、退出码 2,而不是等内核 OOM killer 开枪(那会把整机拖进 swap)。
- 它是**启动时的快照**,不会阻止其它进程随后占内存,所以“留 2 GiB”是给本树留的余量,不是对整机的锁。
- `--heavy`(n=6 Dedekind,约 10 GB)不再单独放宽上限:可用内存低于约 12 GiB 时它会撞上默认 cap 并干净失败,此时用 `--mem-limit` 显式指定或换台机器。
- 管的是**虚拟地址空间**而不是 RSS;Node/V8 自己预留约 1.3 GB 地址空间,所以渲染 worker 的上限不要低于 2 GB(默认值足够)。
- 要**进程树总量**的 RSS 硬保证(连 swap 一起锁死),用 cgroup v2 包一层:`systemd-run --user --scope -p MemoryMax=4G -p MemorySwapMax=0 -- ./build/dcstudy ...`;并发 worker 数(`DCSTUDY_JOBS`)要按这个总量一起规划。
- Fil-C 构建的地址空间开销很大,跑 `dcstudy-filc` 时用 `--mem-limit 0`。
- 线程池也吃这份预算(每个线程 8 MB 栈):上限太小时 `parallel_for` 会退回更少的 worker 而不是 abort。

> **Fil-C 说明**:`make filc` 已能编出完整的 `build/dcstudy-filc`(约 40 s),它通过 `/proc/self/exe` 找到 host 编的 `build/dcstudy-z3` 并进程调用。`make smoke-filc` 是更小的纯内核冒烟(`filc-smoke` 为旧别名);完整 CLI 也已在 Fil-C 下跑通 `bool` / `cdl --exact` / `design min --verify yosys|both` / `render --png`。`render` 命令额外调用 Node TS worker;如果二进制被搬走,用 `DCSTUDY_RENDER_HELPER=/path/to/render_worker.ts` 指定(也可以指向 Python 版本做对照)。

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

默认走**单调不动点 + 布尔矩阵可达闭包**;`--exact` 用 Z3 对导通器件建模并展开 VDD/VSS 的可达路径。遇到浮空栅极时自动退回 Z3。Z3 版对每个输出分别问三个可满足性问题——能否被拉到 0、能否被拉到 1、能否**两条轨都不可达**——第三种成立时输出记 `Z`,所以浮空栅极不会把「可能被拉低」误报成确定的 0。不动点通过输入(而不是电源轨)强制的值也算有效强制数据——这种行会让 invariant 检查失败,但不影响传递;只有真正冲突的行才丢弃。浮空节点用**不动点的实际结果**判定:每行把不动点强制成 0/1 的网固定住,只留没被强制的栅极(不管它是否同时接了沟道端)作为自由变量;然后直接问一次「是否存在一种存储电荷状态让两条轨同时到达这个输出?」——可满足就整行记 `X`。这里没有枚举、也没有节点数上限。

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
- `--verify static` 只对 `--static-cmos` 有意义;不带的组合直接报错,不再返回未验证却 `ok:true` 的设计。
- 真值表输入:`--tt` 支持 0/1 串和 `0x` 十六进制,变量数上限 12(超出在建表和 `1 << n` 之前就报错),十六进制按位填表,超出表宽度的位也报错。
- `--max-fanin N` 可强制只用 ≤N 输入标准单元（例如 NAND3 → AND2+NAND2 = 10T；不加约束则单级 NAND3 = 6T）。扇入按**不同输入信号数**算，不是最长乘积项——`~(A|B|C)` 是三个并联单项，仍算 3 输入 NOR。禁用清单直接从 CDL 的 `*.PININFO` 生成，映射后还会逐单元复核输入数，超限直接报错而不是悄悄放过
- 每个器件 gate-only；`--no-tg` 不能替代 `--static-cmos`
- `--verify static` 对生成的多级网表做 **CDL flatten → Z3 switch-level** 全输入证明
- `--emit` 单级写 `static-cmos.json/cdl`；多级写 `gate-level.v`、`netlist.json`、`flattened.cdl`
- `render --static-cmos` 单级画 PUN/PDN 互补图，多级画门级静态单元图 + 行为等价逻辑符号；多级门级图的 AND/NAND/NOR 等直接复用 circuitikz 标准端口（AOI/OAI 等无单端口标准符号时才退回方块）；`multi synth` 的两级网表自动用输入轨/乘积项阵列，多级用局部通道总线

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

#### 多输出电路：译码器只是一个 spec 实例

工具没有内置 `decoder` 特例；多输出电路统一走 `multi synth`：
输入变量和多个输出表达式写在 JSON 里，Yosys/ABC 把它们当**一个多输出网络**综合，
共享输入反相器、fanout 和中间逻辑，再把每个标准单元展开成 CDL，
用现有的 `analyze_switch` 对**所有输出**做全输入 switch-level 证明。两个输出写成同一个表达式时 Yosys 会把它们合成一个 bit,展开时按别名处理:导出的 `.SUBCKT` 保留两个端口,并写 `*.ALIAS Y1 Y0`;仓库自己的 `cdl solve` 会读取该行,回读时两个输出都有真值表。同一个 net 在 SPICE 里无法写成两个端口,因此导出文件额外用 `V__alias_Y1 Y1 Y0 0` 这个 0V 源把两个端口短接,ngspice 等模拟器也能看到正确连接(`*.ALIAS` 是本仓库解析器用的注释)。

`tests/verify/instances/specs/decoder38.json`(仓库自带,可直接复现)：

```json
{
  "name": "decoder3to8",
  "inputs": ["A", "B", "C"],
  "outputs": [
    {"name": "Y0", "expr": "~A&~B&~C"},
    {"name": "Y1", "expr": "~A&~B&C"},
    {"name": "Y2", "expr": "~A&B&~C"},
    {"name": "Y3", "expr": "~A&B&C"},
    {"name": "Y4", "expr": "A&~B&~C"},
    {"name": "Y5", "expr": "A&~B&C"},
    {"name": "Y6", "expr": "A&B&~C"},
    {"name": "Y7", "expr": "A&B&C"}
  ]
}
```

```bash
./build/dcstudy multi synth --spec tests/verify/instances/specs/decoder38.json \
  --out out/decoder38 --png --json
```

输出：

```text
multi-output static CMOS: decoder3to8
  54T  (27 NMOS + 27 PMOS), 11 cells
  outputs: Y0, Y1, Y2, Y3, Y4, Y5, Y6, Y7
  ...
  switch-level: PASS
```

- 这是 **3 个共享 INV + 8 个 NOR3**，不是 8 个独立单输出电路；
- 所有输出一起经过 CDL flatten / Z3 switch-level 验证；
- 两级网络自动使用**输入/反相轨 + 乘积项阵列**（Mano 图 5-8 的画法）：A/A'、B/B'、C/C' 是竖直轨，8 个 NOR3 排成行，每个门从对应三根轨取输入；
- 多级网络使用**局部通道总线**：每个逻辑层占独立行带，信号在相邻列之间的通道里竖直走线，主输入在层间空隙引到该层，不把中间信号抬到整个图的上方；
- 实心点表示连接,交叉无点表示不连接;输出按 `Y0..Y7` 排序,避免 Yosys 内部单元顺序影响可读性;
- `--max-fanin 2` 同样适用于多输出综合;
- JSON 里的 `kappa_linear` / `pun_dual` 不是白名单的回声,而是**在映射实际用到的单元 CDL 网表上量出来的**:κ 线性判"单元输入不落在沟道端"(无 pass 管),对偶判"每一行恰好单轨驱动"(不浮空、不打架)。`static_cmos` 块给出 `scope`、名字白名单 `whitelist`、量过的 `checked` 类型、失败的 `failed` 类型和合成出来的 `class`。这两项测量是**必要而非充分**条件(CPL 的 `XOR2` 既门控又每行单轨,却没有对偶的 PUN/PDN),所以 `class` 仍要求白名单;单级路径的同一组字段由构造出的 PUN/PDN 网表量出。

#### 未定义输出（`any`）：4-2 编码器

讲义里的 4-2 编码器只在输入为独热码时才有定义，其他输入行是 X（任意值）。spec 的每个输出可以带一个 `"any"` 掩码：宽度等于真值表（`2^n` 行），`1` 表示该行**任意 0/1 都可以**。格式和 `--tt` 一样，支持 `0/1` 串或 `0x` 十六进制。

`tests/verify/instances/specs/encoder42.json`（4 个输入 `A3..A0`，A3 为 MSB；独热行是 1、2、4、8）：

```json
{
  "name": "encoder4to2",
  "inputs": ["A3", "A2", "A1", "A0"],
  "outputs": [
    {"name": "Y0", "expr": "A1|A3", "any": "0xFEE9"},
    {"name": "Y1", "expr": "A2|A3", "any": "0xFEE9"}
  ]
}
```

```bash
./build/dcstudy multi exact --spec tests/verify/instances/specs/encoder42.json --json
./build/dcstudy multi synth --spec tests/verify/instances/specs/encoder42.json --json
./build/dcstudy multi path  --spec tests/verify/instances/specs/encoder42.json --max-cost 64 --json
```

| 命令 | 结果 | 说明 |
|---|---|---|
| `multi exact` | **8T**（0 反相器） | PDN 被选成 `A0\|A2`、`A0\|A1`，在 X 行上取了一个更便宜的扩展 |
| `multi synth` | **8T**（2 个 NOR2） | 先把 X 行补成**单级静态门最便宜**的扩展（`~(A0\|A2)` / `~(A0\|A1)`），再交给 Yosys/ABC |
| `multi path` | **7T** | 用同一扩展做双轨路径搜索 |

- `multi exact` 的最优性证明**只覆盖 care 行**：X 行允许任何 0/1，所以 PDN 的最小 DNF 是在“必须导通的 care 行 + 可任意覆盖的 X 行”上求的；JSON 里 `any` 会回显掩码，`truths` 是实际扩展。如果必须导通的行是空集（只约束了“高电平”方向），搜索不会当成常量，而是从 X 行里挑一个最便宜的真实 cube，所以 `Y=~A` + 只要求 `A=0→1` 仍能得到 2T 反相器。
- 补全只看 care 行：表达式在 X 行上的取值不影响结果（`Y=A^B` 和 `Y=~A` 在同一个掩码下会补全成同一个 2T 实现）。
- `multi synth` / `multi path` 需要全函数，因此先用**单级静态门代价最小**的扩展补全（枚举反相器集合 + 精确最小 DNF，选最便宜的那个扩展），不是只按字面量数；验证时 care 行必须匹配、X 行只要求被驱动到 0 或 1（Z/X 不算合法扩展）。
- `--tri-state` 不接受 `any`：三态的“Z”不是“任意 0/1”的扩展，语义不同，命令会直接报错。
- 每个输出至少要有**一行 care**：全 1 的 `any`（输出完全任意）会被直接拒绝，而不是让下游映射出常量空网表。
- 不带 `any` 的 spec 行为完全不变。

### 自动组合：multi compose

`multi compose` 直接从 spec 构造网络：对每个输出建 reduced ordered BDD，每个非平凡节点实例化一个 `mux2` 原子（`S?B:A`），再交给 `--rtl --atoms` 流程映射、计成本和证明。

`--out DIR --png` 会同时出原生门级图（`DIR/scheme-0.svg`、`scheme-0.png`、`index.html`），不需要手动跑 `yosys show`/`dot`。任意网表也可以直接渲染（优先原生盒子，失败才回退 Graphviz）：

```bash
./build/dcstudy render --netlist out/compose.v --atoms out/mux2.v --png --out out/render
```

```bash
./build/dcstudy multi compose \
  --spec tests/verify/instances/specs/mux4_3bit.json --json
# {"nodes":9,"total":108,"atoms":[{"module":"mux2","instances":9,"transistors":12}],"verified":true}
```

- BDD 变量顺序 = spec 的 `inputs` 顺序；mux 类函数把选择信号放前面、数据放后面。
- 单字面量节点不实例化 mux（`v?1:0` 直接接 `v`，`v?0:1` 接 `~v`），所以 4:1 选择器正好是每位 3 个 mux2。
- 验证：≤12 输入走 spec 真值表 miter；13–16 输入用「穷举自检（BDD 对 spec 全行）+ 结构 miter（映射网表对 BDD 行为参考）」。
- spec 输入上限：compose 为 16，其他 `multi` 子命令仍为 12。

#### 原子库搜索：`--library`

带上 `--library` 时，`multi compose` 不再直接落成 mux2 网络，而是在原子库里**自动选原子、自动分解**：

```bash
./build/dcstudy multi compose --spec bench/fixtures/prio8.json \
  --library tests/verify/instances/specs/library.json --json
# 66T = 2x pe42 + or4 + 2x mux2 胶合, verified
```

- 每个原子先按现有后端实测成本（≤4 输入用单级静态精确解，其余走 Yosys 映射）；成本按实例计一次，胶合用 BDD→mux2 网络，最后仍由 Yosys miter 证明**实际写出的网表**。
- 原子输出按**函数形状**匹配：去掉函数不依赖的输入、枚举变量重命名取字典序最小真值表作签名。签名相同即同一函数，所以端口顺序/变量名不同的原子也能对上（`A&~B` 可匹配 `~X&W`，并把实例接成 `W,X`）。
- 搜索是「种子 + 贪心」：空计划、各精确覆盖（原子输出恰好等于某个 spec 输出，胶合里只需一根线）以及两两不相交的覆盖作为起点；每步加冲突下降最多的原子（第二种模式按每晶体管收益排序），先做一步前瞻评估「种子 + 一个原子」，最后删掉多余的实例。
- 组宽度不限（`n = k·g` 只是常见情形；混合宽度、部分输入不接原子也可以），允许原子共享输入；库里名为 `mux2` 的模块必须是真正的 2:1 mux，否则跳过（胶合网络就实例化这个模块）。
- `--time-budget-ms N` 给搜索一个墙钟上限：到期返回当前最好的完整方案并置 `timed_out: true`；一个方案都没有就回退到纯 mux2 网络（同样 verified）。
- JSON 增加 `mode`（`library` / `atom` / `bdd-mux2`）、`plan`（每个实例的模块、输入、输出端口）、`timed_out`、`time_budget_ms`。
- `--abc`：把胶合网（mux2 网络）交给**独立进程** ABC 做结构等价变换（默认 `balance; rewrite -l; refactor -l; resub -K 6 -N 2; drw -l`，不跑 amap/liberty），再回到原映射与 miter 口径：胶合因此变成标准单元（`cell_cost`），不再是按个计费的 mux2 原子。胶合大时收益明显：hex7seg_sel + 同一库，39 个胶合 mux2（468T）经 ABC 后映射到 186T 标准单元，总 546T → 258T，verified；prio8 的胶合本来只有 2 个 mux2，改写后 22→7 个 `.names`、总成本仍 66T。代价是三次 Yosys + 一次 ABC（约 +70ms）。
- 改写前后再做一次 SAT 等价证明（胶合 ≤12 信号，miter 很小），失败同样回退，避免「节点数对了但函数改错了」；这一步约 +30ms。
- ABC 失败、超时或把网表变大时回退到内联 mux2 胶合，`abc.reason` 给出原因（机器可读）；`--abc-timeout` 控制每步超时（默认 5s），`--abc-commands` 覆盖脚本。没有 `--library` 时 `--abc` 不会改写纯 BDD 网络，JSON 里也会说明。
- JSON 另有 `glue`（`mux2` / `abc` / `direct`）、`glue_estimated`（搜索估价，只用于选方案）和 `abc`（`version` / `commands` / `nodes_before` / `nodes_after` / `reason`）；最终成本以实测的 `atom_cost` / `cell_cost` 为准。

#### 复用自定义原子组件：`--rtl` + `--atoms`

`multi synth` 也可以直接吃一份**层次化 RTL**，并把用户模块当成原子单元：

```bash
./build/dcstudy multi synth --rtl top.v --atoms atoms.v --spec spec.json \
  --out out/rtl8
```

- `atoms.v` 里是原子模块（例如 4-2 优先编码器 `pe42`）：读取时按**黑盒**处理，ABC 只映射剩余的胶合逻辑，不会把原子拆开；
- 每个原子的成本**实测**：把该模块单独映射到同一 Nangate45 库，再数 CDL 晶体管，不写死常数；
- 胶合逻辑映射到静态标准单元，`total = Σ 实例数×原子成本 + 胶合晶体管`；
- 验证：对 spec 的每个输出生成 wrapper，用 Yosys miter（`sat -prove mismatch 0`）证明 RTL 组合与真值表一致；
- 输入数上限 12（受参考真值表限制），`--spec` 不能带 `any`，原子必须是叶子模块（不支持原子内再嵌原子）。

8-3 优先编码器（两个 `pe42` + 胶合逻辑）实测：

```text
multi synth (RTL + atoms): 66T (32T atoms + 34T glue)
  pe42 x2  16T each
  miter: PASS
```


- `--emit-atom NAME` 把本次生成的行为级模块（不是映射后网表）随 JSON 输出，下游用 `--atoms -` 从 stdin 读入，shell 管道直接串联：

  ```bash
  ./build/dcstudy multi synth --spec tests/verify/instances/specs/encoder42.json --emit-atom enc42 --json \
    | ./build/dcstudy multi synth --rtl top.v --atoms - --spec top.json --out out/chain
  ```

  下游把读到的模块重新映射并测成本；`--atoms -` 读 `atom_library`（累积库：stdin 库 + 本级模块），因此可以任意多段串联：

  ```bash
  spec --emit-atom pe42 --json \
    | top8.v  --atoms - --emit-atom pe8  --spec prio8.json --json \
    | top8c.v --atoms - --spec prio8.json --json
  ```

  每级原子的成本按“它的子原子也是黑盒”递归测量，与它上一级被计费的口径一致（pe8 = 2×16T 原子 + 34T 胶合 = 66T），miter 仍 PASS。

- `--ref FILE` 改用结构 miter 验证（不需要 `--spec`）：参考文件里的第一个 `module` 与 `top` 同端口，逐输出比较后由 Yosys SAT 证明 `mismatch = 0`。端口从映射网表读取，输入数只受 128 端口上限约束。16-4 优先编码器（4×pe42）由此得到 172T（64T 原子 + 108T 胶合）且 PASS。
- 端口校验：`top` 的端口名、方向、位宽必须与 `--spec`/`--ref` 完全一致，不一致直接报错（Yosys 会静默补零/截断，否则只验证了部分输入）。
- 原子实例参数（如 `#(.INVERT(1))`）参与计费：测量时用 `chparam` 应用参数，与默认参数分开缓存；`--max-fanin` 在 RTL 模式同样生效。
- `--emit-atom` 输出的 `atom_library` 包含输入的原子库文件 + 本级模块，多级管道不会丢子原子。

注意：**组合关系（实例化哪些原子、怎么连）仍由 RTL 表达**，工具优化的是胶合逻辑、映射和成本核算；自动搜索「用哪些原子、怎么分解」是下一步（16-4 还需要 16 输入的真值表/参考，超过当前 12 的上限）。

#### 严格最优：单级静态复杂 CMOS

`multi synth` 是 Yosys/ABC 的启发式映射，不证明最优。严格版本用 `multi exact`：

```bash
./build/dcstudy multi exact --spec /tmp/decoder38.json --out /tmp/decoder-exact --json
```

优化模型是明确限定的：

- 每个输出一个**单级静态复杂 CMOS 门**（PDN + 对偶 PUN）；
- PDN 是输入字面量的精确最小 DNF；
- 输入反相器是共享的 2T 反相器；
- 不允许 TG/pass-transistor、不允许门间共享、不允许输出反相器。

优化器穷举全部 `2^n` 个反相器集合，并对每个输出求精确最小 DNF，因此在这个模型内是**带最优性证明**的。有一个快捷路径：如果每个输出恰好是单个 cube，PDN 字面量和反相器集合都被唯一确定，就不必再跑 `2^n` 枚举。JSON 里用 `proof_method` 区分 `exhaustive` 与 `analytic_shortcut`，`proof_scope` 固定写 `single-stage static CMOS model`，不会把两者混成一个 “proof”。3-8 译码器（单 cube，走 analytic 分支）：

```text
optimal: 54T
  48T gates + 6T shared inverters
  27 NMOS + 27 PMOS
  proof: analytic -- every output is a single cube, so the
         minimum PDN literals and inverter set are fixed
  switch-level: PASS
```

`--out` 会写出 `flattened.cdl` 和 `exact.json`。

#### 例：16 进制七段数码管（`tests/verify/instances/specs/hex7seg.json`）

4 位输入 `A,B,C,D`(A 为 MSB),7 个高有效段输出 `seg_a..seg_g`。显示字形为 `0-9,A,b,C,d,E,F`。

```bash
# 静态多级标准单元:44 个 2 输入单元,192T,逐输出 switch-level PASS
./build/dcstudy multi synth --spec tests/verify/instances/specs/hex7seg.json \
  --out /tmp/hex7seg-static --max-fanin 2 --png --json

# 不限制 fan-in:31 个单元,162T(用到 AOI21/OAI221/NAND4 等复杂门)
./build/dcstudy multi synth --spec tests/verify/instances/specs/hex7seg.json \
  --out /tmp/hex7seg-default --json

# 单级复杂 CMOS(定制门模型):共享 4 个输入反相器,180T,模型内 proof
./build/dcstudy multi exact --spec tests/verify/instances/specs/hex7seg.json \
  --out /tmp/hex7seg-custom --json
```

当前结果:

| 实现 | 模型 | 晶体管 | 验证 |
|---|---|---|---|
| 静态标准单元(`--max-fanin 2`) | Yosys/ABC + Nangate45,全部 ≤2 输入 | **192T**(96N + 96P)* | 逐输出 switch-level PASS |
| 静态标准单元(库默认 fan-in) | 同上,允许 AOI/OAI 复杂门 | **162T**(81N + 81P)* | 逐输出 switch-level PASS |
| 定制单级复杂 CMOS | 每输出一个 PDN/PUN,共享输入反相器 | **180T**(90N + 90P) | 模型内 proof + PASS |

\* 这两行的数字依赖 Yosys/ABC 版本(另一个 Yosys 0.52 + 同一 PDK 的组合给出 47 单元/194T,功能验证同样通过),集成测试因此只断言扇入契约与预算上界,不钉死单元数。

“定制”在这里指**定制复杂门**,不是 pass-transistor 全定制。三种结果放在一起看:标准单元多级映射(162T)比“每个输出压成单个复杂门”的 180T 更省晶体管,说明单级复杂门模型是保守模型,价值在于它是**可证明最优**的;`--max-fanin 2` 的 192T 则是把单元库限制到 2 输入后的代价。无三态的 `multi path` 现在会先走 PUN/PDN 双轨路径搜索;对这个七段 spec,双轨搜索目前仍未在 45 秒预算内完成,上面三个静态结果都是可用的无三态实现。对 1-to-4 分路器和 3-to-8 译码器,双轨搜索给出 18T / 44T 的无三态网络(整图联合电气校验通过)。

#### 三态输出：`--tri-state`

三态不是单独的译码器命令，而是路径求解器的一个 flag：

```bash
./build/dcstudy multi path --spec decoder38.json --tri-state --max-cost 40
```

语义：输出表达式为 1 的行驱动高电平，为 0 的行变为高阻 Z。求解器仍然以唯一驱动、强驱动和路径图验证为约束，只是允许 `|D|=0`。

- 3-8 译码器：通用路径搜索找到 **16T** 的输入源选择网络；
- 1-to-4 分路器：**6T**；
- 同一个 spec 不加 `--tri-state` 时走 PUN/PDN 双轨搜索：3-8 译码器 **44T**、1-to-4 分路器 **18T**，每个输出始终驱动 0/1；输出端不再有零成本“导线”连接，整张网络必须通过联合电气校验（每个输出每行恰好一个强驱动源），共享前缀版失败时退回每输出私有节点再查一次；
- `multi path` 的 `proof` 只覆盖**保留的候选路径集合**：JSON 带 `proof_scope: "retained candidate path set"`。候选集做了有界 Pareto 去重（同成本、非边集包含）和串联链顺序归并，不是完整设计空间，所以不要把它读成全局最优；
- 经典 14 TG + 3 INV 的 TG demux 树是 34T 的一个结构化实现，但不是唯一解。

标准画法参考：Mano《Digital Design》3-to-8 decoder 图（3 个反相器提供补码，8 个与门产生 minterm），以及 KFUPM EE200 讲义中的同款画法；低有效输出版本通常直接用 8 个 NAND 门。这里因为走静态互补 CMOS，所以实际单元是 NOR3（与 AND3 在 De Morgan 意义下等价）。

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

### 数学 worker

重型数学库都留在 `dcstudy-z3` worker 里,主程序不链接它们:

```bash
./build/dcstudy math closure --n 4 --edges 0-1,1-2,2-3
./build/dcstudy math gf2 --rows '1,1;0,1'
```

- `closure`: GraphBLAS 布尔半环 `(OR, AND)` 的可达闭包;
- `gf2`: FLINT 在 GF(2) 上的秩/零空间;
- `symmetry`: bliss 输入自同构群(内部使用)。
- `cdl solve --graphblas` 可以把开关级连通闭包切到 GraphBLAS worker;默认仍用本地 bitset BoolMatrix(小网表更快)。

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
├── include/              公共头文件(按模块, 不再有 dcstudy/ 子目录)
│   ├── core/             bits/boolfunc/boolmatrix/cdl/cmos/designspace/
│   │                     driver_profile/json/limits/parallel/partial/verilog/verify
│   ├── synthesis/        static_exact/multi_path/multistage/path_search/synthesis/template_synth
│   ├── switch/           switchlevel/bdd_switch
│   ├── z3/               z3_protocol/z3_solvers/math_engine/symmetry
│   └── cli/              worker_client/render_client/math_client/z3_client
├── src/                  按模块分组
│   ├── cli/              main + worker 客户端(math/render/z3/generic)
│   ├── core/             布尔函数/CDL/CMOS/设计空间/limits/verilog/verify
│   ├── synthesis/        精确覆盖/模板/多级/多输出路径
│   ├── switch/           开关级不动点 + BDD 路径
│   └── z3/               Z3 worker: 入口/模型/对称/bliss/math 引擎/协议
├── workers/
│   ├── render_worker.ts   TS/Node SVG/HTML 渲染 worker(默认)
│   └── render_worker.py   Python 渲染 worker(对照/fallback)
├── tests/
│   ├── unit/             Catch2 单元测试(按模块: core/switchlevel/cdl/infra)
│   ├── smoke/            CLI 烟测 + Fil-C 内存安全冒烟(Z3-free 内核)
│   ├── integration.sh    黑盒 CLI / 形式工具集成测试
│   └── verify/           验证目录: instances(实例) / formal(核心/可选)
├── docs/                 四本 HTML 讲义(见上)
│   └── lecture-rtl-examples/  讲义用 XOR 写法示例(不是测试/工具输入)
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
| `include/` `src/` `tests/` `workers/` `tools/` `docs/lecture-rtl-examples/`(代码) | [GPL-3.0-only](LICENSE) |

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
