# digital-circuit-study

> 从一张 4 个管子的 NOR 图出发,一路走到用真实 PDK 量出成本函数。
> From a 4-transistor NOR gate to measured cost functions on real PDKs.

三本 HTML 讲义 + 一套可复现的脚本 + 全部原始数据来源说明。

---

## 讲义

| # | 文件 | 讲什么 | 前置 |
|---|---|---|---|
| 0 | `intuition-zero.html` | **从一个 NOR 出发** — 直觉篇。10 步「观察 → 提问」,自己走出一套代数 | 只要知道 MOS 能当开关 |
| 1 | `formal-analysis.html` | **从电路到代数** — 正向:电路 → 函数。矩阵、布尔闭包、良构性判据 | 矩阵乘法 |
| 2 | `synthesis-inverse.html` | **从代数到电路** — 逆向:函数 → 电路。单调性判据、Dedekind 数、纤维与成本。**第 8 章是全部实测数据** | 读完讲义一 |

`index.html` 是总目录页。启用 GitHub Pages 后可直接在线阅读。

---

## 已经用真实工具量出来的结论

| 结论 | 证据 |
|---|---|
| 晶体管数 $2L$ 公式**只对单调函数**成立 | Nangate45 CDL:9 个单调单元全中,XOR2 一个失手(预测 12,实测 **10**) |
| 单级 CMOS 的可达集 **= Dedekind 数** | 枚举实测:3, 6, 20, 168, 7581;$n=6$ 时占比 $4\times10^{-13}$ |
| 非单调函数的最少管数**取决于设计空间** | 枚举 + Verilator 判定:**XOR2 = 6T(2 种拓扑)**、**AND2 = 5T**、NAND2 = 4T |
| 「最少管数 = 最小面积」**并非普适** | Nangate45 满足 $0.133(k+2)$;ICsprout55 每管面积 0.54→0.24 乱跳 |
| 面积对**阈值电压完全瞎** | 同一 XOR2 三档 VT 面积都是 3.96;漏电差 **104×**,延时差 2.4× |
| 「换 HVT 省功耗」**不无条件成立** | GCD 实测:HVT 漏电降 64×,但总功耗**反升 22%**(漏电只占 0.05%) |
| 工具的成本函数里**没有漏电** | 三库并列喂 ABC,无约束/紧约束/松约束都选 HVT —— 靠 tie-break 乱挑 |
| 同一 RTL,**面积可差 50%** | 裸 `abc -liberty` 给 1.596;完整 `synth` 给 2.394(延时驱动升级) |

---

## 依赖

| 工具 | 已验证版本 | 用途 |
|---|---|---|
| [Verilator](https://verilator.org) | 5.054 | 开关级 / 门级的**功能判定**(独立第二实现) |
| [yosys](https://yosyshq.net/yosys/) | 0.66 | RTL → 门网表 |
| [iEDA](https://github.com/OSCC-Project/iEDA)(iSTA / iPA) | git 071226f | STA 延时 + 功耗 |
| [Nangate45](https://github.com/The-OpenROAD-Project/OpenROAD-flow-scripts) | 45nm 学术预测库 | **带 CDL** → 标准单元晶体管数的真相来源 |
| [ICsprout55 v2](https://github.com/openecos-projects/icsprout55-pdk) | 55nm 真实工艺 9T | SVT / LVT / HVT 三档 VT × 32 个角 |

> **两个 PDK 都不在本仓库内**(体积与许可原因)。脚本通过环境变量找它们。

### 装 Nangate45(只有它带 CDL)

本仓库的 `cdl_solver.py` 需要标准单元的 CDL 网表。推荐走 [yosys-sta](https://github.com/pku-liang/yosys-sta) 的预编译包:

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

### 1. 读一个标准单元的晶体管级网表

```bash
export PDK_ROOT=~/yosys-sta/pdk          # 或者直接给 NANGATE45_CDL
python3 scripts/cdl_solver.py
```

```
单元 XOR2_X1 : 10 管, 输出 ['Z']
  A B   自洽输出
  00    (0,)
  01    (1,)
  10    (1,)
  11    (0,)
  结论: ✓ 与 XOR2_X1 一致,CDL 解读正确
```

### 2. 枚举 → 生成 Verilog → Verilator 判定

```bash
python3 scripts/pipeline.py
```

生成的候选电路在 `out/cands/*.v`,每个都带自己的 testbench。**只有通过 Verilator 的才会被报告。**

### 3. Verilator 回归(含负对照)

```bash
cd verilog
verilator --binary --timing -Wno-fatal -Wno-WIDTH -Wno-UNUSEDSIGNAL \
          -Wno-DECLFILENAME --top-module tb -o sim xor6.v tb_xor6.v
./obj_dir/sim
```

`xor6.v` 里的 `BREAK=1` 是**故意接错**的版本(把 N3 的栅极错接到 GND),用来确认检查器真的会报错。

---

## 目录

```
.
├── index.html                  总目录页
├── intuition-zero.html         讲义 0  直觉篇
├── formal-analysis.html        讲义一 从电路到代数
├── synthesis-inverse.html      讲义二 从代数到电路
├── scripts/
│   ├── cdl_solver.py           读 CDL 的通用开关级求解器
│   ├── pipeline.py             枚举 → Verilog → Verilator
│   ├── design_backwards.py     最简版:单调函数 → 晶体管网络
│   └── enumerate_xor.py        早期枚举(已被 pipeline.py 取代)
├── verilog/                    Verilator 回归测试(含负对照)
├── rtl/                        实验用 RTL
└── tools/pdk/                  yosys-sta 的 PDK 描述文件
```

---

## 许可

**本仓库两种许可并存,按文件类型划分:**

| 部分 | 许可 |
|---|---|
| `*.html`(讲义文档) | [CC BY-NC 4.0](LICENSE-docs) |
| `scripts/` `verilog/` `rtl/` `tools/`(代码) | [GPL-3.0-only](LICENSE) |

> ⚠️ **请留意**:CC BY-NC **禁止商业性使用**,它不是 OSI 认可的开源许可。所以本仓库**整体上不是一个开源仓库** —— 只有代码部分是 GPL-3.0 开源。
> 两类文件是分开的,不构成混合作品;但如果你要商用,请注意讲义部分不可用于商业目的。

---

## 致谢

- 讲义里的公式与推导是重新组织的,但相关理论分别来自 **Boole(1854)**、**Shannon(1938,继电器与开关电路的符号分析)**、**Bryant(1984,switch-level model)**。
- 工具:[yosys](https://yosyshq.net/yosys/)、[Verilator](https://verilator.org)、[iEDA](https://github.com/OSCC-Project/iEDA)、[yosys-sta](https://github.com/pku-liang/yosys-sta)。
- 工艺库:imec / Nangate(学术用途)、[openecos-projects/icsprout55-pdk](https://github.com/openecos-projects/icsprout55-pdk)。

## 相关

- 单级 CMOS 可达集的一个经典结论是「单调函数」,其计数即 Dedekind 数,见 OEIS [A000372](https://oeis.org/A000372)。
- 精确综合(SAT/ILP)方向可以继续查 *exact synthesis* 相关文献。
