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
  run.sh                # instances | formal | optional | all
  instances/            # 实例验证（已知答案），spec 与 RTL 夹具都在这里
    decoder38.sh        # 3-8 译码器：54T
    encoder42.sh        # 4-2 编码器（any）：exact 8T / synth 8T / path 7T
    hex7seg.sh          # 4-7 段十六进制数码管：31 cells/162T，exact 180T
    prio42.sh           # 4-2 优先编码器：16T
    nor2.sh             # NOR2：可直接综合（单级互补门）4T
    xor2.sh / xnor2.sh  # XOR/XNOR：不可直接综合，自动设计仍给出 12T/14T/12T
    prio16.sh           # 16-4 优先编码器：4x pe42 + 胶合 = 172T，结构 miter
    lzcount16.sh        # 前导/尾随 0/1 计数：4x pe16 + 胶合 = 866T，结构 miter + 仿真
    *.json              # 各实例的 spec：decoder38/encoder42/hex7seg/pe42/nor2/xor2/xnor2
    rtl/                # 该实例的 RTL 夹具（pe42/top16/gold16）
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
| 4-7 段译码器（hex7seg） | spec | synth（默认 / --max-fanin 2）+ exact | 31 cells/162T；44 cells/192T；180T |
| 4-2 优先编码器 | spec | 逐输出开关级 miter | 16T，verified |
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
tests/verify/run.sh instances   # 实例验证（含 Verilator 仿真）
tests/verify/run.sh formal      # 形式化核心
tests/verify/run.sh optional    # 形式化可选（完整 CLI）
tests/verify/run.sh all
make verify                     # 只跑实例验证
```

新增实例：在 `instances/` 放一个 `<name>.sh`，断言 `verified` 与 `total`，
并加入 `instances/run.sh` 的列表；RTL 夹具放 `instances/rtl/`。