#include "cli/cmd_seq.hpp"

#include "cli/cli_common.hpp"
#include "cli/cli_includes.hpp"
#include "synthesis/fsm.hpp"

namespace dcstudy {

namespace {

using u32 = std::uint32_t;

std::string state_label(const FsmSpec &fsm, u32 code) {
  for (const auto &[name, value] : fsm.states)
    if (value == code)
      return name;
  return "#" + std::to_string(code);
}

std::string input_label(const FsmSpec &fsm, u32 code) {
  std::string out;
  const std::size_t m = fsm.inputs.size();
  for (std::size_t j = 0; j < m; ++j) {
    const bool bit = ((code >> (m - 1 - j)) & 1u) != 0;
    if (!out.empty())
      out += ",";
    out += fsm.inputs[j] + "=" + (bit ? "1" : "0");
  }
  return out;
}

Json cex_json(const FsmSpec &spec, const FsmSpec &dut,
              const FsmCounterExample &cex) {
  Json steps = Json::Array{};
  for (std::size_t i = 0; i < cex.spec_states.size(); ++i) {
    Json step;
    step["spec_state"] = state_label(spec, cex.spec_states[i]);
    step["dut_state"] = state_label(dut, cex.dut_states[i]);
    if (i < cex.inputs.size())
      step["input"] = input_label(spec, cex.inputs[i]);
    steps.push_back(std::move(step));
  }
  Json j;
  j["depth"] = cex.depth() - 1; // transitions before the mismatching input
  j["steps"] = std::move(steps);
  return j;
}

int cmd_seq_check(const Args &a) {
  const FsmSpec fsm = load_fsm_spec(a.need("spec"));
  const FsmAnalysis analysis = analyze_fsm(fsm);
  std::string cell = a.get("dff");
  if (cell.empty())
    cell = fsm.reset_mode == "async" ? "DFFR_X1" : "DFF_X1";
  const std::string cdl = a.get("cdl");
  const FsmCost cost = price_fsm_register(fsm, cdl, cell);
  if (!cdl.empty() && !cost.ok)
    throw CliError(cost.reason);

  std::optional<FsmSpec> dut;
  std::optional<FsmEquiv> equiv;
  if (a.has("dut")) {
    dut = load_fsm_spec(a.get("dut"));
    equiv = compare_fsm(fsm, *dut);
  }

  const bool json = a.is("json");
  Json j;
  j["ok"] = true;
  j["command"] = "seq.check";
  j["name"] = fsm.name;
  j["state_bits"] = fsm.state_bits;
  j["states"] = fsm.states.size();
  Json inputs = Json::Array{};
  for (const auto &name : fsm.inputs)
    inputs.push_back(name);
  j["inputs"] = std::move(inputs);
  Json outputs = Json::Array{};
  for (const auto &out : fsm.outputs)
    outputs.push_back(out.name);
  j["outputs"] = std::move(outputs);
  j["rows"] = analysis.rows;
  j["reachable_states"] = analysis.reachable.size();
  Json unreachable = Json::Array{};
  for (u32 code : analysis.unreachable)
    unreachable.push_back(state_label(fsm, code));
  j["unreachable_states"] = std::move(unreachable);
  j["dc_rows"] = analysis.dc_rows;
  Json dff;
  dff["cell"] = cost.cell;
  dff["bits"] = cost.dffs;
  if (cost.ok) {
    dff["transistors"] = cost.unit;
    dff["cost"] = cost.total;
  } else {
    dff["reason"] = cost.reason;
  }
  j["dff"] = std::move(dff);
  if (equiv && dut) {
    Json d;
    d["path"] = a.get("dut");
    d["equivalent"] = equiv->equivalent;
    if (!equiv->equivalent) {
      d["reason"] = equiv->reason;
      d["counterexample"] = cex_json(fsm, *dut, equiv->cex);
    }
    j["dut"] = std::move(d);
  }

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "seq check " << fsm.name << "\n";
    std::cout << "  " << fsm.state_bits << " state bits, " << fsm.inputs.size()
              << " input(s), " << fsm.outputs.size() << " output(s)\n";
    std::cout << "  states: " << analysis.reachable.size() << "/"
              << fsm.state_count() << " reachable";
    if (!analysis.unreachable.empty()) {
      std::cout << " (unreachable:";
      for (u32 code : analysis.unreachable)
        std::cout << " " << state_label(fsm, code) << "=" << code;
      std::cout << ")";
    }
    std::cout << "\n";
    std::cout << "  dc rows: " << analysis.dc_rows << "/" << analysis.rows
              << "\n";
    if (cost.ok) {
      std::cout << "  dff: " << cost.cell << " x " << cost.dffs << " = "
                << cost.total << "T (" << cost.unit
                << "T each, CDL measured)\n";
    } else {
      std::cout << "  dff: unavailable (" << cost.reason << ")\n";
    }
    if (equiv && dut) {
      std::cout << "  dut: " << a.get("dut") << "\n";
      if (equiv->equivalent) {
        std::cout << "  equivalent: yes\n";
      } else {
        std::cout << "  equivalent: no -- " << equiv->reason << "\n";
        for (std::size_t i = 0; i < equiv->cex.spec_states.size(); ++i) {
          std::cout << "    " << state_label(fsm, equiv->cex.spec_states[i])
                    << " vs " << state_label(*dut, equiv->cex.dut_states[i]);
          if (i < equiv->cex.inputs.size())
            std::cout << "  --" << input_label(fsm, equiv->cex.inputs[i])
                      << (i + 1 == equiv->cex.inputs.size() ? "  (mismatch)"
                                                            : "-->");
          std::cout << "\n";
        }
      }
    }
  }
  return (equiv && !equiv->equivalent) ? 1 : 0;
}

} // namespace

int cmd_seq(const std::string &sub, int argc, char **argv) {
  const Args a = parse_args(argc, argv, 3);
  if (sub == "check")
    return cmd_seq_check(a);
  throw CliError("unknown seq subcommand: " + sub);
}

} // namespace dcstudy
