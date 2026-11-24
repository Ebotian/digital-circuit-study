# 验证目录

按**验证目的**而不是按命令组织测试：

- `instances/` — **实例验证**：完整设计从 spec/RTL 到映射网表，断言已知答案（成本、验证结论）。
  这些是教材例题和组合示例，跑得快，不依赖 Z3/PDK 之外的东西（prio16 需要 Yosys）。
- `formal/` — **形式化验证**：
  - `core.sh`：单元/性质测试（布尔代数、精确覆盖、开关级语义、CDL 解析、别名、浮空竞争、dc/any 规则等），
    只依赖构建产物，CI 必跑；
  - `optional.sh`：CLI 黑盒套件 `tests/integration.sh`，覆盖 Yosys/Verilator/Z3/PDK 相关功能，
    缺依赖时本地跳过、`CI=1` 时直接失败。

```
tests/verify/
  lib.sh                # BIN/fail/pass/need
  Makefile              # instances | simulation | formal | optional | render | all
  instances/            # 实例验证（已知答案）
    specs/              # 各实例的 spec：decoder38/encoder42/hex7seg/hex7seg_sel/pe42/nor2/xor2/xnor2
    scripts/            # 各实例断言：decoder38.sh ... prio16.sh/lzcount16.sh
    rtl/                # RTL 夹具：pe42/pe16/top16/gold16/lzcount16/gold_lzcount16/tb_lzcount16/hex7seg_sel_golden
    simulation/verilator-xor6/  # 同一实例层级、不同验证层：开关级 XOR6 动态仿真
  formal/
    core.sh             # 单元/性质测试（ctest）
    optional.sh         # 完整 CLI/开关级/Z3/Yosys 套件（integration.sh）
```

## 实例验证矩阵

| 实例 | 输入 | 验证方式 | 断言 |
|---|---|---|---|
| 3-8 译码器 | spec | 逐输出开关级 miter | 54T，verified |
| 4-2 编码器（any） | spec + any 掩码 | exact 的 dc 空间搜索 + 开关级 | 8T / 8T / 7T |
| 1 位二选一选择器（mux2） | spec | exact / synth / path | 14T / 12T / 14T，verified |
| 3 位四选一选择器（mux4_3bit） | RTL + 9×mux2 / 14 输入 spec | 结构 miter + BDD→mux2 compose | 9×12T = 108T，PASS |
| 8-3 优先编码器（prio8） | spec + {pe42,or4,mux2} 库 | 分区构造 + DC 补全 + 结构 miter | 2×16T+10T+2×12T = 66T，PASS |
| 4 位相等比较器（eq4） | 8 输入 spec（A3..A0, B3..B0） | synth + miter | 12 cells / 62T，verified |
| 1 位全加器（fa1） | 3 输入 spec + 2×ha 原子 | synth / exact / ha 组合 | 7 cells/38T；exact 42T；2×14T+OR=34T |
| 4 位 RCA + 三路七段（rca4_7seg） | 4×fa1 + 3×hex7seg 原子 | 结构 miter | 4×38T+3×162T = 638T，PASS |
| 4-7 段译码器（hex7seg） | spec | synth（默认 / --max-fanin 2）+ exact | 31 cells/162T；44 cells/192T；180T |
| 4 位 hex/十进制七段（hex7seg_sel） | 5 输入 spec（D3..D0,SEL） | synth（多级静态 CMOS）+ 原生渲染 | 40 cells / 232T，scheme-0.svg |
| 4-2 优先编码器（pe42） | spec + library | 开关级 miter + library 整函数匹配 | 16T，verified |
| 16-4 优先编码器 | RTL + 原子 + --ref | 结构 miter（无真值表） | 172T = 4x16T + 108T，PASS |
| 前导/尾随 0/1 计数（lzcount16） | RTL + 4x pe16 + --ref | 结构 miter + Verilator 激励 | 866T = 4x172T + 178T；16392 → LZ=1, TZ=3 |
| 开关级 XOR6（仿真） | Verilator testbench | 4/4 正例 + 注入故障必须被检出 | 动态仿真，非穷举 |
| NOR2（可直接综合） | spec | `bool info` realizable=true，exact/synth/path | 4T，verified |
| XOR2（不可直接综合） | spec | realizable=false，但仍可自动设计（输入反相器） | exact 12T / synth 14T / path 12T |
| XNOR2（不可直接综合） | spec | 同 XOR2 | exact 12T / synth 14T / path 12T |

## 形式化验证矩阵

| 层 | 内容 | 依赖 | 何时跑 |
|---|---|---|---|
| core | 布尔/精确覆盖/开关级/CDL/浮空/别名/dc 规则 | 仅构建产物 | 每次提交（CI） |
| optional | CLI 端到端、Yosys SAT、Z3 工作进程、PDK 单元 | yosys/z3/PDK | 本地完整回归、CI |

## 运行

```bash
make -C tests/verify instances   # 实例验证（含 Verilator 仿真）
make -C tests/verify formal      # 形式化核心
make -C tests/verify optional    # 形式化可选（完整 CLI）
make -C tests/verify all         # 以上全部
make verify                      # 仓库根目录：只跑实例
make verify-all                  # 仓库根目录：全量
```

新增实例：在 `instances/scripts/` 放一个 `<name>.sh`，断言 `verified` 与 `total`，
并把名字加进 `tests/verify/Makefile` 的 `INSTANCE_CASES`；spec 放 `instances/specs/`，RTL 夹具放 `instances/rtl/`。

### 可选渲染

```bash
make render                            # 每个实例的默认实现(fa1 含 spec + ha)
make render INSTANCE=fa1               # fa1 的全部实现 (spec + ha)
make render INSTANCE=fa1 VARIANT=ha    # 只渲 ha 组合
make render VARIANT=rtl                # 所有带 rtl 变体的实例
```

渲染清单在 `instances/render.manifest`（instance / variant / kind / 参数），
`render.sh` 只负责解释清单，不再为单个器件写 case。默认变体直接放在
`out/render/<instance>/`；同一器件的后续默认/非默认变体在
`out/render/<instance>/<variant>/`。组合/层次实例会把它用到的自定义原子也
一并渲染到 `<variant>/atoms/<atom>/`。
该目标默认不参与 `make all`/`make ci`，只作为本地看图入口。