#include "synthesis/compose.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace dcstudy {
namespace {

namespace fs = std::filesystem;

// A reduced ordered BDD over the spec's variable order.  Terminals are 0/1;
// node ids >= 2 point into `nodes`.
struct Node {
  int var = 0;
  int lo = 0;
  int hi = 0;
};

struct Bdd {
  std::vector<Node> nodes{Node{}, Node{}}; // 0 = zero, 1 = one
  std::map<std::tuple<int, int, int>, int> unique;

  bool terminal(int id) const { return id < 2; }

  int mk(int var, int lo, int hi) {
    if (lo == hi)
      return lo;
    const auto key = std::make_tuple(var, lo, hi);
    const auto it = unique.find(key);
    if (it != unique.end())
      return it->second;
    const int id = static_cast<int>(nodes.size());
    nodes.push_back(Node{var, lo, hi});
    unique.emplace(key, id);
    return id;
  }

  int build(const std::vector<unsigned char> &table, int n, int level) {
    if (std::all_of(table.begin(), table.end(),
                    [&](unsigned char v) { return v == table[0]; }))
      return table[0] ? 1 : 0;
    const std::size_t half = table.size() / 2;
    std::vector<unsigned char> lo(table.begin(), table.begin() + half);
    std::vector<unsigned char> hi(table.begin() + half, table.end());
    return mk(level, build(lo, n, level + 1), build(hi, n, level + 1));
  }
};

std::string temp_dir() {
  static std::atomic<int> counter{0};
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  fs::path p =
      fs::temp_directory_path() / ("dcstudy-compose-" + std::to_string(stamp) +
                                   "-" + std::to_string(counter++) + ".d");
  fs::create_directories(p);
  return p.string();
}

} // namespace

ComposeResult compose_mux2(const MultiProblem &spec) {
  ComposeResult out;
  if (spec.vars.empty() || spec.outputs.empty()) {
    out.reason = "compose needs a non-empty spec";
    return out;
  }
  if (spec.has_dont_cares()) {
    out.reason = "compose does not support don't-care masks yet";
    return out;
  }

  Bdd bdd;
  std::vector<int> roots;
  const std::size_t rows = std::size_t{1} << spec.vars.size();
  for (const auto &po : spec.outputs) {
    std::vector<unsigned char> table(rows);
    for (std::size_t m = 0; m < rows; ++m)
      table[m] = po.func.value(static_cast<std::uint32_t>(m)) ? 1 : 0;
    roots.push_back(bdd.build(table, static_cast<int>(spec.vars.size()), 0));
  }

  // A BDD node whose function is a single literal is just that signal: no mux
  // instance.  (Otherwise Y = S?B:A would expand into muxes for A and B too.)
  auto is_identity = [&](int id) {
    const Node &nd = bdd.nodes[id];
    return nd.lo == 0 && nd.hi == 1;
  };
  auto is_inverted = [&](int id) {
    const Node &nd = bdd.nodes[id];
    return nd.lo == 1 && nd.hi == 0;
  };
  std::vector<int> mux_nodes;
  for (std::size_t id = 2; id < bdd.nodes.size(); ++id)
    if (!is_identity(static_cast<int>(id)) &&
        !is_inverted(static_cast<int>(id)))
      mux_nodes.push_back(static_cast<int>(id));

  std::ostringstream os;
  os << "module top(";
  for (std::size_t i = 0; i < spec.vars.size(); ++i)
    os << (i ? ", " : "") << "input logic " << spec.vars[i];
  for (std::size_t i = 0; i < spec.names.size(); ++i)
    os << ", output logic " << spec.names[i];
  os << ");\n";
  for (int id : mux_nodes)
    os << "  wire n" << id << ";\n";

  std::function<std::string(int)> expr = [&](int id) -> std::string {
    if (id == 0)
      return "1'b0";
    if (id == 1)
      return "1'b1";
    const Node &nd = bdd.nodes[id];
    if (is_identity(id))
      return spec.vars[nd.var];
    if (is_inverted(id))
      return "~" + spec.vars[nd.var];
    return "n" + std::to_string(id);
  };
  for (int id : mux_nodes) {
    const Node &nd = bdd.nodes[id];
    os << "  mux2 u" << id << "(.A(" << expr(nd.lo) << "), .B(" << expr(nd.hi)
       << "), .S(" << spec.vars[nd.var] << "), .Y(n" << id << "));\n";
  }
  for (std::size_t k = 0; k < roots.size(); ++k)
    os << "  assign " << spec.names[k] << " = " << expr(roots[k]) << ";\n";
  os << "endmodule\n";

  out.rtl = os.str();
  out.nodes = static_cast<int>(mux_nodes.size());

  // Exhaustively evaluate the composed network against the spec rows; this is
  // the only check that stays feasible when the truth table is above the
  // Yosys SOP reference limit.
  std::function<bool(int, std::uint32_t)> eval = [&](int id,
                                                     std::uint32_t m) -> bool {
    if (id < 2)
      return id == 1;
    const Node &nd = bdd.nodes[id];
    const bool bit =
        (m >> (spec.vars.size() - 1 - static_cast<std::size_t>(nd.var))) & 1u;
    return eval(bit ? nd.hi : nd.lo, m);
  };
  bool self_check = true;
  for (std::size_t k = 0; k < roots.size() && self_check; ++k)
    for (std::size_t m = 0; m < rows; ++m)
      if (eval(roots[k], static_cast<std::uint32_t>(m)) !=
          spec.outputs[k].func.value(static_cast<std::uint32_t>(m))) {
        self_check = false;
        out.reason = "compose self-check failed at output " + spec.names[k];
        break;
      }
  if (!self_check)
    return out;
  out.atom_rtl = "module mux2(input logic A, B, S, output logic Y);\n"
                 "  assign Y = (S & B) | (~S & A);\nendmodule\n";

  const std::string dir = temp_dir();
  const fs::path top = fs::path(dir) / "top.v";
  const fs::path atoms = fs::path(dir) / "mux2.v";
  const fs::path gold = fs::path(dir) / "golden.v";
  {
    std::ofstream(top) << out.rtl;
    std::ofstream(atoms) << out.atom_rtl;
  }
  // Above kMaxArity-1 the SOP reference is too deep for Yosys; use a
  // behavioural golden with the same BDD (which the self-check above tied to
  // the spec) and verify the mapped netlist against it structurally.
  if (spec.vars.size() > 12) {
    std::function<std::string(int)> tern = [&](int id) -> std::string {
      if (id == 0)
        return "1'b0";
      if (id == 1)
        return "1'b1";
      const Node &nd = bdd.nodes[id];
      if (is_identity(id))
        return spec.vars[nd.var];
      if (is_inverted(id))
        return "~" + spec.vars[nd.var];
      return "(" + spec.vars[nd.var] + " ? " + tern(nd.hi) + " : " +
             tern(nd.lo) + ")";
    };
    std::ostringstream g;
    g << "module golden(";
    for (std::size_t i = 0; i < spec.vars.size(); ++i)
      g << (i ? ", " : "") << "input logic " << spec.vars[i];
    for (std::size_t i = 0; i < spec.names.size(); ++i)
      g << ", output logic " << spec.names[i];
    g << ");\n";
    for (std::size_t k = 0; k < roots.size(); ++k)
      g << "  assign " << spec.names[k] << " = " << tern(roots[k]) << ";\n";
    g << "endmodule\n";
    std::ofstream(gold) << g.str();
  }
  try {
    if (spec.vars.size() > 12)
      out.synth = synthesize_rtl_atoms(top.string(), atoms.string(),
                                       MultiProblem{}, gold.string());
    else
      out.synth = synthesize_rtl_atoms(top.string(), atoms.string(), spec);
    out.ok = out.synth.ok;
    out.verified = out.synth.verified && self_check;
    if (!out.synth.ok)
      out.reason = out.synth.reason;
  } catch (const std::exception &e) {
    out.reason = e.what();
  }
  std::error_code ec;
  fs::remove_all(dir, ec);
  return out;
}

} // namespace dcstudy
