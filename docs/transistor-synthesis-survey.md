# 晶体管级逻辑综合相关实现调研

> 目标：确认“从多输出逻辑自动生成晶体管级网络”这件事，学界和工业界已经做到什么程度；本文附可追溯来源。
>
> 检索方式：基于网络检索和正文片段抓取，自动语义判断置信度不高，以下结论按抓到的摘要/片段人工整理。链接可能随时间失效。

## 1. 结论先行

不是空白领域。最接近本项目目标的是：

- **DATE 2025：Hybrid Exact and Heuristic Efficient Transistor Network Optimization for Multi-Output Logic**
  明确的**多输出、晶体管级、SAT 精确优化 + 启发式加速**。
- **MOTO-X（2016）**
  多输出晶体管级综合，允许 bridging，不局限 series-parallel。
- **MiniTNtk**
  小规模精确晶体管网络最小化，面向 4 输入 P-class。
- **PTM**
  基于 ROBDD 的 pass-transistor technology mapper。
- **Stanford BDD/PTL 报告**
  BDD/ZDD 到 pass-transistor 网络、低功耗与 domino 结构。
- **IRSIM / COSMOS**
  开关级仿真，可处理双向开关、电荷和 MOS 网络语义。

本项目的差异主要在**集成方式**：

- 把 kappa/开关图、C0*/C1* 驱动集、|D| 约束、GraphBLAS 批量闭包放在同一条教学工具链；
- 有静态 54T 的模型内证明；
- 有三态 16T、动态 43T 等具体结构；
- 但通用任意拓扑多输出精确综合，成熟度不如 DATE 2025 / MOTO-X / MiniTNtk。

## 2. 最接近的工作

### 2.1 Hybrid Exact and Heuristic Transistor Network Optimization for Multi-Output Logic

- 作者：Lang Feng, Rongjian Liang, Hongxin Kong
- 会议：DATE 2025
- 来源：
  - https://ieeexplore.ieee.org/document/10992735
  - https://research.nvidia.com/labs/electronic-design-automation/publication/liang2024exact/

摘要片段：

    propose an exact transistor network optimization algorithm that supports multi-output logic and is formulated as a SAT problem

    most previous works focused on single-output networks or heuristics only

    hybrid process that uses a heuristic algorithm to predict the solution range as guidance

对应到本项目：

- 我们的 NetworkTemplate / EdgeChoice 是变量模板的简化版；
- 它把多输出 + 晶体管级 + SAT 精确性做成了完整方法；
- 我们的启发式早停、路径搜索和它方向一致，但还没有形成同等级的 SAT 拓扑/流编码。

### 2.2 MOTO-X: A Multiple-Output Transistor-Level Synthesis CAD Tool

- 作者：Dimitri Kagaris
- 期刊：IEEE TCAD, 2016
- 来源：
  - https://dl.acm.org/doi/abs/10.1109/TCAD.2015.2448675
  - https://ieeexplore.ieee.org/document/7131515

摘要片段：

    tries to minimize the number of transistors required to implement a given multiple-output logic function

    goes beyond the traditional series-parallel design style and allows for extensive bridging

对应到本项目：

- 我们的 path/template 仍以串联路径和共享前缀为主；
- MOTO-X 明确允许非串并联/桥接结构，这正是“任意拓扑”方向；
- 如果要做真正一般化，需要类似它的晶体管网络图变量，而不是只选路径。

### 2.3 MiniTNtk

- 标题：MiniTNtk: An Exact Synthesis-based Method for Minimizing Transistor Networks
- 来源：
  https://umji.sjtu.edu.cn/~wkqian/papers/Xiao_Han_Yang_Yang_Zheng_Chen_Liang_Li_Qian_MiniTNtk_An_Exact_Synthesis-based_Method_for_Minimizing_Transistor_Networks.pdf

片段：

    first method that achieves the optimal transistor networks for a set of Boolean functions with known optimal solutions

    reduces the number of transistors by up to 9.39% over all 4-input P-class representative functions

对应到本项目：

- 我们的 multi exact 只覆盖单级静态复杂 CMOS 模型；
- MiniTNtk 是更完整的 exact transistor network 结果；
- 适合作为我们小规模 exact 的对照基线。

### 2.4 PTM: A Technology Mapper for Pass-Transistor Logic

- 来源：
  http://www.ee.ic.ac.uk/pcheung/publications/PTM_CDT98.pdf

片段：

    employs optimized ROBDD

    BDD ordering using Genetic Algorithm

    pass-transistor based technology mapping

对应到本项目：

- design bdd 已经是 ROBDD 到 TG 选择 DAG；
- PTM 进一步做 BDD ordering 搜索和 pass-transistor 工艺映射；
- 我们的输入序枚举是 PTM 思路的极小规模版本。

### 2.5 Decision Diagrams and Pass Transistor Logic Synthesis

- Stanford CSL 技术报告：
  http://infolab.stanford.edu/pub/cstr/reports/csl/tr/97/748/CSL-TR-97-748.pdf

片段涉及：

- BDD/ZBDD-based pass-transistor synthesis；
- NMOS/CMOS pass gate；
- 阈值损失、ratioed logic、domino；
- 低功耗 pass-transistor 结构。

对应到本项目：

- 我们的 TG/BDD 构造和它同源；
- 但我们的验证层更偏 C* 驱动集和 |D|；
- 该报告对 pass-transistor 的电气限制讨论更完整。

### 2.6 IRSIM 和 COSMOS

IRSIM：

- 主页：http://opencircuitdesign.com/irsim/
- 论文：https://dl.acm.org/doi/abs/10.1145/74382.74412
- 片段：

    switch-level simulator ... treats transistors as ideal switches

COSMOS：

- https://kilthub.cmu.edu/articles/journal_contribution/COSMOS_A_Compiled_Simulator_for_MOS_Circuits/6604532

对应到本项目：

- cdl solve / SwitchGraph 的开关级验证思路与它们同类；
- IRSIM/COSMOS 更成熟，包含电容/电阻/时序近似；
- 我们是布尔开关级 + Z3 + 驱动集分析，不是模拟器。

## 3. 其他相关方向

### 标准单元/门级综合

- ABC：https://people.eecs.berkeley.edu/~alanmi/abc/
- Yosys：https://yosyshq.net/yosys/
- mockturtle：https://github.com/lsils/mockturtle

这些工具强在：

- 多输出逻辑优化；
- technology mapping；
- 标准单元库；
- 形式验证/等价检查。

它们**不直接生成任意晶体管拓扑**，通常输出门级网表或标准单元网表，晶体管来自单元库展开。我们的 multi synth 正是这个层次。

### 晶体管级 SAT/形式模型

检索中出现了晶体管级 SAT 模型和 channel-connected component 的专利/论文，例如：

- US10402525：transistor-level satisfiability models
- TRAP：transistor-level programmable fabric SAT

这说明“用 SAT 描述晶体管网络并验证”本身也不是新想法；区别在于具体网络模型、变量编码和优化目标。

### 三态/动态译码器

三态 TG demux 树和 domino 译码器属于经典电路结构：

- 3-8 三态 TG 树：14 TG + 3 INV = 34T（通用 tri-state path 搜索还可找到 16T 的输入源选择网络）；
- domino/动态版本：需要预充/keeper/时钟语义；
- 它们不是静态互补 CMOS，不能直接套 --static-cmos 的最优性模型。

## 4. 和本项目的对比

| 能力 | 现有工作 | 本项目 |
|---|---|---|
| 多输出晶体管精确优化 | DATE 2025, MOTO-X, MiniTNtk | multi exact 仅单级静态；multi path/template 有界 |
| 任意/桥接拓扑 | MOTO-X, SAT 工作 | 固定模板边选择，未自由生成 |
| BDD 到 pass-transistor | PTM, Stanford PTL | design bdd |
| 多输出共享边 | DATE 2025, MOTO-X | multi path, multi template |
| 开关级验证 | IRSIM, COSMOS, SAT switch-level | Z3 + C* + GraphBLAS |
| 三态译码器 | 经典结构 | multi path --tri-state 16T |
| 动态/预充 | 经典 domino | 尚未实现完整时钟语义 |
| 管级渲染 | Magic/Xschem 等 | 自研 SVG/门级/轨矩阵；多输出管级图未完成 |
| 静态 54T 证明 | 可对照 | multi exact 模型内 proof |

## 5. 值得直接借鉴的点

1. **DATE 2025 的 SAT 编码**
   多输出晶体管网络 + exact + heuristic bound，是最直接的上位替代/参考。

2. **MOTO-X 的 bridging/非串并联**
   如果目标真是任意拓扑，不能只做 path/template；需要图级变量和桥接。

3. **MiniTNtk 的 exact benchmark**
   用已知最优的小函数集校准我们的 proof。

4. **PTM 的 BDD ordering**
   我们目前只枚举输入序；PTM 用 GA 处理更大函数。

5. **IRSIM/COSMOS 的开关级语义**
   电荷共享、浮空、弱驱动、时序；我们目前是布尔/驱动集抽象。

## 6. 本项目的定位

更准确的说法不是“网上没有类似实现”，而是：

> 我们把晶体管级精确综合、开关级路径验证、多输出共享、三态/动态结构和教学渲染集成在一个小工具里；但在通用多输出任意拓扑精确综合上，已有 DATE 2025、MOTO-X、MiniTNtk 等更成熟的工作。

当前比较有价值的差异化点：

- C0*/C1* 驱动集和 |D| 良构性；
- GraphBLAS 批量闭包；
- 静态/三态/动态几种语义放在同一套分析框架；
- 可控的教学级渲染。

## 7. 参考来源

1. L. Feng, R. Liang, H. Kong. Hybrid Exact and Heuristic Efficient Transistor Network Optimization for Multi-Output Logic. DATE 2025.
   https://ieeexplore.ieee.org/document/10992735
   https://research.nvidia.com/labs/electronic-design-automation/publication/liang2024exact/

2. D. Kagaris. MOTO-X: A Multiple-Output Transistor-Level Synthesis CAD Tool. IEEE TCAD, 2016.
   https://dl.acm.org/doi/abs/10.1109/TCAD.2015.2448675
   https://ieeexplore.ieee.org/document/7131515

3. MiniTNtk: An Exact Synthesis-based Method for Minimizing Transistor Networks.
   https://umji.sjtu.edu.cn/~wkqian/papers/Xiao_Han_Yang_Yang_Zheng_Chen_Liang_Li_Qian_MiniTNtk_An_Exact_Synthesis-based_Method_for_Minimizing_Transistor_Networks.pdf

4. PTM: A Technology Mapper for Pass-Transistor Logic.
   http://www.ee.ic.ac.uk/pcheung/publications/PTM_CDT98.pdf

5. Decision Diagrams and Pass Transistor Logic Synthesis. Stanford CSL-TR-97-748.
   http://infolab.stanford.edu/pub/cstr/reports/csl/tr/97/748/CSL-TR-97-748.pdf

6. A. Salz, M. Horowitz. IRSIM: An Incremental MOS Switch-Level Simulator. DAC 1989.
   https://dl.acm.org/doi/abs/10.1145/74382.74412
   http://opencircuitdesign.com/irsim/

7. COSMOS: A Compiled Simulator for MOS Circuits.
   https://kilthub.cmu.edu/articles/journal_contribution/COSMOS_A_Compiled_Simulator_for_MOS_Circuits/6604532

8. ABC.
   https://people.eecs.berkeley.edu/~alanmi/abc/

9. Yosys.
   https://yosyshq.net/yosys/

10. mockturtle.
    https://github.com/lsils/mockturtle

11. Transistor-level satisfiability models. US10402525.
    https://patents.google.com/patent/US10402525B1/en

## 8. 说明

- 本文不是完整 survey，只覆盖与本项目目标直接相关的方向。
- 自动检索的语义置信度有限；引用片段来自检索结果，未逐篇通读全文。
- DATE 2025 和 MOTO-X 的细节应以论文正文为准；本文只按摘要/片段归纳。
