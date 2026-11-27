#include "synthesis/fsm.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/cdl.hpp"
#include "core/json.hpp"

namespace dcstudy {

namespace {

namespace fs = std::filesystem;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

std::string state_var_name(std::size_t bits, std::size_t index) {
  return "s" + std::to_string(bits - 1 - index);
}

std::string read_text(const std::string &path) {
  std::ifstream in(path);
  if (!in)
    throw std::runtime_error("cannot read FSM spec: " + path);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

const Json &need(const Json &root, const char *key) {
  if (!root.contains(key))
    throw std::runtime_error(std::string("FSM spec is missing '") + key + "'");
  return root.at(key);
}

std::string need_string(const Json &root, const char *key) {
  const Json &v = need(root, key);
  if (!v.is_string())
    throw std::runtime_error(std::string("FSM spec '") + key +
                             "' must be a string");
  return v.as_string();
}

u32 step_state(const FsmSpec &fsm, u32 state, u32 input) {
  const std::size_t m = fsm.inputs.size();
  const u32 minterm = (state << m) | input;
  u32 next = 0;
  for (std::size_t i = 0; i < fsm.state_bits; ++i)
    if (fsm.next[i].value(minterm))
      next |= u32{1} << (fsm.state_bits - 1 - i);
  return next;
}

u32 output_value(const FsmOutput &out, u32 state, u32 input, std::size_t m) {
  return out.func.value((state << m) | input) ? 1u : 0u;
}

std::string join(const std::vector<std::string> &v, const char *sep) {
  std::string out;
  for (std::size_t i = 0; i < v.size(); ++i)
    out += (i ? sep : "") + v[i];
  return out;
}

} // namespace

std::size_t FsmSpec::rows() const {
  return std::size_t{1} << (state_bits + inputs.size());
}

std::uint32_t FsmSpec::reset_code() const {
  const auto it = states.find(reset);
  if (it == states.end())
    throw std::runtime_error("reset state '" + reset + "' is not in state");
  return it->second;
}

FsmSpec load_fsm_spec(const std::string &path) {
  const Json root = Json::parse(read_text(path));
  if (!root.is_object())
    throw std::runtime_error("FSM spec must be a JSON object");
  FsmSpec fsm;
  fsm.name = need_string(root, "name");
  if (fsm.name.empty())
    throw std::runtime_error("FSM spec 'name' must not be empty");

  const Json &bits = need(root, "state_bits");
  if (!bits.is_number())
    throw std::runtime_error("FSM spec 'state_bits' must be a number");
  const auto bits_value = bits.as_int();
  if (bits_value < 1 || bits_value > static_cast<std::int64_t>(kFsmMaxArity))
    throw std::runtime_error("state_bits must be 1.." +
                             std::to_string(kFsmMaxArity));
  fsm.state_bits = static_cast<std::size_t>(bits_value);

  const Json &inputs = need(root, "inputs");
  if (!inputs.is_array())
    throw std::runtime_error("FSM spec 'inputs' must be an array");
  for (const auto &v : inputs.array()) {
    if (!v.is_string())
      throw std::runtime_error("FSM spec 'inputs' must be strings");
    fsm.inputs.push_back(v.as_string());
  }
  if (fsm.state_bits + fsm.inputs.size() > kFsmMaxArity)
    throw std::runtime_error("state bits plus inputs exceed the " +
                             std::to_string(kFsmMaxArity) + "-variable limit");

  for (std::size_t i = 0; i < fsm.state_bits; ++i)
    fsm.state_vars.push_back(state_var_name(fsm.state_bits, i));

  const Json &states = need(root, "state");
  if (!states.is_object() || states.object().empty())
    throw std::runtime_error("FSM spec 'state' must be a non-empty object");
  const std::size_t state_count = fsm.state_count();
  std::set<u32> used_codes;
  for (const auto &[name, code_json] : states.object()) {
    if (!code_json.is_number())
      throw std::runtime_error("state '" + name + "' must map to a number");
    const auto code = code_json.as_int();
    if (code < 0 || static_cast<std::uint64_t>(code) >= state_count)
      throw std::runtime_error("state '" + name + "' code " +
                               std::to_string(code) + " does not fit " +
                               std::to_string(fsm.state_bits) + " bits");
    if (!used_codes.insert(static_cast<u32>(code)).second)
      throw std::runtime_error("two states share code " + std::to_string(code));
    fsm.states[name] = static_cast<u32>(code);
  }
  fsm.reset = need_string(root, "reset");
  if (!fsm.states.count(fsm.reset))
    throw std::runtime_error("reset state '" + fsm.reset + "' is not in state");

  // Names must be unambiguous in the emitted RTL.
  std::set<std::string> reserved(fsm.inputs.begin(), fsm.inputs.end());
  reserved.insert(fsm.state_vars.begin(), fsm.state_vars.end());
  for (const auto &[name, code] : fsm.states) {
    (void)code;
    if (reserved.count(name))
      throw std::runtime_error("state name '" + name +
                               "' collides with an input or state bit");
  }
  for (const auto &name : fsm.inputs)
    if (fsm.states.count(name))
      throw std::runtime_error("input '" + name +
                               "' collides with a state name");

  // Expressions see the state bits first, then the inputs (big-endian order).
  std::vector<std::string> vars = fsm.state_vars;
  vars.insert(vars.end(), fsm.inputs.begin(), fsm.inputs.end());
  auto parse = [&](const std::string &expr, const std::string &what) {
    try {
      return spec_from_expr(expr, vars).func;
    } catch (const std::exception &e) {
      throw std::runtime_error(what + ": " + e.what());
    }
  };

  const Json &next = need(root, "next");
  if (!next.is_object())
    throw std::runtime_error("FSM spec 'next' must be an object of state bits");
  for (const auto &[key, value] : next.object()) {
    if (!value.is_string())
      throw std::runtime_error("next." + key + " must be a string expression");
  }
  if (next.object().size() != fsm.state_vars.size())
    throw std::runtime_error("next must have exactly " +
                             std::to_string(fsm.state_vars.size()) +
                             " entries (one per state bit)");
  for (const auto &var : fsm.state_vars) {
    if (!next.contains(var))
      throw std::runtime_error("next is missing state bit '" + var + "'");
    fsm.next.push_back(parse(next.at(var).as_string(), "next." + var));
  }

  const Json &outputs = need(root, "outputs");
  if (!outputs.is_array())
    throw std::runtime_error("FSM spec 'outputs' must be an array");
  std::set<std::string> output_names;
  for (const auto &o : outputs.array()) {
    if (!o.is_object())
      throw std::runtime_error("each output must be {name, expr}");
    const std::string name = need_string(o, "name");
    if (name.empty() || name == "VDD" || name == "VSS")
      throw std::runtime_error("bad output name: " + name);
    if (reserved.count(name) || !output_names.insert(name).second)
      throw std::runtime_error("output '" + name +
                               "' collides with another name");
    FsmOutput out;
    out.name = name;
    out.registered = o.contains("registered") && o.at("registered").is_bool() &&
                     o.at("registered").as_bool();
    if (out.registered)
      throw std::runtime_error(
          "registered output '" + name +
          "' is S1 (REQ-SEQ-005); S0 accepts only combinational outputs");
    out.func = parse(need_string(o, "expr"), "outputs." + name);
    fsm.outputs.push_back(std::move(out));
  }

  if (root.contains("reset_mode")) {
    fsm.reset_mode = need_string(root, "reset_mode");
    if (fsm.reset_mode != "sync" && fsm.reset_mode != "async")
      throw std::runtime_error("reset_mode must be sync or async");
  }
  if (root.contains("reset_active")) {
    fsm.reset_active = need_string(root, "reset_active");
    if (fsm.reset_active != "low" && fsm.reset_active != "high")
      throw std::runtime_error("reset_active must be low or high");
  }
  return fsm;
}

FsmAnalysis analyze_fsm(const FsmSpec &fsm) {
  FsmAnalysis out;
  out.rows = fsm.rows();
  const std::size_t m = fsm.inputs.size();
  const u32 state_count = static_cast<u32>(fsm.state_count());
  const u32 input_count = m == 0 ? 1u : (u32{1} << m);
  std::vector<char> seen(state_count, 0);
  std::vector<u32> queue;
  queue.push_back(fsm.reset_code());
  seen[fsm.reset_code()] = 1;
  for (std::size_t head = 0; head < queue.size(); ++head) {
    const u32 state = queue[head];
    for (u32 x = 0; x < input_count; ++x) {
      const u32 next = step_state(fsm, state, x);
      if (!seen[next]) {
        seen[next] = 1;
        queue.push_back(next);
      }
    }
  }
  std::vector<u32> reachable = queue;
  std::sort(reachable.begin(), reachable.end());
  out.reachable = std::move(reachable);
  for (u32 s = 0; s < state_count; ++s)
    if (!seen[s])
      out.unreachable.push_back(s);
  out.dc_rows = out.unreachable.size() * (m == 0 ? 1 : (std::size_t{1} << m));
  return out;
}

MultiProblem fsm_problem(const FsmSpec &fsm, const FsmAnalysis &analysis) {
  MultiProblem problem;
  problem.title = fsm.name;
  problem.vars = fsm.state_vars;
  problem.vars.insert(problem.vars.end(), fsm.inputs.begin(), fsm.inputs.end());
  for (const auto &var : fsm.state_vars)
    problem.names.push_back(var);
  for (const auto &out : fsm.outputs)
    problem.names.push_back(out.name);

  const std::size_t m = fsm.inputs.size();
  std::vector<char> unreachable(fsm.state_count(), 0);
  for (u32 code : analysis.unreachable)
    unreachable[code] = 1;
  Bits any(analysis.rows, false);
  for (std::size_t row = 0; row < analysis.rows; ++row)
    if (unreachable[row >> m])
      any.set(row);

  for (const auto &func : fsm.next)
    problem.outputs.push_back({func, any});
  for (const auto &out : fsm.outputs)
    problem.outputs.push_back({out.func, any});
  return problem;
}

FsmEquiv compare_fsm(const FsmSpec &spec, const FsmSpec &dut) {
  FsmEquiv out;
  if (spec.inputs != dut.inputs) {
    out.reason = "input lists differ: [" + join(spec.inputs, ",") + "] vs [" +
                 join(dut.inputs, ",") + "]";
    return out;
  }
  std::map<std::string, const FsmOutput *> dut_outputs;
  for (const auto &o : dut.outputs)
    dut_outputs[o.name] = &o;
  if (dut_outputs.size() != spec.outputs.size()) {
    out.reason = "output lists differ in size";
    return out;
  }
  for (const auto &o : spec.outputs) {
    if (!dut_outputs.count(o.name)) {
      out.reason = "dut is missing output '" + o.name + "'";
      return out;
    }
  }
  const std::size_t budget = std::size_t{1} << 22;
  if (spec.state_count() * dut.state_count() > budget) {
    out.reason = "product machine has " +
                 std::to_string(spec.state_count() * dut.state_count()) +
                 " state pairs, over the " + std::to_string(budget) + " budget";
    return out;
  }

  const std::size_t m = spec.inputs.size();
  const u32 input_count = m == 0 ? 1u : (u32{1} << m);
  const u64 start = (static_cast<u64>(spec.reset_code()) << 32) |
                    static_cast<u64>(dut.reset_code());
  // parent[key] = (parent key, input that led here); presence also marks
  // visited pairs.
  std::unordered_map<u64, std::pair<u64, u32>> parent;
  parent[start] = {start, 0};
  std::vector<u64> queue{start};
  for (std::size_t head = 0; head < queue.size(); ++head) {
    const u64 key = queue[head];
    const u32 spec_state = static_cast<u32>(key >> 32);
    const u32 dut_state = static_cast<u32>(key & 0xffffffffu);
    for (u32 x = 0; x < input_count; ++x) {
      for (const auto &o : spec.outputs) {
        const u32 expected = output_value(o, spec_state, x, m);
        const u32 actual =
            output_value(*dut_outputs.at(o.name), dut_state, x, m);
        if (expected != actual) {
          FsmCounterExample cex;
          u64 walk = key;
          std::vector<u32> states_spec;
          std::vector<u32> states_dut;
          std::vector<u32> inputs;
          while (true) {
            states_spec.push_back(static_cast<u32>(walk >> 32));
            states_dut.push_back(static_cast<u32>(walk & 0xffffffffu));
            const auto &p = parent.at(walk);
            if (p.first == walk)
              break;
            inputs.push_back(p.second);
            walk = p.first;
          }
          std::reverse(states_spec.begin(), states_spec.end());
          std::reverse(states_dut.begin(), states_dut.end());
          std::reverse(inputs.begin(), inputs.end());
          inputs.push_back(x);
          cex.spec_states = std::move(states_spec);
          cex.dut_states = std::move(states_dut);
          cex.inputs = std::move(inputs);
          out.reason = "output " + o.name + " differs at depth " +
                       std::to_string(cex.depth() - 1);
          out.cex = std::move(cex);
          return out;
        }
      }
      const u32 next_spec = step_state(spec, spec_state, x);
      const u32 next_dut = step_state(dut, dut_state, x);
      const u64 next_key =
          (static_cast<u64>(next_spec) << 32) | static_cast<u64>(next_dut);
      if (parent.emplace(next_key, std::pair<u64, u32>{key, x}).second)
        queue.push_back(next_key);
    }
  }
  out.equivalent = true;
  return out;
}

FsmCost price_fsm_register(const FsmSpec &fsm, const std::string &cdl_path,
                           const std::string &cell) {
  FsmCost cost;
  cost.cell = cell;
  cost.dffs = fsm.state_bits; // registered outputs are rejected in S0
  const std::string path = cdl_path.empty() ? default_cdl_path() : cdl_path;
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    cost.reason = "CDL not found: " + path + " (set NANGATE45_CDL or --cdl)";
    return cost;
  }
  CdlLibrary library;
  try {
    library = CdlLibrary::load(path);
  } catch (const std::exception &e) {
    cost.reason = e.what();
    return cost;
  }
  if (!library.has(cell)) {
    cost.reason = "cell " + cell + " is not in " + path;
    return cost;
  }
  cost.unit = static_cast<int>(library.cell(cell).transistor_count());
  cost.total = static_cast<int>(cost.dffs) * cost.unit;
  cost.ok = true;
  return cost;
}

} // namespace dcstudy
