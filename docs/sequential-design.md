# 时序逻辑支持：需求基线（REQ-SEQ）

> 状态：**设计基线，未实现**。本文只定义需求与设计，不代表代码已经存在。
> 编码规则：所有需求用 `REQ-SEQ-NNN` 编号；提交信息、测试名、实例脚本和里程碑都引用这些编号。级别用 RFC 2119 中译：**必须 / 应当 / 可以 / 不得**。
> 与实现的关系：本文是 S0 开工前的基线；实现过程中若需求变化，先改本文再改代码（与仓库「文档与行为同 commit」的约定一致）。
> 核对方式：代码事实来自本仓库（已标注文件/函数）；外部材料在 §8 标注「本地核对 / 文档核对 / 待核对」，未核对的不写成结论。

## 1. 范围

### 1.1 目标

时序支持拆成两条独立线，编号互不依赖：

- **线 A（REQ-SEQ-0xx）FSM / 时序综合**：把时序问题化成「组合逻辑 + 触发器原语」。next-state 与输出都是 `(state, inputs)` 上的布尔函数，复用现有多输出后端；新增的是规格格式、复位可达性、触发器计价、多周期验证。
- **线 B（REQ-SEQ-07x）相位语义**：把 `|D|`、预充、动态节点扩到时间维度（DFF 内部 master–slave、CLK/CLK̄、采样与保持相位）。这是 Hindsight 里登记的开放项 `clock/precharge/dynamic-node semantics and |D| timing`，v1 不动。

### 1.2 非目标（明确不做，写进文档免得后面扯皮）

- **REQ-SEQ-080（不得 · 全程）** 不做多时钟域、CDC、时钟门控（ICG）、扫描链、锁存器（v1 仅有边沿触发器）。
- **REQ-SEQ-081（不得 · 全程）** 不建模 clock tree、STA、RC 延迟、功耗、亚稳态与建立/保持时间；成本仍只按 CDL 晶体管数累加。
- **REQ-SEQ-082（不得 · v1）** 不支持时序原子库（stateful atom）。`MultiProblem` 是纯组合真值表，装不下 `q' = f(q, x)`；接口草案见 §4.8，落地推到 S5。
- **REQ-SEQ-083（不得 · v1）** 不把触发器纳入严格静态 CMOS 类判定（κ 线性 / PUN 对偶只对组合成立）。

## 2. 术语

- **状态位 / state bits**：寄存器的位数 `k`，状态空间 `2^k`。
- **复位状态 / reset state**：复位有效后的确定状态，spec 必须给出。
- **可达状态**：从复位状态出发、在任意输入序列下能到达的状态集合；其余状态为不可达。
- **DC 行**：不可达 `(state, inputs)` 组合对应的真值表行，值任意但网表必须确定。
- **组合块**：next-state 逻辑 + 输出逻辑，不含触发器。
- **product machine**：两个 FSM 的状态与输入做笛卡尔积得到的机器，用于顺序等价。

## 3. 需求

### 3.1 规格与模型

**REQ-SEQ-001（必须 · S0）** FSM spec 必须是 JSON，至少包含：`name`、`state_bits`、`reset`（复位状态名或编码）、`inputs`、`state`（状态名到编码的映射）、`next`（每位一个布尔表达式或真值表）、`outputs`（每个输出一个布尔表达式或真值表）。

验收：`include/core/partial.hpp` 的 `MultiProblem` 能无损装下 next 位与输出；加载器对缺字段/位宽不符给出退出码 2。

**REQ-SEQ-002（必须 · S0）** 状态编码由 spec 显式给出（默认二进制），不得由工具隐式决定；实现另提供 `--encoding binary|onehot|gray` 作为**生成候选**，每个候选独立报成本（见 REQ-SEQ-023）。

**REQ-SEQ-003（必须 · S0）** 复位必须显式：复位状态与复位方式（同步 / 异步）都写在 spec 里；没有复位的 spec 必须被拒绝。

**REQ-SEQ-004（必须 · S0）** 输出按依赖自动区分 Moore / Mealy：只引用状态位即 Moore，引用输入即 Mealy；不引入单独的模式开关。

**REQ-SEQ-005（应当 · S0）** spec 允许为每个输出声明 `registered: true`（输出打一拍）；寄存器化输出的成本计一次触发器（REQ-SEQ-032）。

**REQ-SEQ-006（必须 · S0）** 组合块的真值表行数 = `2^(state_bits + inputs)`；超过当前后端可承受规模时（复用现有内存预算模型）必须在加载阶段报错，不得跑到 OOM。

### 3.2 分析与 don't-care

**REQ-SEQ-010（必须 · S0）** 必须实现「复位可达性」：从复位状态对所有输入做 BFS（或 BDD 前向遍历），得到可达状态集；实现必须给出 `reachable_states` 计数。

**REQ-SEQ-011（必须 · S0）** 不可达 `(state, inputs)` 行必须写进 `MultiProblem` 的 `any` 掩码（`PartialOutput.any`，现有机制），让 `multi exact/synth/path/compose` 把它们当 don't-care。

**REQ-SEQ-012（必须 · S0）** **不得**把不可达行当「真 X」传播到导出网表：写出网表前必须确定化（next 位取 0、Moore 输出取 0 或显式给定值），并在 JSON 报 `dc_rows`。

**REQ-SEQ-013（应当 · S0）** 应当报告「不可达状态数 / 总状态数」，并在渲染里把不可达状态标灰（REQ-SEQ-055）。

**REQ-SEQ-014（可以 · S2）** 可以提供 FSM 最小化（Hopcroft）作为**报告项**（最小状态数、是否可约简），不得静默改写用户给定的状态编码去「凑」成本。

### 3.3 综合

**REQ-SEQ-020（必须 · S1）** 组合块必须复用现有后端（`multi exact / multi synth / multi path / multi compose`），不得为时序逻辑另写一套覆盖/映射逻辑。

**REQ-SEQ-021（必须 · S1）** 触发器必须是**原语**：由标准单元库映射（`dfflibmap -liberty` 或等价步骤），不从组合网络推导；不得用组合反馈去「造」锁存器。

**REQ-SEQ-022（必须 · S1）** 复位必须按 spec 的同步/异步方式生成；异步复位必须用带复位端的库单元（`DFFR_` / `SDFFR_` 族），同步复位允许实现为组合逻辑 + 无复位触发器，并需在报告里说明。

**REQ-SEQ-023（必须 · S3）** 状态编码搜索必须对每个候选（binary / one-hot / gray）各跑一次完整综合与验证，按**实测**总成本选最小；选择规则必须确定（先比总成本，再比触发器数，再比编码名），不得依赖机器速度。

**REQ-SEQ-024（必须 · S1）** 综合流水线必须复用现有映射/证明口径：组合部分仍走 `synthesize_rtl_atoms`（或同族函数），最终网表仍由 Yosys miter + Verilator 检验；时序部分新增验证见 §3.5。

**REQ-SEQ-025（应当 · S2）** 可以接入 ABC 的顺序命令（`scorr` / `dch` / `retime` 等）作为可选后端，接入方式与 M1 的 `--abc` 相同：子进程、失败回退、改写前后等价证明、映射与计费不外包。

### 3.4 成本

**REQ-SEQ-030（必须 · S1）** 成本公式：`total = comb_cost + dff_cost`，其中 `comb_cost` 是现有映射实测（含胶合），`dff_cost = Σ 触发器实例数 × 该单元 CDL 晶体管数`。

**REQ-SEQ-031（必须 · S1）** 触发器单价必须从 CDL 实测（数 `M` 行），不得写死常数；本机 Nangate45 基线见附录 A。

**REQ-SEQ-032（必须 · S1）** JSON 必须分别报 `dffs`（实例数）、`state_bits`、`dff_cost`、`comb_cost`、`total`；不得把触发器成本混进 `atom_cost` 或 `cell_cost`。

**REQ-SEQ-033（必须 · S1）** `static_cell()` 的白名单必须拆成「组合」与「时序」两类：时序单元只计价，不参与 κ 线性/PUN 对偶判定；`static_cmos` 报告必须分开列「组合单元」与「时序单元」。

**REQ-SEQ-034（应当 · S1）** 多编码对比时必须同时报每个候选的 `total / comb_cost / dff_cost / dffs`，便于讲清「one-hot 用更多触发器换更简单组合」。

### 3.5 验证

**REQ-SEQ-040（必须 · S0）** v1 的主 oracle 必须是 **C++ 内 product machine 穷举**：spec FSM 与候选实现的 FSM 模型从复位状态做 BFS，逐状态比较输出（S0 先支持由 spec 直接生成的行为模型；从 RTL/网表提取 FSM 是 S2 的扩展）；状态空间超过预算时报错，不得静默跳过。

**REQ-SEQ-041（必须 · S1）** 必须提供多周期 SAT 证明：Yosys `sat -seq N -prove mismatch 0`（BMC）至少支持 N 可配；可选 `-tempinduct` 做 k-归纳。证明脚本必须显式驱动复位序列（用 `-set-at`），不得依赖未定义初态。

**REQ-SEQ-042（必须 · S1）** 必须提供 Verilator 多周期测试：复位后跑 `--cycles N`，激励为固定种子 LCG（可复现），失配时打印周期号与信号名。

**REQ-SEQ-043（必须 · S1）** 时序路径**不得**使用 `proven_by_spec` 跳过证明；若某个输出确实跳过了 miter，JSON 必须给出 `proof_method` 与 `proof_scope`（沿用现有字段惯例）。

**REQ-SEQ-044（必须 · S0）** 必须有一组**真正会失败**的负控：复位值接错、复位方式错（同步 vs 异步）、少一条转移、用锁存器代替触发器、初态未定义、DC 行被当 care、输出打拍漏掉一拍。每条负控必须证明被拒绝或验证失败。

**REQ-SEQ-045（应当 · S2）** 可以把 SBY 作为可选外部驱动（`mode bmc|prove` + `depth`），但不得成为默认路径的唯一实现；默认路径必须能在只有 Yosys/Verilator 的环境跑通。

**REQ-SEQ-046（必须 · S2）** 至少用一个状态空间可穷举的小 FSM（如 2-bit counter、序列检测器）同时跑三套 oracle（product machine、`sat -seq`、Verilator），三者结论必须一致；不一致按 bug 处理。

### 3.6 接口、JSON、渲染

**REQ-SEQ-050（必须 · S1）** 新命令形态：`multi seq exact --spec FILE`、`multi seq synth --spec FILE [--library LIB] [--encoding ...]`、`verify seq --dut FILE --spec FILE --cycles N`；参数风格与现有 `verb-object` 一致。

**REQ-SEQ-051（必须 · S1）** 退出码契约不变：0 成功、1 证明/可行性失败、2 用法或运行时错误；`--json` 在错误时也必须可解析。

**REQ-SEQ-052（必须 · S1）** JSON 必须含：`command`、`state_bits`、`dffs`、`dff_cost`、`comb_cost`、`total`、`reachable_states`、`dc_rows`、`encoding`、`verified`、`proof`（方法/周期/是否归纳）；失败时含 `reason`。

**REQ-SEQ-053（必须 · S2）** JSON 的 `proof` 必须能区分 `product-machine`、`bmc`、`induction`、`simulation` 四种方法，便于回归对比。

**REQ-SEQ-054（应当 · S2）** 渲染是呈现、不决定正确性（沿用现有规则）：FSM 输出状态转移图（可达/不可达着色），门级图输出触发器符号与 CLK/RST 网络。

**REQ-SEQ-055（应当 · S2）** 状态图必须标出复位状态、不可达状态、以及每条转移的输入条件；不得只画可达状态而让读者误以为状态集只有这些。

**REQ-SEQ-056（可以 · S3）** 可以输出每编码对比表（Markdown/JSON 均可），供讲义直接引用。

### 3.7 质量、可追溯、资源

**REQ-SEQ-060（必须 · 全程）** 每个实现提交的说明必须引用对应的 `REQ-SEQ-NNN`；测试名、实例脚本断言、负控必须在注释里引用 REQ 编号。

**REQ-SEQ-061（必须 · 全程）** 断言锁契约与上界：组合成本沿用现有上界风格；触发器成本可以精确断言（来自 CDL）；映射出的标准单元成本按上界断言（不锁 ABC/工艺细节）。

**REQ-SEQ-062（必须 · 全程）** 不把某个 benchmark 电路烤进代码（沿用现有最强规则）；实例只做回归语料，能力必须来自通用机制。

**REQ-SEQ-063（必须 · S0）** 状态空间遍历必须有显式预算（复用 `--max-ops` 风格的显式上限），超预算报错并给出规模；不得靠「输入小所以不会爆」的假设。

**REQ-SEQ-064（必须 · 全程）** 内存上限沿用 `--mem-limit` / `RLIMIT_AS`；所有外部步骤（yosys / verilator / abc）继续用 timeout + 日志尾部。

**REQ-SEQ-065（必须 · 全程）** 回归语料至少含：2-bit counter、序列检测器、交通灯，以及 ISCAS'89 的小规模电路（如 s27/s208/s298）作为可选大样本；语料只用于测试，不进入实现。

**REQ-SEQ-066（应当 · 全程）** 文档同步：README 与本文件在行为变更的同一提交里更新；新增实例按现有仪式登记（spec/脚本/INSTANCE_CASES/README 矩阵/render.manifest）。

### 3.8 线 B：相位/时序语义（S4，需求先立框架）

**REQ-SEQ-070（必须 · S4）** 必须定义采样相位与保持相位：`|D|` 的判定从「每个 minterm 一个快照」升级为「每个相位一个约束」，并明确检查时机（CLK 有效沿前/后）。

**REQ-SEQ-071（必须 · S4）** 必须能用开关级模型分析一个 master–slave DFF 的 CDL：CLK/CLK̄ 两相、内部节点保持、电荷共享按模型边界显式声明不建模（REQ-SEQ-081）。

**REQ-SEQ-072（应当 · S4）** 应当给出「动态/预充结构在相位语义下是否合法」的判定示例：43T 预充 TG 译码器在 CLK=1 时靠电荷保持的输出必须被判为不满足 `|D|=1`（与已有手工结论一致），并给出加 keeper 后的成本对比。

**REQ-SEQ-073（可以 · S4）** 可以把锁存器（两相）纳入模型；若纳入，必须同时给出双相不重叠时钟的约束与违例判定。

## 4. 设计

### 4.1 规格格式（草案）

```json
{"name":"seqdet","state_bits":2,"reset":"S0",
 "inputs":["A"],
 "state":{"S0":0,"S1":1,"S2":2,"S3":3},
 "next":[{"name":"N1","expr":"S1 ^ A"},
         {"name":"N0","expr":"(~S1 & A) | (S1 & ~S0)"}],
 "outputs":[{"name":"Y","expr":"S1 & S0"}]}
```

- `next` / `outputs` 的表达式变量集 = 状态位名 + 输入名；组装成 `MultiProblem` 时 vars = 状态位 + 输入，names = next 位 + 输出。
- 状态位名按 spec 的 `state_bits` 生成（如 `S1,S0`），避免与输入重名；重名直接拒绝。
- 复位方式：`reset_mode: "sync" | "async"`（默认 sync），`reset_active: "low" | "high"`（默认 low）。

### 4.2 内部表示与流水线

1. **加载**：spec -> `SeqSpec {k, inputs, reset, state_map, next[], outputs[]}`；位宽/重名/规模检查（REQ-SEQ-006）。
2. **组装**：`MultiProblem`（next 位 + 输出），初始无 DC。
3. **可达性**：BFS（状态数 ≤ 2^k，边数 = 2^k × 2^m）；同时产生 `reachable` 集合与每条转移的见证（用于状态图，REQ-SEQ-055）。
4. **DC 掩码**：不可达 `(state,input)` 行写 `any`（REQ-SEQ-011/012）。
5. **组合综合**：调用现有后端；编码候选循环在**外层**（每个编码重新组装一次 `MultiProblem`，因为状态位名/行序变了）。
6. **RTL 发射**：组合块 + `always_ff`（或映射后触发器实例）；复位按 REQ-SEQ-022。
7. **成本**：`comb_cost`（现有实测）+ `dff_cost`（CDL 单价 × 实例数）。
8. **验证**：product-machine（主）+ `sat -seq`（BMC）+ Verilator（REQ-SEQ-040/041/042）。

### 4.3 可达性与 DC 的细节

- 复位状态的语义：**复位有效时**状态 = `reset`（同步复位在时钟沿生效；异步复位立即生效）。BFS 的起点是复位**之后**的那个状态。
- 不可达行 DC 只对 **next 位与 Moore 输出**成立；Mealy 输出在不可达状态下同样 DC（因为状态本身到不了）。实现上对每行统一写 `any` = 不可达判定。
- 确定化：导出前把 DC 行的 next 位写 0、Moore 输出写 0（或在 spec 里用 `dc_value` 覆盖），保证网表没有 X 依赖（REQ-SEQ-012）。
- 预算：`states × 2^inputs` 与 `2^(k+m)` 双上限；超限按 REQ-SEQ-063 报错。

### 4.4 综合与编码搜索

- 组合后端完全复用；编码搜索只改「状态位到物理触发器的映射」，不改 `MultiProblem` 的语义。
- one-hot 的 `state_bits = 状态数`；编码后 DC 行更多（未用组合），对现有后端的 DC 处理是加分项。
- 选择规则（REQ-SEQ-023）：先比 `total`，再比 `dffs`，再按编码名字典序；结果必须可复现。
- 与 compose 原子库的关系：v1 只允许**组合**原子参与 next/output 网络；时序原子见 §4.8。

### 4.5 触发器计价与 `static_cell` 两栏

- 现有代码事实：`src/synthesis/multistage.cpp` 的 `static_cell(type)` 是名字白名单（`INV_/NAND_/AOI_...`），映射结果一旦命中库单元就检查；`DFF_X1` 不在白名单里会直接抛 `mapped to non-static cell`。因此 S1 必须：
  1. 新增 `sequential_cell(type)`（`DFF`/`SDFF`/`DFFR`/`DFFS`/`DFFRS`/`TLAT` 前缀）；
  2. 成本循环里把时序单元累到 `dff_cost`（REQ-SEQ-032），不进入 `cell_cost`；
  3. `static_cmos` 报告的 `checked/failed` 只覆盖组合单元（REQ-SEQ-033）。
- 映射步骤（本地已核对）：`synth -top top` 之后触发器仍是 Yosys 内部 FF 单元（例如 `$_DFF_PN0_`），必须补 `dfflibmap -liberty <lib>` 才会变成库单元（异步复位实测映射到 `DFFR_X1`）。`dfflegalize` 用在需要把 FF 类型收敛/限制到目标库支持集合的场合（例如只允许 `$_DFF_PN0_`、或拒绝异步 set）；映射失败必须显式报错，不得静默换成同步复位。
- 单价（附录 A）来自 CDL `M` 行计数；实现必须在运行时读取 CDL，不得把附录数字写死。

### 4.6 验证架构

1. **product machine（主 oracle）**：DUT 必须是 FSM 级模型（spec 或从 RTL 提取的 FSM）。对 RTL DUT，v1 用「RTL 与 spec 各自综合成 FSM」比较；若 DUT 只有网表，用 Yosys 导出 next/output 真值表（`sat -show` 或写 JSON）后比较——实现细节在 S0 先只支持 spec 对 spec，S2 再扩到 RTL。
2. **BMC**：生成 miter（spec golden vs DUT），复位序列用 `-set-at 1 rst 0`（或 1，按极性）→ `-set-at 2 rst 1` → `-seq N -prove mismatch 0`；开关初态用 `-set-init-undef` 明确「不假设」或 `-set-init-*` 明确「假设」，不得默认。
3. **k-归纳**：可选 `-tempinduct`；若用，必须同时报 `proof_method=induction` 与归纳长度。
4. **Verilator**：复位 1 拍 + N 拍固定种子激励；golden 用行为级 FSM；失配打印周期与信号（REQ-SEQ-042）。
5. **一致性**：三者结论不一致时以 product machine 为准并当作 bug 定位（REQ-SEQ-046）。

### 4.7 线 B 的相位模型（S4 草案，先立接口）

- 引入 `Phase`（`sample` / `hold`）与每相位的 `|D|` 约束：`|D|` 不再只是「每个 minterm 一个快照」，而是「每个相位、每个节点」。
- DFF 的 CDL 作为开关图分析：CLK/CLK̄ 作为相位信号；内部节点在 hold 相位允许「保持」而不是「无驱动」；电荷共享/RC 仍不建模（REQ-SEQ-081）。
- 判定示例（REQ-SEQ-072）：43T 预充译码器在 CLK=1 时未选中输出无持续驱动 → 在 sample 相位不满足 `|D|=1`；加 keeper（+16T）后满足但总成本 59T > 54T，结论与已有手工分析一致。

### 4.8 时序原子（S5，非目标但先定契约）

- 原子契约草案：`SeqAtom {inputs, outputs, state_bits, next[], init}`；成本 = 实例数 ×（组合实测 + state_bits × DFF 实测）。
- compose 搜索需要新增「状态相容」约束（同一时钟/复位、状态宽度可拼接），以及「组合原子 + 时序原子」混合搜索的目标函数；在 S5 之前不得把 DFF 塞进 `MultiProblem`。

## 5. 里程碑

| 阶段 | 目标 | 退出条件 | 覆盖需求 |
|---|---|---|---|
| S0 | 纯 C++ 骨架：spec 加载、可达性/DC、DFF 计价、product-machine 验证 | 2-bit counter + 序列检测器跑通；负控全红；不依赖 Yosys | 001–006, 010–013, 040, 044, 063 |
| S1 | RTL 发射 + 映射 + 成本 + Verilator | `multi seq synth` 出网表，成本 = comb + dffs；Verilator 多周期 PASS | 020–022, 024, 030–033, 041–043, 050–052 |
| S2 | 证明与报告 | `sat -seq` + 可选 `-tempinduct`；三 oracle 一致；状态图渲染 | 025, 045, 046, 053–055 |
| S3 | 编码搜索 | binary/one-hot/gray 实测对比表；确定性选择 | 023, 034, 056 |
| S4 | 相位语义（线 B） | DFF 内部 master–slave 分析；`\|D\|` 相位定义；43T 判定示例 | 070–073 |
| S5 | 时序原子库 | 契约 + 状态相容搜索；至少一个计数器原子 | 082（届时改本文） |

## 6. VCRM（Verification Cross-Reference Matrix）

> 术语：VCRM 是**验证交叉引用矩阵**，逐条把需求映射到验证方法、层级、判据和证据（系统/国防工程里的标准做法，例如 ASDEFCON 的 VCRM 定义：<https://www.defence.gov.au/sites/default/files/2025-03/Whole-ASDEFCON-CMV2-V52.pdf>）。它与 RVTM（需求→验证的可追溯性记录）互补：**VCRM 是「怎么验」的基线，RVTM 是「验到哪」的运行记录**。本仓库的对应物：本表是计划，`tests/` 与输出 JSON 是证据，git 历史是追溯。

### 6.1 方法与层级代码

- **方法**：I = 检查（代码/文档/schema）；A = 分析（人工核算、可达性计算）；T = 测试（Catch2 / 集成脚本 / 实例脚本）；S = 仿真（Verilator）；F = 形式证明（Yosys SAT / product machine / 归纳）。
- **层级**：U = 单元；I = 集成；E = 实例/端到端。
- **状态**：`未实现`（基线默认）→ `进行中` → `已验证`（必须附证据位置）→ `豁免`（必须写理由与替代验证）。

### 6.2 VCRM 主表（逐条 REQ）

#### 模型与规格

| REQ | 方法 | 层级 | 判据 | 证据 | 状态 |
|---|---|---|---|---|---|
| 001 | I+T | U | 六个字段齐全；缺字段/位宽不符 exit 2 | schema 校验单测 | 未实现 |
| 002 | I+T | U | 编码显式；`--encoding` 三值 | 加载器测试 | 未实现 |
| 003 | T | U | 无复位 spec 被拒 | 负控 NC-08 | 未实现 |
| 004 | A+I | U | Moore/Mealy 由依赖自动判定 | 依赖测试 + 审查 | 未实现 |
| 005 | T | U | registered 输出多一个触发器且计费 | 成本断言 | 未实现 |
| 006 | A+T | U | 超规模在加载期 exit 2 | 规模测试 | 未实现 |

#### 分析与 don't-care

| REQ | 方法 | 层级 | 判据 | 证据 | 状态 |
|---|---|---|---|---|---|
| 010 | A+T | U | `reachable_states` 与手工枚举一致 | 2-bit counter 全状态测试 | 未实现 |
| 011 | T | U | `dc_rows` = 不可达行数 | JSON 断言 | 未实现 |
| 012 | T | U | 导出网表无 X 依赖（确定化） | 网表检查 + SAT | 未实现 |
| 013 | I+T | U | 报告可达/总状态；渲染标灰 | JSON + 渲染快照 | 未实现 |
| 014 | A+T | U | 最小化只报告，不改用户编码 | 报告测试 | 未实现 |

#### 综合

| REQ | 方法 | 层级 | 判据 | 证据 | 状态 |
|---|---|---|---|---|---|
| 020 | T | I | 同一 spec 的组合成本与 `multi exact` 对拍一致 | 对拍测试 | 未实现 |
| 021 | I+T | I | 触发器来自库单元（`dfflibmap`） | `net.json` 单元类型 | 未实现 |
| 022 | T | I | 同步/异步复位映射到正确单元 | `DFFR_X1`/`DFF_X1` 断言 + 负控 | 未实现 |
| 023 | T | E | 三编码各跑一遍，确定性选最小 | 对比表 | 未实现 |
| 024 | T | I | 组合走 `synthesize_rtl_atoms` + miter | 回归套件 | 未实现 |
| 025 | T | I | ABC 顺序命令可选；改写失败回退 | 等价证明 + 回退测试 | 未实现 |

#### 成本

| REQ | 方法 | 层级 | 判据 | 证据 | 状态 |
|---|---|---|---|---|---|
| 030 | A+T | U | `total = comb_cost + dff_cost` | JSON 断言 | 未实现 |
| 031 | A+T | U | 单价 = CDL `M` 行计数 | 与附录 A 对拍 | 未实现 |
| 032 | T | U | JSON 分项；不混入 `atom_cost`/`cell_cost` | schema 测试 | 未实现 |
| 033 | T | U | `static_cmos` 分栏；DFF 不进 κ 判定 | 报告测试 | 未实现 |
| 034 | T | E | 每编码报四项成本 | 对比表 | 未实现 |

#### 验证

| REQ | 方法 | 层级 | 判据 | 证据 | 状态 |
|---|---|---|---|---|---|
| 040 | F | U | product machine 全状态一致 | 报告 + NC-03/NC-06 | 未实现 |
| 041 | F | I | `sat -seq N` PASS；错一拍必须 FAIL | 证明日志 | 未实现 |
| 042 | S | I | N 周期 PASS；固定种子可复现 | Verilator 日志 | 未实现 |
| 043 | I+T | I | 时序路径无 `proven_by_spec` | JSON `proof_method` + 静态检查 | 未实现 |
| 044 | T | I | 七条负控全部失败 | NC-01..NC-07 | 未实现 |
| 045 | F | I | SBY 可选；无 sby 也能跑默认路径 | 两条路径都通过 | 未实现 |
| 046 | F+S | U | 三个 oracle 结论一致 | 交叉报告 | 未实现 |

#### 接口、JSON、渲染

| REQ | 方法 | 层级 | 判据 | 证据 | 状态 |
|---|---|---|---|---|---|
| 050 | T | I | 命令与参数出现在 `--help` | CLI 测试 | 未实现 |
| 051 | T | I | 退出码 0/1/2 契约 | 错误注入 | 未实现 |
| 052 | T | I | JSON 字段齐全（含 `proof`） | schema 测试 | 未实现 |
| 053 | T | I | `proof` 四值可区分 | JSON 断言 | 未实现 |
| 054 | I | I | 渲染不参与正确性判定 | 审查 + 快照 | 未实现 |
| 055 | I | I | 状态图含复位/不可达/转移条件 | 图审 | 未实现 |
| 056 | I | I | 对比表可被讲义引用 | 快照 | 未实现 |

#### 质量、可追溯、资源

| REQ | 方法 | 层级 | 判据 | 证据 | 状态 |
|---|---|---|---|---|---|
| 060 | I | — | 实现提交引用 `REQ-SEQ-NNN` | `git log` 检查 | 未实现 |
| 061 | I | — | 断言只锁契约与上界 | 审查 | 未实现 |
| 062 | I | — | 无 benchmark 烤进代码 | 审查 + grep | 未实现 |
| 063 | T | U | 超预算 exit 2 并报规模 | 预算测试 | 未实现 |
| 064 | T | I | `--mem-limit`/timeout 对子进程生效 | 子进程测试 | 未实现 |
| 065 | T | E | 语料登记在 `INSTANCE_CASES` | 实例清单 | 未实现 |
| 066 | I | — | README/本文与行为同提交 | 审查 | 未实现 |

#### 线 B：相位/时序语义

| REQ | 方法 | 层级 | 判据 | 证据 | 状态 |
|---|---|---|---|---|---|
| 070 | A | U | 采样/保持相位定义写进模型 | 设计评审 + 单测 | 未实现 |
| 071 | F | U | master–slave DFF 的开关级相位判定 | CDL 分析报告 | 未实现 |
| 072 | A+F | U | 43T 预充判定不满足 `\|D\|=1` | 报告（与手工结论一致） | 未实现 |
| 073 | A | U | latch 双相约束与违例判定（若纳入） | 评审 + 单测 | 未实现 |

#### 非目标（用「拒绝测试」验证）

| REQ | 方法 | 层级 | 判据 | 证据 | 状态 |
|---|---|---|---|---|---|
| 080 | T | U | 多时钟/ICG/扫描 spec 被拒 | 负控 NC-08 | 未实现 |
| 081 | I | — | 不建模项在文档中逐条声明 | 审查 | 未实现 |
| 082 | T | U | 时序原子在 v1 被拒 | 加载器测试 | 未实现 |
| 083 | T | U | `static_cmos` 不对 DFF 做 κ/对偶判定 | 报告测试 | 未实现 |

### 6.3 负控矩阵（Negative Controls）

| ID | 注入的错误 | 关联 REQ | 期望结果 |
|---|---|---|---|
| NC-01 | 复位值接错（复位到非 reset 状态） | 040/041/042 | 至少两个 oracle 失败 |
| NC-02 | 复位方式错（同步实现成异步，或反之） | 022/041 | 映射或证明失败 |
| NC-03 | 少一条转移 | 040/041 | product machine 与 BMC 都失败 |
| NC-04 | 用 latch 代替 DFF（边沿行为错） | 021/040 | 多周期比较失败 |
| NC-05 | 依赖未定义初态（`$initstate` 风格） | 041/043 | JSON 报 undef 或证明失败 |
| NC-06 | 把 DC 行当 care（在不可达状态上取错值） | 011/040 | 不可达状态错值被检出 |
| NC-07 | 输出打拍漏掉一拍 | 040/042 | 周期错位被检出 |
| NC-08 | 无复位 / 两时钟 spec | 003/080 | exit 2，错误信息可解析 |
| NC-09 | 时序路径使用 `proven_by_spec` | 043 | 静态检查/JSON 断言失败 |
| NC-10 | ABC 顺序改写改错函数 | 025 | 等价自检回退并报 `abc.reason` |

### 6.4 维护规则

- **REQ 变更先改本文**：新增/删除需求时，VCRM 行必须同步增删，不允许「有需求无验证」。
- **状态升级要附件证据**：`已验证` 必须写证据路径（测试名、JSON 字段、日志、图）；`豁免` 必须写理由和替代验证（例如「S0 没有 Yosys，BMC 由 S2 覆盖」）。
- **每次基线变更记一行**：本文顶部加「基线段」记录（日期、REQ 集合版本、对应 commit），便于 RVTM 对齐。
- **实现提交引用编号**：commit message 至少写一个 `REQ-SEQ-NNN`；纯重构/文档提交可写 `REQ-SEQ-NNN (no behaviour change)`。

## 7. 风险与未决问题

- **R1 触发器映射**：Yosys `synth` 不映射 DFF，需要 `dfflibmap`（已本地核对：`$_DFF_PN0_` → `DFFR_X1`）；`dfflegalize` 的支持类型集合随版本变化，接入时按本机 Yosys 实测并写进 JSON。
- **R2 复位语义**：同步 vs 异步、复位极性、FPGA 风格初态（`$initstate`）三者的组合很多；v1 只支持「同步/异步 × 低/高」四种，其余拒绝。
- **R3 初态与 X**：`sat -set-init` 的四种模式（undef/def/zero/显式）会改变证明含义；必须显式选择并写进 JSON（REQ-SEQ-041）。
- **R4 DC 的合法性**：不可达行 DC 依赖「复位后状态可达性」；若 DUT 能自发进入不可达状态（实现 bug），DC 可能掩盖错误——用负控（REQ-SEQ-044）和 product-machine 兜底。
- **R5 成本可比性**：DFF 单价按 CDL 晶体管数，与现有 `cell_cost` 口径一致；但时钟树/布线不计，讲课时要说明「不是物理面积」。
- **R6 ABC 顺序命令**：`scorr`/`dch`/`retime` 的语义随版本变化（本地 `abc -c help` 只列命令名，具体说明在 ABC 文档）；接入前必须实测并用等价自检兜底（与 M1 相同）。
- **R7 线 B 的模型边界**：相位语义一旦引入电荷保持/预充，就必须明确「不建模电荷共享」，否则会得到看似严格实则物理上不成立的结论。
- **未决 Q1**：v1 是否支持锁存器？本文按 REQ-SEQ-080 排除，但两相锁存是教学上自然的下一步。
- **未决 Q2**：product-machine oracle 需要 FSM 级 DUT；从 RTL 反推 FSM（Yosys `fsm_detect`/`fsm_extract`）在多大范围内可靠？S2 评估。
- **未决 Q3**：是否引入 SBY 作为依赖？REQ-SEQ-045 只把它当可选驱动。
- **未决 Q4**：时序等价是否要支持「不同状态编码」的 DUT/spec 比较（需要状态映射）？S2 先按同一编码，S3 再扩。

## 8. 参考材料

| 材料 | 用途 | 链接 | 核对状态 |
|---|---|---|---|
| Yosys FSM 文档（fsm / fsm_detect / fsm_extract / fsm_recode / fsm_map） | one-hot/binary 编码、FSM 提取 | <https://yosyshq.readthedocs.io/projects/yosys/en/0.40/using_yosys/synthesis/fsm.html> | 文档核对 |
| Yosys `sat`（`-seq` / `-tempinduct` / `-set-init` / `-set-at` / `-prove-x`） | BMC 与 k-归纳 | 本机 `yosys -p 'help sat'` | 本地核对 |
| Yosys `dfflegalize` / `opt_dff` / `dfflibmap` / `clk2fflogic` / `async2sync` | 触发器合法化、映射、多时钟与异步复位 | 本机 `yosys -p 'help <pass>'` | 本地核对 |
| SymbiYosys（`mode bmc` / `mode prove`、`depth`、smtbmc/abc pdr 等引擎） | 外部形式化驱动（可选） | <https://symbiyosys.readthedocs.io/en/latest/reference.html> | 文档核对 |
| EQY 策略（`sat -tempinduct` 做次序等价） | 等价检查流程参考 | <https://github.com/YosysHQ/eqy/blob/main/docs/source/strategies.rst> | 文档核对 |
| ABC 顺序命令（`scorr`/`scleanup`/`dch`/`dchoice`/`retime`/`dretime`/`fraig`/`ifraig`） | 可选结构优化后端 | 本机 `abc -c help`；<https://github.com/berkeley-abc/abc> | 本地核对命令名，语义待核对 |
| ABC `ext_seq_dcs`（不可达状态 → EXDC）与 `dsec`（无界 SEC） | DC 与顺序等价的公开实现对照 | <https://people.eecs.berkeley.edu/~alanmi/abc_old/> | 文档核对（版本较老） |
| ISCAS'89 基准集（BLIF/Verilog 等格式） | 回归语料 | <https://ddd.fit.cvut.cz/www/prj/Benchmarks/> | 文档核对 |
| Nangate45 CDL（本机） | DFF/LATCH 计价与相位分析 | `~/yosys-sta/pdk/nangate45/cdl/NangateOpenCellLibrary.cdl` | 本地实测 |
| sky130 PDK（含 ICG） | 后续 clock gating 参考 | <https://skywater-pdk.readthedocs.io/> | 待核对 |
| Product machine + BFS 顺序等价讲义 | oracle 设计 | <http://www.ee.iitb.ac.in/~viren/Courses/2012/EE709/Lecture17.pdf> | 文档核对 |
| BMC / k-induction（Biere；Sheeran 等） | 理论背景 | 见文献 | 待核对 |

> 本文不是完整综述；外部材料只用于「有没有现成实现可抄/可对照」，不替代本地实测。

## 9. 附录

### A. 触发器计价实测（Nangate45 CDL，数 `M` 行）

| 单元 | 晶体管 |
|---|---|
| `TLAT_X1` | 20T |
| `DFF_X1` | 28T |
| `DFFR_X1` | 32T |
| `DFFS_X1` | 32T |
| `SDFF_X1` | 38T |
| `DFFRS_X1` | 40T |

方法：`python3` 逐 subckt 数 `M` 行（与 `CdlLibrary` 的晶体管计数口径一致）。实现必须运行时读取，不写死。

### B. REQ 索引

| ID | 摘要 | 阶段 |
|---|---|---|
| 001–006 | 规格格式、编码、复位、Moore/Mealy、打拍输出、规模上限 | S0 |
| 010–014 | 可达性、DC 掩码、确定化、报告、最小化（可选） | S0–S2 |
| 020–025 | 复用后端、DFF 原语、复位生成、编码搜索、同口径验证、ABC 可选 | S1–S2 |
| 030–034 | 成本公式、CDL 单价、JSON 分项、static_cell 两栏、多编码对比 | S1–S3 |
| 040–046 | product machine、BMC、Verilator、禁用跳证明、负控、SBY 可选、三 oracle 一致 | S0–S2 |
| 050–056 | CLI、退出码、JSON、proof 方法、渲染、状态图、对比表 | S1–S3 |
| 060–066 | 提交引用 REQ、上界断言、不烤 benchmark、预算、资源限制、语料、文档同步 | 全程 |
| 070–073 | 相位/采样、DFF 开关级分析、预充判定、锁存器（可选） | S4 |
| 080–083 | 非目标：单时钟、不建模时序物理量、无时序原子、DFF 不参与静态类判定 | 全程 |
