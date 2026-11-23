#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dcstudy/bdd_switch.hpp"
#include "dcstudy/boolfunc.hpp"
#include "dcstudy/cdl.hpp"
#include "dcstudy/cmos.hpp"
#include "dcstudy/driver_profile.hpp"
#include "dcstudy/designspace.hpp"
#include "dcstudy/json.hpp"
#include "dcstudy/limits.hpp"
#include "dcstudy/math_client.hpp"
#include "dcstudy/multi_path.hpp"
#include "dcstudy/multistage.hpp"
#include "dcstudy/path_search.hpp"
#include "dcstudy/render_client.hpp"
#include "dcstudy/static_exact.hpp"
#include "dcstudy/switchlevel.hpp"
#include "dcstudy/template_synth.hpp"
#include "dcstudy/synthesis.hpp"
#include "dcstudy/verilog.hpp"
#include "dcstudy/verify.hpp"

namespace fs = std::filesystem;
using namespace dcstudy;

namespace {

struct CliError : std::runtime_error {
  int code;
  explicit CliError(const std::string& msg, int c = 2)
      : std::runtime_error(msg), code(c) {}
};

struct Args {
  std::map<std::string, std::string> opt;
  std::set<std::string> flag;

  bool has(const std::string& k) const { return opt.count(k) || flag.count(k); }
  bool is(const std::string& k) const { return flag.count(k) != 0; }
  std::string get(const std::string& k, const std::string& def = {}) const {
    auto it = opt.find(k);
    return it == opt.end() ? def : it->second;
  }
  std::string need(const std::string& k) const {
    auto it = opt.find(k);
    if (it == opt.end()) throw CliError("missing --" + k);
    return it->second;
  }
};

const std::set<std::string> kBoolFlags = {
    "json", "all", "canonical", "exact", "keep", "help", "verbose", "no-tg",
    "heavy", "png", "no-logic", "static-cmos", "graphblas", "tri-state"};

Args parse_args(int argc, char** argv, int start) {
  Args a;
  for (int i = start; i < argc; ++i) {
    std::string s = argv[i];
    if (s.rfind("--", 0) != 0) {
      throw CliError("unexpected argument: " + s);
    }
    const std::string body = s.substr(2);
    const auto eq = body.find('=');
    const std::string key = body.substr(0, eq);
    if (kBoolFlags.count(key)) {
      if (eq != std::string::npos)
        throw CliError("flag --" + key + " does not take a value");
      a.flag.insert(key);
    } else {
      if (eq != std::string::npos) {
        a.opt[key] = body.substr(eq + 1);
      } else {
        if (i + 1 >= argc || std::string(argv[i + 1]).rfind("--", 0) == 0)
          throw CliError("missing value for --" + key);
        a.opt[key] = argv[++i];
      }
    }
  }
  return a;
}

std::vector<std::string> split_csv(const std::string& s) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const auto p = s.find(',', start);
    std::string item = s.substr(start, p == std::string::npos ? std::string::npos
                                                              : p - start);
    auto b = item.find_first_not_of(" \t");
    auto e = item.find_last_not_of(" \t");
    if (b != std::string::npos) out.push_back(item.substr(b, e - b + 1));
    if (p == std::string::npos) break;
    start = p + 1;
  }
  return out;
}

FuncSpec get_spec(const Args& a) {
  const bool has_expr = a.has("expr");
  const bool has_tt = a.has("tt");
  if (has_expr == has_tt) throw CliError("give exactly one of --expr or --tt");
  const auto vars = split_csv(a.get("vars"));
  if (has_expr) return spec_from_expr(a.get("expr"), vars);
  return spec_from_truth(a.get("tt"), vars);
}

std::vector<int> target_of(const FuncSpec& s) {
  std::vector<int> t;
  t.reserve(s.func.num_minterms());
  for (std::uint32_t m = 0; m < s.func.num_minterms(); ++m)
    t.push_back(s.func.value(m) ? 1 : 0);
  return t;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
  std::string out;
  for (const auto& s : v) {
    if (!out.empty()) out += sep;
    out += s;
  }
  return out;
}

Json driver_profile_json(const DriverProfile& p) {
  Json j;
  j["all_single"] = p.all_single;
  j["valid"] = p.valid;
  j["simple"] = p.simple;
  j["max_drivers"] = p.max_drivers;
  j["power_steps"] = p.power_steps;
  Json rows = Json::Array{};
  for (std::size_t m = 0; m < p.rows.size(); ++m) {
    const auto& r = p.rows[m];
    Json row;
    row["count"] = static_cast<int>(r.sources.size());
    Json sources = Json::Array{};
    Json values = Json::Array{};
    Json strong = Json::Array{};
    for (std::size_t i = 0; i < r.sources.size(); ++i) {
      sources.push_back(r.sources[i]);
      values.push_back(r.values[i]);
      strong.push_back(r.strong[i]);
    }
    row["sources"] = std::move(sources);
    row["values"] = std::move(values);
    row["strong"] = std::move(strong);
    rows.push_back(std::move(row));
  }
  j["rows"] = std::move(rows);
  return j;
}

Json design_json(const Design& d, const std::vector<std::string>& vars) {
  Json j;
  j["cost"] = d.cost;
  Json invs = Json::Array{};
  for (auto i : d.inv_vars) invs.push_back(vars.at(i));
  j["inverters"] = invs;
  Json branches = Json::Array{};
  for (const auto& b : d.branches) {
    Json bj;
    bj["source"] = b.source;
    bj["cost"] = b.cost;
    Json chain = Json::Array{};
    for (const auto& el : b.chain) {
      Json ej;
      ej["kind"] = switch_name(el.kind);
      ej["lit"] = el.lit;
      ej["cost"] = el.cost;
      chain.push_back(std::move(ej));
    }
    bj["chain"] = std::move(chain);
    bj["topology"] = b.topo(vars);
    branches.push_back(std::move(bj));
  }
  j["branches"] = std::move(branches);
  j["topology"] = d.topologies(vars);
  return j;
}

std::vector<std::string> cube_names(const Cube& c,
                                      const std::vector<std::string>& vars) {
  std::vector<std::string> out;
  const std::size_t n = vars.size();
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint64_t bit = std::uint64_t{1} << (n - 1 - i);
    if (!(c.care & bit)) continue;
    if (!(c.value & bit))
      throw std::runtime_error("PD cube contains a negated literal");
    out.push_back(vars[i]);
  }
  return out;
}

Json static_design_json(const CmosDesign& d,
                        const std::vector<std::string>& vars) {
  Json j;
  j["kind"] = "static-cmos";
  j["cost"] = d.total;
  j["nfet"] = d.nfet;
  j["pfet"] = d.pfet;
  j["kappa_linear"] = true;
  j["pun_dual"] = true;
  j["inverters"] = Json::Array{};
  j["branches"] = Json::Array{};
  Json pd = Json::Array{};
  Json pu = Json::Array{};
  for (const auto& c : d.pull_down.terms) {
    const auto names = cube_names(c, vars);
    Json term = Json::Array{};
    for (const auto& s : names) term.push_back(s);
    pd.push_back(term);
    // PUN is the dual: each PDN term becomes a parallel PMOS group, groups
    // are in series.
    pu.push_back(term);
  }
  j["pd_terms"] = std::move(pd);
  j["pu_groups"] = std::move(pu);
  j["pull_down"] = d.pull_down.expression(vars);
  j["pull_up"] = pull_up_expression(d, vars);
  j["topology"] = "PDN: " + d.pull_down.expression(vars) +
                  "; PUN: " + pull_up_expression(d, vars);
  return j;
}

std::string subckt_to_cdl(const Subckt& c) {
  std::string out = ".SUBCKT " + c.name;
  for (const auto& p : c.ports) out += " " + p;
  out += "\n*.PININFO";
  for (const auto& p : c.ports) {
    const char dir = [&] {
      switch (c.direction(p)) {
        case PinDir::Input: return 'I';
        case PinDir::Output: return 'O';
        case PinDir::Power: return 'P';
        case PinDir::Ground: return 'G';
        default: return 'B';
      }
    }();
    out += " " + p + ":" + dir;
  }
  out += "\n";
  for (const auto& d : c.devices) {
    const std::string bulk = d.pmos ? "VDD" : "VSS";
    out += d.name + " " + d.drain + " " + d.gate + " " + d.source + " " +
           bulk + " " + d.model + "\n";
  }
  out += ".ENDS\n";
  return out;
}

void write_files(const fs::path& dir, const Design& d, const FuncSpec& spec) {
  fs::create_directories(dir);
  const std::string dut = emit_dut(d, spec.vars);
  std::ofstream(dir / "dut.v") << dut;
  std::ofstream(dir / "ref.v") << emit_ref(spec.func, spec.vars);
  std::ofstream(dir / "miter.v") << emit_miter(spec.vars);
  std::ofstream(dir / "tb.v") << emit_tb(spec.func, spec.vars);
}

enum class VerifyMode { None, Yosys, Sim, Both, Static };

VerifyMode parse_verify(const Args& a, VerifyMode def) {
  const std::string v = a.get("verify");
  if (v.empty()) return def;
  if (v == "none") return VerifyMode::None;
  if (v == "yosys") return VerifyMode::Yosys;
  if (v == "sim") return VerifyMode::Sim;
  if (v == "both") return VerifyMode::Both;
  if (v == "static") return VerifyMode::Static;
  throw CliError("--verify must be none|yosys|sim|both|static");
}

Json verify_json(const ToolResult& r) {
  Json j;
  j["ok"] = r.ok;
  j["exit_code"] = r.exit_code;
  j["output"] = r.output;
  if (!r.workdir.empty()) j["workdir"] = r.workdir;
  return j;
}

struct Verified {
  bool yosys = false;
  bool sim = false;
  Json json;
};

Verified verify_design(const Design& d, const FuncSpec& spec, VerifyMode mode,
                       bool keep, const fs::path* emit_dir) {
  Verified v;
  const std::string dut = emit_dut(d, spec.vars);
  if (emit_dir) write_files(*emit_dir, d, spec);
  Json j;
  if (mode == VerifyMode::Yosys || mode == VerifyMode::Both) {
    const auto r = prove_yosys(dut, spec.func, spec.vars, keep);
    v.yosys = r.ok;
    j["yosys"] = verify_json(r);
  }
  if (mode == VerifyMode::Sim || mode == VerifyMode::Both) {
    const auto r = simulate_verilator(dut, spec.func, spec.vars, keep);
    v.sim = r.ok;
    j["verilator"] = verify_json(r);
  }
  v.json = std::move(j);
  return v;
}

void emit_error(bool want_json, const std::string& message, int code) {
  if (want_json) {
    Json j;
    j["ok"] = false;
    j["error"] = message;
    j["exit_code"] = code;
    std::cout << j.dump() << "\n";
  } else {
    std::cerr << "error: " << message << "\n";
  }
}

void help() {
  std::cout << R"(dcstudy - Boolean circuits, exact synthesis, formal checks

Usage:
  dcstudy [--json] <command> [options]

Commands:
  bool info      --expr EXPR | --tt BITS [--vars A,B]
  bool count     --n N
  cmos synth     --expr EXPR | --tt BITS [--vars A,B]
  math closure   --n N --edges 0-1,1-2 [--json]
  math gf2       --rows '1,1;0,1' [--ncols N] [--json]
  cdl list       [--cdl FILE]
  cdl solve      --cell NAME [--cdl FILE] [--inputs A,B] [--exact]
                 [--graphblas] [--expect BITS]
  design bdd     --expr EXPR | --tt BITS [--vars A,B]
  design template --expr EXPR | --tt BITS [--vars A,B]
  design path    --expr EXPR | --tt BITS [--max-cost N] [--max-chain N] [--tri-state]
  design min     --expr EXPR | --tt BITS [--max-cost N] [--max-chain N]
                 [--all] [--limit N] [--canonical] [--emit DIR]
                 [--verify none|yosys|sim|both|static] [--static-cmos] [--keep]
  verify equiv   --dut FILE --expr EXPR | --tt BITS [--vars A,B] [--keep]
  verify sim     --dut FILE --expr EXPR | --tt BITS [--vars A,B] [--keep]
  pipeline       --funcs 'name=EXPR,...' [--max-cost N] [--out DIR]
                 [--verify none|yosys|sim|both] [--canonical]
  multi synth    --spec FILE [--out DIR] [--png] [--max-fanin N]
                 [--json]
  multi exact    --spec FILE [--out DIR] [--json]
  multi path     --spec FILE [--max-cost N] [--max-chain N] [--tri-state] [--json]
  multi template --spec FILE [--json]
  render         --expr EXPR | --tt BITS [--max-cost N] [--limit N]
                 [--canonical] [--out DIR] [--png] [--no-logic]
                 [--static-cmos]

Options:
  --json         machine-readable output (also on errors)
  --vars         comma-separated variable order, e.g. A,B
  --max-cost     total transistor budget for a design (default 8)
  --max-chain    maximum switches in a series chain (default 4)
  --all          enumerate all optimal designs (up to --limit)
  --limit        maximum enumerated designs (default 8)
  --canonical    quotient designs by input permutations that preserve f
  --png          also rasterise rendered SVGs (needs rsvg-convert)
  --no-logic     skip the logic-abstraction panel (and its LaTeX compile)
  --static-cmos  restrict to static complementary CMOS (kappa-linear; rejects
                 pass/TG; single-stage, with automatic multi-stage fallback)
  --graphblas    use the GraphBLAS worker for switch-level reachability closure
  --tri-state    allow high-Z outputs; a 0 in a one-hot output becomes high-Z
  --spec         JSON circuit spec: {"inputs":[...],"outputs":[{"name":...,"expr":...}]}
  --max-fanin N  static CMOS cell fan-in limit (e.g. 2); 0 = library default
  --heavy        allow the n=6 Dedekind count (about 40 s and 10 GB)
  --mem-limit    per-process memory cap in MiB, inherited by every child
                 (not a shared budget: N workers can each reach it). Default:
                 all of MemAvailable except 2 GiB. DCSTUDY_MEM_LIMIT_MB sets
                 the same cap; 0 = hard rlimit

Exit codes: 0 success, 1 verification/feasibility failure, 2 usage/runtime error.
)";
}

// Freed before reporting an allocation failure, so the error path does not
// have to allocate while the cap is already reached.
char* g_oom_reserve = nullptr;

// True when the cap came from --mem-limit / DCSTUDY_MEM_LIMIT_MB rather than
// from the machine, so errors can name the knob that was actually used.
bool g_mem_limit_from_flag = false;

// Install the memory cap before any command runs: --mem-limit wins, then
// DCSTUDY_MEM_LIMIT_MB, then whatever the machine can spare (MemAvailable
// minus a 2 GiB reserve).  Children inherit RLIMIT_AS, so this bounds the
// whole process tree.
void configure_mem_limit(int argc, char** argv) {
  bool explicit_limit = false;
  std::string text;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--mem-limit" && i + 1 < argc) {
      text = argv[++i];
      explicit_limit = true;
    } else if (arg.rfind("--mem-limit=", 0) == 0) {
      text = arg.substr(std::string("--mem-limit=").size());
      explicit_limit = true;
    }
  }
  if (!explicit_limit) {
    if (const char* env = std::getenv("DCSTUDY_MEM_LIMIT_MB"); env && *env) {
      text = env;
      explicit_limit = true;
    }
  }
  unsigned long mb = 0;
  g_mem_limit_from_flag = explicit_limit;
  if (explicit_limit) {
    try {
      mb = parse_mem_limit_mb(text);
    } catch (const std::invalid_argument& e) {
      throw CliError(e.what());
    }
  } else {
    const unsigned long available = available_memory_mb();
    mb = mem_limit_from_available(available);
    if (available == 0)
      std::cerr << "warning: cannot read /proc/meminfo; running without a "
                   "memory cap\n";
    else if (available <= kMemReserveMb)
      std::cerr << "warning: only " << available
                << " MB available, under the 2 GiB reserve; refusing to "
                   "allocate past 1 MB\n";
  }
  const unsigned long installed = apply_mem_limit(mb);
  if (mb != 0 && installed != 0 && installed < mb)
    std::cerr << "warning: memory cap " << mb << " MB clamped to " << installed
              << " MB by the hard rlimit\n";
}

int cmd_bool(const std::string& sub, int argc, char** argv) {
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  if (sub == "info") {
    const FuncSpec spec = get_spec(a);
    const bool realizable = (~spec.func).is_monotone();
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "bool.info";
      Json vars = Json::Array{};
      for (const auto& v : spec.vars) vars.push_back(v);
      j["vars"] = vars;
      j["truth"] = spec.func.truth_string();
      j["monotone"] = spec.func.is_monotone();
      j["self_dual"] = spec.func.is_self_dual();
      j["realizable"] = realizable;
      j["anf"] = spec.func.anf_string(spec.vars);
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "spec: " << spec.description << "\n";
      std::cout << "vars: " << join(spec.vars, ",") << "\n";
      std::cout << "truth: " << spec.func.truth_string() << "\n";
      std::cout << "monotone: " << (spec.func.is_monotone() ? "yes" : "no") << "\n";
      std::cout << "self-dual: " << (spec.func.is_self_dual() ? "yes" : "no") << "\n";
      std::cout << "single-stage CMOS: " << (realizable ? "yes" : "no") << "\n";
      std::cout << "ANF: " << spec.func.anf_string(spec.vars) << "\n";
    }
    return 0;
  }
  if (sub == "count") {
    const int n = std::stoi(a.need("n"));
    if (n < 0 || n > 6) throw CliError("--n must be in 0..6");
    if (n == 6 && !a.is("heavy"))
      throw CliError(
          "n=6 needs --heavy (about 40 s and 10 GB); n<=5 is instant");
    const auto t0 = std::chrono::steady_clock::now();
    const auto c = count_monotone(static_cast<std::size_t>(n));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "bool.count";
      j["n"] = n;
      j["count"] = c;
      j["ms"] = static_cast<std::int64_t>(ms);
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "n=" << n << "  monotone functions=" << c << "  (" << ms
                << " ms)\n";
    }
    return 0;
  }
  throw CliError("unknown bool subcommand: " + sub);
}

int cmd_cmos(const std::string& sub, int argc, char** argv) {
  const Args a = parse_args(argc, argv, 3);
  if (sub != "synth") throw CliError("unknown cmos subcommand: " + sub);
  const bool json = a.is("json");
  const FuncSpec spec = get_spec(a);
  const CmosDesign d = synthesize_cmos(spec.func, spec.vars);
  if (json) {
    Json j;
    j["ok"] = true;
    j["command"] = "cmos.synth";
    j["realizable"] = d.realizable;
    j["kappa_linear"] = d.realizable;
    j["anf_affine"] = spec.func.is_anf_affine();
    j["vars"] = [&] {
      Json v = Json::Array{};
      for (const auto& s : spec.vars) v.push_back(s);
      return v;
    }();
    j["truth"] = spec.func.truth_string();
    if (d.realizable) {
      j["pull_down"] = d.pull_down.expression(spec.vars);
      j["pull_up"] = pull_up_expression(d, spec.vars);
      j["monotone_complement"] = true;
      j["terms"] = static_cast<std::int64_t>(d.pull_down.terms.size());
      j["literals"] = d.pull_down.literals;
      j["nfet"] = d.nfet;
      j["pfet"] = d.pfet;
      j["total"] = d.total;
    } else {
      j["reason"] = d.reason;
      j["monotone_complement"] = false;
    }
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "spec: " << spec.description << "\n";
    if (!d.realizable) {
      std::cout << "single-stage complementary CMOS: no (" << d.reason << ")\n";
    } else {
      std::cout << "single-stage complementary CMOS: yes\n";
      std::cout << "pull-down = " << d.pull_down.expression(spec.vars) << "\n";
      std::cout << "pull-up   = " << pull_up_expression(d, spec.vars) << "\n";
      std::cout << "NMOS=" << d.nfet << "  PMOS=" << d.pfet
                << "  total=" << d.total << "T\n";
    }
  }
  return 0;
}

std::vector<std::pair<int, int>> parse_edges(const std::string& s) {
  std::vector<std::pair<int, int>> out;
  std::size_t start = 0;
  while (start <= s.size()) {
    const auto comma = s.find(',', start);
    std::string item = s.substr(start, comma == std::string::npos ? std::string::npos
                                                                 : comma - start);
    auto dash = item.find('-');
    if (dash == std::string::npos) throw CliError("bad edge: " + item);
    out.emplace_back(std::stoi(item.substr(0, dash)),
                     std::stoi(item.substr(dash + 1)));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

std::vector<std::vector<int>> parse_rows(const std::string& s) {
  std::vector<std::vector<int>> rows;
  std::size_t start = 0;
  while (start <= s.size()) {
    const auto semi = s.find(';', start);
    std::string row = s.substr(start, semi == std::string::npos ? std::string::npos
                                                                : semi - start);
    std::vector<int> vals;
    std::size_t p = 0;
    while (p <= row.size()) {
      const auto comma = row.find(',', p);
      std::string cell = row.substr(p, comma == std::string::npos ? std::string::npos
                                                                  : comma - p);
      vals.push_back(std::stoi(cell));
      if (comma == std::string::npos) break;
      p = comma + 1;
    }
    rows.push_back(std::move(vals));
    if (semi == std::string::npos) break;
    start = semi + 1;
  }
  return rows;
}

int cmd_math(const std::string& sub, int argc, char** argv) {
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  if (sub == "closure") {
    const auto edges = parse_edges(a.need("edges"));
    int n = 0;
    for (auto [u, v] : edges) n = std::max(n, std::max(u, v) + 1);
    if (a.has("n")) n = std::stoi(a.get("n"));
    const auto rows = graph_closure(n, edges);
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "math.closure";
      j["n"] = n;
      Json r = Json::Array{};
      for (const auto& row : rows) {
        Json one = Json::Array{};
        for (int v : row) one.push_back(v);
        r.push_back(std::move(one));
      }
      j["rows"] = std::move(r);
      std::cout << j.dump() << "\n";
    } else {
      for (int i = 0; i < n; ++i) {
        std::cout << i << ":";
        for (int v : rows[static_cast<std::size_t>(i)]) std::cout << " " << v;
        std::cout << "\n";
      }
    }
    return 0;
  }
  if (sub == "gf2") {
    const auto rows = parse_rows(a.need("rows"));
    int ncols = rows.empty() ? 0 : static_cast<int>(rows[0].size());
    if (a.has("ncols")) ncols = std::stoi(a.get("ncols"));
    const MathGf2 g = gf2_analysis(rows, ncols);
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "math.gf2";
      j["rank"] = g.rank;
      j["nullity"] = g.nullity;
      Json ns = Json::Array{};
      for (const auto& v : g.nullspace) {
        Json one = Json::Array{};
        for (int x : v) one.push_back(x);
        ns.push_back(std::move(one));
      }
      j["nullspace"] = std::move(ns);
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "rank=" << g.rank << " nullity=" << g.nullity << "\n";
      for (const auto& v : g.nullspace) {
        for (int x : v) std::cout << x << " ";
        std::cout << "\n";
      }
    }
    return 0;
  }
  throw CliError("unknown math subcommand: " + sub);
}

int cmd_cdl(const std::string& sub, int argc, char** argv) {
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  const std::string path = a.get("cdl", default_cdl_path());
  const CdlLibrary lib = CdlLibrary::load(path);
  if (sub == "list") {
    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "cdl.list";
      j["cdl"] = path;
      Json cells = Json::Array{};
      for (const auto& c : lib.cells()) {
        Json cj;
        cj["name"] = c.name;
        cj["devices"] = c.transistor_count();
        Json in = Json::Array{}, out = Json::Array{};
        for (const auto& p : c.inputs()) in.push_back(p);
        for (const auto& p : c.outputs()) out.push_back(p);
        cj["inputs"] = std::move(in);
        cj["outputs"] = std::move(out);
        if (!c.eqn.empty()) cj["eqn"] = c.eqn;
        cells.push_back(std::move(cj));
      }
      j["cells"] = std::move(cells);
      std::cout << j.dump() << "\n";
    } else {
      for (const auto& c : lib.cells()) {
        std::cout << c.name << "  " << c.transistor_count() << "T  in:";
        for (const auto& p : c.inputs()) std::cout << " " << p;
        std::cout << "  out:";
        for (const auto& p : c.outputs()) std::cout << " " << p;
        std::cout << "\n";
      }
    }
    return 0;
  }
  if (sub == "solve") {
    const Subckt& cell = lib.cell(a.need("cell"));
    std::vector<std::string> inputs = split_csv(a.get("inputs"));
    if (inputs.empty()) inputs = cell.inputs();
    const auto outputs = cell.outputs();
    const bool exact = a.is("exact");
    const SwitchAnalysis r = analyze_switch(cell, inputs, outputs, exact,
                                           a.get("vdd", "VDD"),
                                           a.get("vss", "VSS"),
                                           a.is("graphblas"));
    std::vector<std::string> truth(outputs.size());
    for (const auto& row : r.rows)
      for (std::size_t oi = 0; oi < outputs.size(); ++oi)
        truth[oi].push_back(logic_symbol(row.values[oi]));

    bool expect_ok = true;
    if (a.has("expect")) {
      const std::string exp = a.get("expect");
      expect_ok = exp.size() == truth[0].size() && exp == truth[0];
    }

    if (json) {
      Json j;
      j["ok"] = true;
      j["command"] = "cdl.solve";
      j["cell"] = cell.name;
      j["cdl"] = path;
      j["devices"] = cell.transistor_count();
      j["mode"] = r.mode;
      j["exact"] = r.exact;
      j["assumptions_ok"] = r.assumptions_ok;
      Json in = Json::Array{}, out = Json::Array{};
      for (const auto& s : inputs) in.push_back(s);
      for (const auto& s : outputs) out.push_back(s);
      j["inputs"] = std::move(in);
      j["outputs"] = std::move(out);
      Json rows = Json::Array{};
      for (const auto& row : r.rows) {
        Json rj;
        std::string sig;
        for (int b : row.inputs) sig.push_back(char('0' + b));
        rj["in"] = sig;
        Json vals = Json::Array{};
        for (auto v : row.values) vals.push_back(std::string(1, logic_symbol(v)));
        rj["values"] = std::move(vals);
        if (!row.floating_gates.empty()) {
          Json fg = Json::Array{};
          for (const auto& g : row.floating_gates) fg.push_back(g);
          rj["floating_gates"] = std::move(fg);
        }
        if (row.conflict) rj["conflict"] = true;
        rows.push_back(std::move(rj));
      }
      j["rows"] = std::move(rows);
      Json tv;
      for (std::size_t oi = 0; oi < outputs.size(); ++oi) tv[outputs[oi]] = truth[oi];
      j["truth"] = std::move(tv);
      if (a.has("expect")) j["expect_ok"] = expect_ok;
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "cell " << cell.name << ": " << cell.transistor_count()
                << " devices, mode=" << r.mode << "\n";
      std::cout << "  " << join(inputs, " ");
      for (const auto& o : outputs) std::cout << " | " << o;
      std::cout << "\n";
      for (const auto& row : r.rows) {
        for (int b : row.inputs) std::cout << b << " ";
        std::cout << "|";
        for (auto v : row.values) std::cout << " " << logic_symbol(v);
        if (!row.floating_gates.empty()) {
          std::cout << "   floating gate: " << join(row.floating_gates, ",");
        }
        if (row.conflict) std::cout << "   conflict";
        std::cout << "\n";
      }
      for (std::size_t oi = 0; oi < outputs.size(); ++oi)
        std::cout << "  " << outputs[oi] << " = " << truth[oi] << "\n";
      if (a.has("expect")) std::cout << "  expected: " << (expect_ok ? "match" : "MISMATCH") << "\n";
    }
    return expect_ok ? 0 : 1;
  }
  throw CliError("unknown cdl subcommand: " + sub);
}

struct StaticCheck {
  bool ok = false;
  std::string truth;
  std::string target;
};

StaticCheck check_static_cmos(const CmosDesign& d, const FuncSpec& spec) {
  const Subckt cell = build_static_cmos_subckt(d, spec.vars);
  const SwitchAnalysis a =
      analyze_switch(cell, spec.vars, {"Y"}, true, "VDD", "VSS");
  StaticCheck c;
  for (const auto& row : a.rows) c.truth.push_back(logic_symbol(row.values[0]));
  c.target = spec.func.truth_string();
  c.ok = c.truth == c.target;
  return c;
}

Json cells_json(const std::vector<GateInstance>& instances) {
  Json cells = Json::Array{};
  std::string topology;
  for (const auto& c : instances) {
    Json cj;
    cj["type"] = c.type;
    Json ports;
    for (const auto& [k, v] : c.ports) ports[k] = v;
    cj["ports"] = std::move(ports);
    cells.push_back(std::move(cj));
    if (!topology.empty()) topology += " → ";
    topology += c.type;
  }
  return cells;
}

Json multi_design_json(const MultiStageDesign& m) {
  Json j;
  j["kind"] = "static-cmos-multi";
  j["cost"] = m.total;
  j["nfet"] = m.nfet;
  j["pfet"] = m.pfet;
  j["inverters"] = Json::Array{};
  j["branches"] = Json::Array{};
  j["kappa_linear"] = true;
  j["single_stage"] = false;
  j["cells"] = cells_json(m.cells);
  std::string topology;
  for (const auto& c : m.cells) {
    if (!topology.empty()) topology += " → ";
    topology += c.type;
  }
  j["topology"] = topology;
  return j;
}

Json multi_design_json(const MultiStageMultiDesign& m) {
  Json j;
  j["kind"] = "static-cmos-multi";
  j["cost"] = m.total;
  j["nfet"] = m.nfet;
  j["pfet"] = m.pfet;
  j["inverters"] = Json::Array{};
  j["branches"] = Json::Array{};
  j["kappa_linear"] = true;
  j["single_stage"] = false;
  Json outs = Json::Array{};
  for (const auto& o : m.outputs) outs.push_back(o);
  j["outputs"] = std::move(outs);
  j["cells"] = cells_json(m.cells);
  Json truths;
  for (std::size_t i = 0; i < m.outputs.size(); ++i)
    truths[m.outputs[i]] = m.truths.at(i);
  j["truths"] = std::move(truths);
  std::string topology;
  for (const auto& c : m.cells) {
    if (!topology.empty()) topology += " → ";
    topology += c.type;
  }
  j["topology"] = topology;
  return j;
}

// Fan-in of a single-stage static gate is the number of distinct input
// signals its PDN uses -- not the longest product term.  Three parallel
// single-literal terms is still a 3-input NOR.
int static_gate_fanin(const CmosDesign& d) {
  std::uint64_t used = 0;
  for (const auto& c : d.pull_down.terms) used |= c.care;
  return static_cast<int>(__builtin_popcountll(used));
}

int cmd_design_static(const Args& a, const FuncSpec& spec,
                      const SpaceOptions& opt, VerifyMode vmode, bool json) {
  const CmosDesign d = synthesize_cmos(spec.func, spec.vars);
  const int max_fanin = std::stoi(a.get("max-fanin", "0"));
  if (max_fanin != 0 && (max_fanin < 2 || max_fanin > 4))
    throw CliError("--max-fanin must be 0 or 2..4");
  const int single_fanin = static_gate_fanin(d);
  const bool fanin_ok = max_fanin == 0 || single_fanin <= max_fanin;
  const bool single_ok = d.realizable && d.total <= opt.max_cost &&
      !d.pull_down.terms.empty() &&
      std::none_of(d.pull_down.terms.begin(), d.pull_down.terms.end(),
                   [](const Cube& c) { return c.literals() == 0; }) && fanin_ok;
  if (!single_ok) {
    if (vmode != VerifyMode::None && vmode != VerifyMode::Static)
      throw CliError("--static-cmos supports --verify static|none");
    const MultiStageDesign m =
        synthesize_multistage_static(spec.func, spec.vars, max_fanin);
    if (!m.ok || !m.verified) {
      if (json) {
        Json j;
        j["ok"] = false;
        j["command"] = "design.min";
        j["space"] = "static-cmos";
        j["feasible"] = false;
        j["message"] = m.reason.empty() ? "multi-stage synthesis failed" : m.reason;
        j["truth"] = spec.func.truth_string();
        j["single_stage"] = false;
        j["anf_affine"] = spec.func.is_anf_affine();
        std::cout << j.dump() << "\n";
      } else {
        std::cout << "static complementary CMOS: multi-stage failed: "
                  << m.reason << "\n";
      }
      return 1;
    }
    if (m.total > opt.max_cost) {
      if (json) {
        Json j;
        j["ok"] = false;
        j["command"] = "design.min";
        j["space"] = "static-cmos";
        j["feasible"] = false;
        j["message"] = "multi-stage static CMOS needs " +
                       std::to_string(m.total) + "T, over --max-cost";
        j["optimum"] = m.total;
        std::cout << j.dump() << "\n";
      } else {
        std::cout << "multi-stage static CMOS needs " << m.total
                  << "T, over --max-cost\n";
      }
      return 1;
    }

    Json design = multi_design_json(m);
    Json j;
    j["ok"] = true;
    j["command"] = "design.min";
    j["space"] = "static-cmos";
    j["feasible"] = true;
    j["single_stage"] = false;
    j["multi_stage"] = true;
    j["optimum"] = m.total;
    j["truth"] = spec.func.truth_string();
    j["kappa_linear"] = true;
    j["pun_dual"] = true;
    j["monotone_complement"] = false;
    j["anf_affine"] = spec.func.is_anf_affine();
    j["designs"] = [&] {
      Json arr = Json::Array{};
      arr.push_back(design);
      return arr;
    }();
    j["verification"] = [&] {
      Json v;
      v["static"] = [&] {
        Json s;
        s["ok"] = m.verified;
        s["truth"] = m.truth;
        s["target"] = spec.func.truth_string();
        s["mode"] = "z3 switch-level (flattened standard cells)";
        return s;
      }();
      return v;
    }();

    if (a.has("emit")) {
      const fs::path dir = fs::path(a.get("emit")) /
                           ("static_multi_" + std::to_string(m.total) + "T");
      fs::create_directories(dir);
      std::ofstream(dir / "static-cmos.json") << design.dump() << "\n";
      std::ofstream(dir / "gate-level.v") << m.verilog;
      std::ofstream(dir / "netlist.json") << m.json;
      std::ofstream(dir / "flattened.cdl") << m.cdl;
      j["files"] = [&] {
        Json v = Json::Array{};
        v.push_back((dir / "static-cmos.json").string());
        v.push_back((dir / "gate-level.v").string());
        v.push_back((dir / "netlist.json").string());
        v.push_back((dir / "flattened.cdl").string());
        return v;
      }();
    }

    if (json) {
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "multi-stage static CMOS: " << m.total << "T ("
                << m.nfet << " NMOS + " << m.pfet << " PMOS)\n";
      std::cout << "  cells:";
      for (const auto& c : m.cells) std::cout << " " << c.type;
      std::cout << "\n  switch-level: " << (m.verified ? "PASS" : "FAIL")
                << "  truth=" << m.truth << "\n";
      if (a.has("emit")) std::cout << "  emitted: " << a.get("emit") << "\n";
    }
    return 0;
  }
  if (!d.realizable) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.min";
      j["space"] = "static-cmos";
      j["feasible"] = false;
      j["message"] = d.reason;
      j["truth"] = spec.func.truth_string();
      j["monotone_complement"] = false;
      j["kappa_linear"] = false;
      j["anf_affine"] = spec.func.is_anf_affine();
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "static complementary CMOS: no (" << d.reason << ")\n";
    }
    return 1;
  }
  if (d.pull_down.terms.empty() ||
      std::any_of(d.pull_down.terms.begin(), d.pull_down.terms.end(),
                  [](const Cube& c) { return c.literals() == 0; })) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.min";
      j["space"] = "static-cmos";
      j["feasible"] = false;
      j["message"] = "constant functions are not supported by the static CMOS emitter";
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "static complementary CMOS: constant functions are not supported\n";
    }
    return 1;
  }
  if (d.total > opt.max_cost) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.min";
      j["space"] = "static-cmos";
      j["feasible"] = false;
      j["message"] = "static CMOS needs " + std::to_string(d.total) +
                     "T, over --max-cost";
      j["optimum"] = d.total;
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "static CMOS needs " << d.total << "T, over --max-cost\n";
    }
    return 1;
  }
  if (vmode != VerifyMode::None && vmode != VerifyMode::Static)
    throw CliError("--static-cmos supports --verify static|none");

  Json verification;
  bool check_ok = true;
  if (vmode == VerifyMode::Static) {
    const StaticCheck c = check_static_cmos(d, spec);
    check_ok = c.ok;
    verification["static"] = [&] {
      Json v;
      v["ok"] = c.ok;
      v["truth"] = c.truth;
      v["target"] = c.target;
      v["mode"] = "z3 switch-level";
      return v;
    }();
  }

  Json design = static_design_json(d, spec.vars);
  Json j;
  j["ok"] = check_ok;
  j["command"] = "design.min";
  j["space"] = "static-cmos";
  j["feasible"] = true;
  j["single_stage"] = true;
  j["multi_stage"] = false;
  j["optimum"] = d.total;
  j["truth"] = spec.func.truth_string();
  j["monotone_complement"] = true;
  j["kappa_linear"] = true;
  j["pun_dual"] = true;
  j["anf_affine"] = spec.func.is_anf_affine();
  j["designs"] = [&] {
    Json arr = Json::Array{};
    arr.push_back(design);
    return arr;
  }();
  if (vmode != VerifyMode::None) j["verification"] = verification;

  if (a.has("emit")) {
    const fs::path dir = fs::path(a.get("emit")) /
                         ("static_" + std::to_string(d.total) + "T");
    fs::create_directories(dir);
    std::ofstream(dir / "static-cmos.json") << design.dump() << "\n";
    std::ofstream(dir / "static-cmos.cdl")
        << subckt_to_cdl(build_static_cmos_subckt(d, spec.vars));
    j["files"] = [&] {
      Json v = Json::Array{};
      v.push_back((dir / "static-cmos.json").string());
      v.push_back((dir / "static-cmos.cdl").string());
      return v;
    }();
  }

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "static complementary CMOS: " << d.total << "T ("
              << d.nfet << " NMOS + " << d.pfet << " PMOS)\n";
    std::cout << "  PDN = " << d.pull_down.expression(spec.vars) << "\n";
    std::cout << "  PUN = " << pull_up_expression(d, spec.vars) << "\n";
    if (vmode == VerifyMode::Static)
      std::cout << "  switch-level: " << (check_ok ? "PASS" : "FAIL")
                << "  truth=" << verification["static"]["truth"].as_string()
                << "\n";
    if (a.has("emit")) std::cout << "  emitted: " << a.get("emit") << "\n";
  }
  return check_ok ? 0 : 1;
}

int cmd_design_template(const Args& a, const FuncSpec& spec) {
  const BddSwitchDesign b = synthesize_bdd_switch_best(spec.func, spec.vars);
  if (!b.ok || !b.verified) {
    if (a.is("json")) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.template";
      j["reason"] = b.reason;
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "template seed failed: " << b.reason << "\n";
    }
    return 1;
  }
  std::vector<int> target;
  for (std::uint32_t m = 0; m < spec.func.num_minterms(); ++m)
    target.push_back(spec.func.value(m) ? 1 : 0);
  const NetworkTemplate t = template_from_graph(b.graph, b.inverters);
  const TemplateResult r = optimize_template(t, spec.vars, {target});
  const bool json = a.is("json");
  if (!r.ok || !r.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.template";
      j["reason"] = r.reason;
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "template optimization failed: " << r.reason << "\n";
    }
    return 1;
  }
  Json j;
  j["ok"] = true;
  j["command"] = "design.template";
  j["model"] = "fixed candidate graph, optional edge variables";
  j["total"] = r.total;
  j["edge_cost"] = r.edge_cost;
  j["inverter_cost"] = r.inverter_cost;
  j["proof"] = r.proof;
  j["verified"] = r.verified;
  j["selected_edges"] = [&] {
    Json v = Json::Array{};
    for (int e : r.selected_edges) v.push_back(e);
    return v;
  }();
  j["graph"] = [&] {
    Json graph;
    Json nets = Json::Array{};
    for (const auto& n : r.graph.nets) nets.push_back(n);
    graph["nets"] = std::move(nets);
    Json edges = Json::Array{};
    for (const auto& e : r.graph.edges) {
      Json ej;
      ej["from"] = e.from;
      ej["to"] = e.to;
      ej["kind"] = switch_name(e.kind);
      ej["lit"] = e.lit;
      ej["cost"] = e.cost;
      ej["strong"] = e.strong;
      edges.push_back(std::move(ej));
    }
    graph["edges"] = std::move(edges);
    Json outs = Json::Array{};
    for (int o : r.graph.outputs) outs.push_back(o);
    graph["outputs"] = std::move(outs);
    return graph;
  }();
  if (json) std::cout << j.dump() << "\n";
  else
    std::cout << "template-optimized switch graph: " << r.total << "T\n"
              << "  edges=" << r.edge_cost << "T  inverters="
              << r.inverter_cost << "T  proof="
              << (r.proof ? "exhaustive" : "greedy") << "\n";
  return 0;
}

int cmd_design_path(const Args& a, const FuncSpec& spec) {
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  const PathSynthesisResult d =
      synthesize_path_variables(spec.func, spec.vars, opt, a.is("tri-state"));
  const bool json = a.is("json");
  if (!d.ok || !d.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.path";
      j["reason"] = d.reason;
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "design path failed: " << d.reason << "\n";
    }
    return 1;
  }
  Json j;
  j["ok"] = true;
  j["command"] = "design.path";
  j["model"] = "path-variable cover with shared switch prefixes";
  j["total"] = d.total;
  j["edge_cost"] = d.edge_cost;
  j["inverter_cost"] = d.inverter_cost;
  j["proof"] = d.proof;
  j["verified"] = d.verified;
  j["selected"] = [&] {
    Json v = Json::Array{};
    for (int s : d.selected) v.push_back(s);
    return v;
  }();
  Json graph;
  Json nets = Json::Array{};
  for (const auto& n : d.graph.nets) nets.push_back(n);
  graph["nets"] = std::move(nets);
  Json edges = Json::Array{};
  for (const auto& e : d.graph.edges) {
    Json ej;
    ej["from"] = e.from;
    ej["to"] = e.to;
    ej["kind"] = switch_name(e.kind);
    ej["lit"] = e.lit;
    ej["strong"] = e.strong;
    edges.push_back(std::move(ej));
  }
  graph["edges"] = std::move(edges);
  Json sources = Json::Array{};
  for (int s : d.graph.source_ids) sources.push_back(s);
  graph["sources"] = std::move(sources);
  graph["output"] = d.graph.output;
  j["graph"] = std::move(graph);
  if (json) std::cout << j.dump() << "\n";
  else
    std::cout << "path-variable switch design: " << d.total << "T\n"
              << "  edges=" << d.edge_cost << "T  inverters="
              << d.inverter_cost << "T\n"
              << "  proof: exhaustive within candidate path set\n";
  return 0;
}

int cmd_design_bdd(const Args& a, const FuncSpec& spec) {
  const BddSwitchDesign d = synthesize_bdd_switch_best(spec.func, spec.vars);
  const bool json = a.is("json");
  if (!d.ok || !d.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "design.bdd";
      j["reason"] = d.reason;
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "design bdd failed: " << d.reason << "\n";
    }
    return 1;
  }
  Json j;
  j["ok"] = true;
  j["command"] = "design.bdd";
  j["model"] = "BDD-derived switch DAG";
  j["total"] = d.total;
  j["nodes"] = d.nodes;
  j["inverters"] = d.inverters;
  j["order"] = [&] {
    Json v = Json::Array{};
    for (int i : d.order) v.push_back(spec.vars.at(static_cast<std::size_t>(i)));
    return v;
  }();
  j["verified"] = d.verified;
  j["truth"] = d.truths.empty() ? "" : d.truths[0];
  Json graph;
  Json nets = Json::Array{};
  for (const auto& n : d.graph.nets) nets.push_back(n);
  graph["nets"] = std::move(nets);
  Json edges = Json::Array{};
  for (const auto& e : d.graph.edges) {
    Json ej;
    ej["from"] = e.from;
    ej["to"] = e.to;
    ej["kind"] = switch_name(e.kind);
    ej["lit"] = e.lit;
    ej["strong"] = e.strong;
    edges.push_back(std::move(ej));
  }
  graph["edges"] = std::move(edges);
  Json sources = Json::Array{};
  for (int s : d.graph.source_ids) sources.push_back(s);
  graph["sources"] = std::move(sources);
  graph["output"] = d.graph.output;
  j["graph"] = std::move(graph);
  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "BDD switch DAG: " << d.total << "T\n";
    std::cout << "  nodes=" << d.nodes << "  input inverters="
              << d.inverters << "\n";
    std::cout << "  switch-level drivers: PASS\n";
  }
  return 0;
}

int cmd_design(const std::string& sub, int argc, char** argv) {
  if (sub == "bdd") {
    const Args a = parse_args(argc, argv, 3);
    return cmd_design_bdd(a, get_spec(a));
  }
  if (sub == "path") {
    const Args a = parse_args(argc, argv, 3);
    return cmd_design_path(a, get_spec(a));
  }
  if (sub == "template") {
    const Args a = parse_args(argc, argv, 3);
    return cmd_design_template(a, get_spec(a));
  }
  if (sub != "min") throw CliError("unknown design subcommand: " + sub);
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  const FuncSpec spec = get_spec(a);
  if (spec.vars.size() > 4) throw CliError("design min supports at most 4 variables");
  const bool static_mode = a.is("static-cmos");
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", static_mode ? "32" : "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  if (opt.max_cost < 1 || opt.max_cost > 32) throw CliError("--max-cost out of range");
  if (opt.max_chain < 1 || opt.max_chain > 8) throw CliError("--max-chain out of range");
  const unsigned long limit_raw =
      std::stoul(a.get("limit", a.is("all") ? "64" : "8"));
  if (limit_raw == 0 || limit_raw > 1024)
    throw CliError("--limit must be in 1..1024");
  const std::size_t limit = static_cast<std::size_t>(limit_raw);
  const VerifyMode vmode =
      parse_verify(a, static_mode ? VerifyMode::Static : VerifyMode::Yosys);
  // --verify static is a switch-level proof of the single-stage static gate;
  // without --static-cmos there is no such gate, and silently returning an
  // unverified design would be a false green.
  if (!static_mode && vmode == VerifyMode::Static)
    throw CliError("--verify static requires --static-cmos");
  if (static_mode) return cmd_design_static(a, spec, opt, vmode, json);

  const auto result = synthesize_min(spec.vars, target_of(spec), opt, limit,
                                     a.is("canonical"));

  Json j;
  j["ok"] = result.feasible;
  j["command"] = "design.min";
  j["vars"] = [&] {
    Json v = Json::Array{};
    for (const auto& s : spec.vars) v.push_back(s);
    return v;
  }();
  j["truth"] = spec.func.truth_string();
  j["branch_count"] = result.branch_count;
  j["optimum"] = result.optimum;
  j["feasible"] = result.feasible;
  if (!result.message.empty()) j["message"] = result.message;

  Json designs = Json::Array{};
  bool all_verified = true;
  for (std::size_t i = 0; i < result.designs.size(); ++i) {
    const auto& d = result.designs[i];
    fs::path emit_dir;
    if (a.has("emit"))
      emit_dir = fs::path(a.get("emit")) /
                 ("design_" + std::to_string(i) + "_" + std::to_string(d.cost) + "T");
    const auto v = verify_design(d, spec, vmode, a.is("keep"),
                                 a.has("emit") ? &emit_dir : nullptr);
    if ((vmode == VerifyMode::Yosys || vmode == VerifyMode::Both) && !v.yosys)
      all_verified = false;
    if ((vmode == VerifyMode::Sim || vmode == VerifyMode::Both) && !v.sim)
      all_verified = false;
    Json dj = design_json(d, spec.vars);
    if (!d.branches.empty()) {
      const DriverProfile dp =
          analyze_design_drivers(d, spec.vars, target_of(spec));
      dj["drivers"] = driver_profile_json(dp);
      if (!dp.valid) all_verified = false;
    }
    if (vmode != VerifyMode::None) dj["verification"] = v.json;
    if (a.has("emit")) dj["files"] = emit_dir.string();
    designs.push_back(std::move(dj));
  }
  j["designs"] = std::move(designs);
  j["ok"] = result.feasible && all_verified;

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "issue: " << spec.description << "  truth=" << spec.func.truth_string()
              << "\n";
    std::cout << "space: " << result.branch_count << " usable branches\n";
    if (!result.feasible) {
      std::cout << "no design: " << result.message << "\n";
    } else {
      std::cout << "optimum: " << result.optimum << "T  designs="
                << result.designs.size() << "\n";
      for (std::size_t i = 0; i < result.designs.size(); ++i) {
        const auto& d = result.designs[i];
        std::cout << "  [" << i << "] " << d.cost << "T  inverters="
                  << d.inverter_names(spec.vars) << "  branches="
                  << d.branches.size() << "\n";
        for (const auto& b : d.branches) std::cout << "       " << b.topo(spec.vars) << "\n";
        if (vmode == VerifyMode::Yosys || vmode == VerifyMode::Both)
          std::cout << "       yosys: "
                    << (j["designs"].array().at(i).at("verification").at("yosys").at("ok")
                                .dump(0) == "true"
                            ? "PASS"
                            : "FAIL")
                    << "\n";
        if (vmode == VerifyMode::Sim || vmode == VerifyMode::Both)
          std::cout << "       verilator: "
                    << (j["designs"].array().at(i).at("verification").at("verilator").at("ok")
                                .dump(0) == "true"
                            ? "PASS"
                            : "FAIL")
                    << "\n";
      }
    }
  }
  if (!result.feasible) return 1;
  return all_verified ? 0 : 1;
}

int cmd_verify(const std::string& sub, int argc, char** argv) {
  const Args a = parse_args(argc, argv, 3);
  const bool json = a.is("json");
  const FuncSpec spec = get_spec(a);
  std::ifstream in(a.need("dut"));
  if (!in) throw CliError("cannot read DUT file: " + a.get("dut"));
  std::ostringstream os;
  os << in.rdbuf();
  const std::string dut = os.str();
  ToolResult r;
  if (sub == "equiv")
    r = prove_yosys(dut, spec.func, spec.vars, a.is("keep"));
  else if (sub == "sim")
    r = simulate_verilator(dut, spec.func, spec.vars, a.is("keep"));
  else
    throw CliError("unknown verify subcommand: " + sub);
  if (json) {
    Json j;
    j["ok"] = r.ok;
    j["command"] = "verify." + sub;
    j["result"] = verify_json(r);
    std::cout << j.dump() << "\n";
  } else {
    std::cout << (r.ok ? "PASS" : "FAIL") << "  (exit " << r.exit_code << ")\n";
    std::cout << r.output;
  }
  return r.ok ? 0 : 1;
}

struct MultiSpec {
  std::string title;
  std::vector<std::string> vars;
  std::vector<std::string> names;
  std::vector<BoolFunc> funcs;
  // Per-output don't-care mask: bit m set => row m may be either value.
  std::vector<Bits> any;
};

MultiSpec load_multi_spec(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw CliError("cannot read spec: " + path);
  std::ostringstream os;
  os << in.rdbuf();
  const Json root = Json::parse(os.str());
  MultiSpec s;
  s.title = root.contains("name") ? root.at("name").as_string() : fs::path(path).stem().string();
  const Json& vars = root.at("inputs");
  if (!vars.is_array() || vars.array().empty())
    throw CliError("spec.inputs must be a non-empty array");
  for (const auto& v : vars.array()) s.vars.push_back(v.as_string());
  if (s.vars.size() > 4) throw CliError("multi synth supports at most 4 inputs");
  const Json& outs = root.at("outputs");
  if (!outs.is_array() || outs.array().empty())
    throw CliError("spec.outputs must be a non-empty array");
  for (const auto& o : outs.array()) {
    std::string name, expr;
    if (o.is_object()) {
      name = o.at("name").as_string();
      expr = o.at("expr").as_string();
    } else if (o.is_string()) {
      const std::string t = o.as_string();
      const auto eq = t.find('=');
      if (eq == std::string::npos) throw CliError("output string must be name=expr");
      name = t.substr(0, eq);
      expr = t.substr(eq + 1);
    } else {
      throw CliError("each output must be {name, expr} or name=expr");
    }
    if (name.empty() || name == "VDD" || name == "VSS")
      throw CliError("bad output name: " + name);
    Bits any_mask = Bits::zeros(std::size_t{1} << s.vars.size());
    if (o.is_object() && o.contains("any")) {
      const std::string t = o.at("any").as_string();
      try {
        any_mask = spec_from_truth(t, s.vars).func.bits();
      } catch (const std::exception& e) {
        throw CliError("bad any mask for " + name + ": " + e.what());
      }
      if (any_mask.count() == any_mask.size())
        throw CliError("output " + name +
                       " has no care rows (any is all 1s)");
    }
    if (std::find(s.names.begin(), s.names.end(), name) != s.names.end())
      throw CliError("duplicate output name: " + name);
    if (std::find(s.vars.begin(), s.vars.end(), name) != s.vars.end())
      throw CliError("output collides with input: " + name);
    s.names.push_back(name);
    s.funcs.push_back(spec_from_expr(expr, s.vars).func);
    s.any.push_back(std::move(any_mask));
  }
  return s;
}

// Echo the don't-care rows of a spec into a result object.
void add_any_json(Json& j, const MultiSpec& spec) {
  if (spec.any.empty()) return;
  Json any = Json::Object{};
  bool found = false;
  for (std::size_t i = 0; i < spec.any.size(); ++i) {
    if (spec.any[i].none()) continue;
    any[spec.names[i]] = spec.any[i].to_binary_string();
    found = true;
  }
  if (found) j["any"] = std::move(any);
}

int cmd_multi_template(const Args& a, const MultiSpec& spec) {
  SpaceOptions opt;
  opt.max_cost = 32;
  opt.max_chain = 4;
  opt.allow_tg = !a.is("no-tg");
  const MultiPathSynthesisResult seed =
      synthesize_path_variables_multi(spec.funcs, spec.names, spec.vars, opt,
                                      false, spec.any);
  if (!seed.ok || !seed.verified) {
    if (a.is("json")) {
      Json j;
      j["ok"] = false;
      j["command"] = "multi.template";
      j["reason"] = seed.reason;
      std::cout << j.dump() << "\n";
    }
    return 1;
  }
  std::vector<std::vector<int>> targets;
  for (const auto& f : spec.funcs) {
    std::vector<int> t;
    for (std::uint32_t m = 0; m < f.num_minterms(); ++m)
      t.push_back(f.value(m) ? 1 : 0);
    targets.push_back(std::move(t));
  }
  const NetworkTemplate tmpl = template_from_graph(seed.graph, seed.inverter_cost);
  const TemplateResult r = optimize_template(tmpl, spec.vars, targets);
  const bool json = a.is("json");
  if (!r.ok || !r.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "multi.template";
      j["reason"] = r.reason;
      std::cout << j.dump() << "\n";
    }
    return 1;
  }
  Json j;
  j["ok"] = true;
  j["command"] = "multi.template";
  j["total"] = r.total;
  j["edge_cost"] = r.edge_cost;
  j["inverter_cost"] = r.inverter_cost;
  j["proof"] = r.proof;
  j["verified"] = r.verified;
  j["selected_edges"] = [&] {
    Json v = Json::Array{};
    for (int e : r.selected_edges) v.push_back(e);
    return v;
  }();
  if (json) std::cout << j.dump() << "\n";
  else
    std::cout << "multi-output template: " << r.total << "T ("
              << r.edge_cost << "T edges + " << r.inverter_cost
              << "T inverters)\n";
  return 0;
}

int cmd_multi_path(const Args& a, const MultiSpec& spec) {
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", "64"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  const MultiPathSynthesisResult r =
      synthesize_path_variables_multi(spec.funcs, spec.names, spec.vars, opt,
                                      a.is("tri-state"), spec.any);
  const bool json = a.is("json");
  std::string fallback_note;
  if ((!r.ok || !r.verified) && !a.is("tri-state")) {
    const StaticExactResult s =
        synthesize_static_exact_multi(spec.funcs, spec.names, spec.vars,
                                      spec.any);
    if (s.ok && s.verified && s.total > opt.max_cost)
      fallback_note = " (static fallback needs " + std::to_string(s.total) +
                      "T, over --max-cost " + std::to_string(opt.max_cost) + ")";
    if (s.ok && s.verified && s.total <= opt.max_cost) {
      Json j;
      j["ok"] = true;
      j["command"] = "multi.path";
      j["mode"] = "static-exact-fallback";
      j["path_reason"] = r.reason;
      add_any_json(j, spec);
      j["outputs"] = [&] {
        Json v = Json::Array{};
        for (const auto& n : spec.names) v.push_back(n);
        return v;
      }();
      j["total"] = s.total;
      j["gate_cost"] = s.gate_cost;
      j["inverter_cost"] = s.inverter_cost;
      j["proof"] = s.proof;
      j["proof_method"] = s.proof_method;
      j["proof_scope"] = "single-stage static CMOS model";
      j["verified"] = s.verified;
      std::cout << j.dump() << "\n";
      return 0;
    }
  }
  if (!r.ok || !r.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "multi.path";
      j["reason"] = r.reason + fallback_note;
      Json edges = Json::Array{};
      for (const auto& e : r.graph.edges) {
        Json ej;
        ej["from"] = e.from;
        ej["to"] = e.to;
        ej["kind"] = switch_name(e.kind);
        ej["lit"] = e.lit;
        edges.push_back(std::move(ej));
      }
      Json nets = Json::Array{};
      for (const auto& n : r.graph.nets) nets.push_back(n);
      Json sources = Json::Array{};
      for (int s : r.graph.source_ids) sources.push_back(s);
      Json outs = Json::Array{};
      for (int o : r.graph.outputs) outs.push_back(o);
      j["graph"] = Json::Object{{"edges", edges}, {"nets", nets},
                                {"sources", sources}, {"outputs", outs}};
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "multi path failed: " << r.reason << fallback_note << "\n";
    }
    return 1;
  }
  Json j;
  j["ok"] = true;
  j["command"] = "multi.path";
  j["mode"] = a.is("tri-state") ? "tri-state" : "dual-rail";
  add_any_json(j, spec);
  j["model"] = a.is("tri-state")
                   ? "multi-output path variables with shared switch edges"
                   : "PUN/PDN dual-rail path variables with shared switch edges";
  j["total"] = r.total;
  j["edge_cost"] = r.edge_cost;
  j["inverter_cost"] = r.inverter_cost;
  j["proof"] = r.proof;
  j["proof_method"] = r.proof_method;
  j["proof_scope"] = r.proof_scope;
  j["verified"] = r.verified;
  Json outs = Json::Array{};
  for (const auto& o : r.outputs) outs.push_back(o);
  j["outputs"] = std::move(outs);
  j["selected"] = [&] {
    Json v = Json::Array{};
    for (int s : r.selected) v.push_back(s);
    return v;
  }();
  Json graph;
  Json nets = Json::Array{};
  for (const auto& n : r.graph.nets) nets.push_back(n);
  graph["nets"] = std::move(nets);
  Json edges = Json::Array{};
  for (const auto& e : r.graph.edges) {
    Json ej;
    ej["from"] = e.from;
    ej["to"] = e.to;
    ej["kind"] = switch_name(e.kind);
    ej["lit"] = e.lit;
    ej["strong"] = e.strong;
    edges.push_back(std::move(ej));
  }
  graph["edges"] = std::move(edges);
  Json sources = Json::Array{};
  for (int s : r.graph.source_ids) sources.push_back(s);
  graph["sources"] = std::move(sources);
  Json gouts = Json::Array{};
  for (int o : r.graph.outputs) gouts.push_back(o);
  graph["outputs"] = std::move(gouts);
  j["graph"] = std::move(graph);
  if (json) std::cout << j.dump() << "\n";
  else
    std::cout << "multi-output path design: " << r.total << "T\n"
              << "  edges=" << r.edge_cost << "T  shared inverters="
              << r.inverter_cost << "T\n";
  return 0;
}

int cmd_multi_exact(const Args& a, const MultiSpec& spec) {
  const StaticExactResult r =
      synthesize_static_exact_multi(spec.funcs, spec.names, spec.vars,
                                    spec.any);
  const bool json = a.is("json");
  if (!r.ok || !r.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "multi.exact";
      j["reason"] = r.reason;
      j["verified"] = r.verified;
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "multi exact failed: " << r.reason << "\n";
    }
    return 1;
  }

  Json j;
  j["ok"] = true;
  j["command"] = "multi.exact";
  j["model"] = "single-stage static complex CMOS with shared input inverters";
  j["title"] = spec.title;
  add_any_json(j, spec);
  j["optimal"] = r.total;
  j["total"] = r.total;
  j["gate_cost"] = r.gate_cost;
  j["inverter_cost"] = r.inverter_cost;
  j["nfet"] = r.nfet;
  j["pfet"] = r.pfet;
  j["proof"] = r.proof;
  j["proof_method"] = r.proof_method;
  j["proof_scope"] = "single-stage static CMOS model";
  j["verified"] = r.verified;
  Json outs = Json::Array{};
  Json truths;
  Json inverted = Json::Array{};
  Json gates = Json::Array{};
  for (std::size_t i = 0; i < spec.vars.size(); ++i)
    if ((r.inverted_mask >> i) & 1U) inverted.push_back(spec.vars[i]);
  for (std::size_t k = 0; k < r.outputs.size(); ++k) {
    outs.push_back(r.outputs[k]);
    truths[r.outputs[k]] = r.truths.at(k);
    Json g;
    g["output"] = r.outputs[k];
    g["pdn"] = r.pdn.at(k).expression(spec.vars);
    g["literals"] = r.pdn.at(k).literals;
    g["transistors"] = 2 * r.pdn.at(k).literals;
    gates.push_back(std::move(g));
  }
  j["outputs"] = std::move(outs);
  j["truths"] = std::move(truths);
  j["inverted"] = std::move(inverted);
  j["gates"] = std::move(gates);

  const std::string out_dir = a.get("out");
  if (!out_dir.empty()) {
    const fs::path dir(out_dir);
    fs::create_directories(dir);
    std::ofstream(dir / "flattened.cdl") << r.cdl;
    std::ofstream(dir / "exact.json") << j.dump() << "\n";
    Json files = Json::Array{};
    for (const char* f : {"flattened.cdl", "exact.json"})
      files.push_back((dir / f).string());
    j["files"] = std::move(files);
  }

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "multi-output exact static CMOS: " << spec.title << "\n";
    std::cout << "  " << r.total << "T  ("
              << r.gate_cost << "T gates + " << r.inverter_cost
              << "T shared inverters)\n";
    std::cout << "  " << r.nfet << " NMOS + " << r.pfet << " PMOS\n";
    std::cout << "  inverted inputs:";
    bool any = false;
    for (std::size_t i = 0; i < spec.vars.size(); ++i)
      if ((r.inverted_mask >> i) & 1U) {
        std::cout << " " << spec.vars[i];
        any = true;
      }
    if (!any) std::cout << " none";
    std::cout << "\n";
    if (r.proof_method == "analytic_shortcut")
      std::cout << "  proof: analytic -- every output is a single cube, so the\n"
                   "         minimum PDN literals and inverter set are fixed\n";
    else
      std::cout << "  proof: exhaustive over all 2^" << spec.vars.size()
                << " inverter sets + exact minimum DNF per output\n";
    std::cout << "  switch-level: PASS\n";
  }
  return 0;
}

int cmd_multi(const std::string& sub, int argc, char** argv) {
  const Args a = parse_args(argc, argv, 3);
  if (sub == "exact") {
    const MultiSpec spec = load_multi_spec(a.need("spec"));
    return cmd_multi_exact(a, spec);
  }
  if (sub == "path") {
    const MultiSpec spec = load_multi_spec(a.need("spec"));
    return cmd_multi_path(a, spec);
  }
  if (sub == "template") {
    const MultiSpec spec = load_multi_spec(a.need("spec"));
    return cmd_multi_template(a, spec);
  }
  if (sub != "synth") throw CliError("unknown multi subcommand: " + sub);
  const MultiSpec spec = load_multi_spec(a.need("spec"));
  const int max_fanin = std::stoi(a.get("max-fanin", "0"));
  if (max_fanin != 0 && (max_fanin < 2 || max_fanin > 4))
    throw CliError("--max-fanin must be 0 or 2..4");
  const MultiStageMultiDesign m =
      synthesize_multistage_static_multi(spec.funcs, spec.names, spec.vars,
                                         max_fanin, spec.any);
  const bool json = a.is("json");
  if (!m.ok || !m.verified) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "multi.synth";
      j["reason"] = m.reason;
      j["verified"] = m.verified;
      Json truths;
      for (std::size_t i = 0; i < m.outputs.size() && i < m.truths.size(); ++i)
        truths[m.outputs[i]] = m.truths[i];
      j["truths"] = std::move(truths);
      std::cout << j.dump() << "\n";
    } else {
      std::cerr << "multi synth failed: " << m.reason << "\n";
      for (std::size_t i = 0; i < m.outputs.size() && i < m.truths.size(); ++i)
        std::cerr << "  " << m.outputs[i] << " = " << m.truths[i] << "\n";
    }
    return 1;
  }

  Json design = multi_design_json(m);
  const std::string out_dir = a.get("out");
  Json j;
  j["ok"] = true;
  j["command"] = "multi.synth";
  j["title"] = spec.title;
  add_any_json(j, spec);
  j["total"] = m.total;
  j["nfet"] = m.nfet;
  j["pfet"] = m.pfet;
  j["cells"] = static_cast<int>(m.cells.size());
  Json outs = Json::Array{};
  for (const auto& o : m.outputs) outs.push_back(o);
  j["outputs"] = std::move(outs);
  Json truths;
  for (std::size_t i = 0; i < m.outputs.size(); ++i)
    truths[m.outputs[i]] = m.truths[i];
  j["truths"] = truths;
  j["verified"] = m.verified;
  j["design"] = design;

  if (!out_dir.empty()) {
    const fs::path dir(out_dir);
    fs::create_directories(dir);
    std::ofstream(dir / "gate-level.v") << m.verilog;
    std::ofstream(dir / "netlist.json") << m.json;
    std::ofstream(dir / "flattened.cdl") << m.cdl;
    std::ofstream(dir / "design.json") << design.dump() << "\n";
    Json files = Json::Array{};
    for (const char* f : {"gate-level.v", "netlist.json", "flattened.cdl",
                          "design.json"})
      files.push_back((dir / f).string());
    j["files"] = std::move(files);

    Json req;
    req["op"] = "render";
    req["out"] = dir.string();
    req["vars"] = [&] {
      Json v = Json::Array{};
      for (const auto& s : spec.vars) v.push_back(s);
      return v;
    }();
    req["title"] = spec.title;
    req["truth"] = m.truths.empty() ? "" : m.truths.front();
    req["truths"] = truths;
    req["outputs"] = [&] {
      Json v = Json::Array{};
      for (const auto& o : m.outputs) v.push_back(o);
      return v;
    }();
    req["png"] = a.is("png");
    req["logic"] = false;
    req["designs"] = Json::Array{design};
    const Json resp = render_call(req);
    j["render"] = resp;
  } else if (a.is("png")) {
    throw CliError("--png requires --out DIR");
  }

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "multi-output static CMOS: " << spec.title << "\n";
    std::cout << "  " << m.total << "T  (" << m.nfet << " NMOS + "
              << m.pfet << " PMOS), " << m.cells.size() << " cells\n";
    std::cout << "  outputs: " << join(m.outputs, ", ") << "\n";
    for (std::size_t i = 0; i < m.outputs.size(); ++i)
      std::cout << "  " << m.outputs[i] << " = " << m.truths[i] << "\n";
    std::cout << "  switch-level: PASS\n";
    if (j.contains("render"))
      std::cout << "  rendered: " << j.at("render").at("index").as_string()
                << "\n";
  }
  return 0;
}

int cmd_render_static(const Args& a, const FuncSpec& spec,
                      const SpaceOptions& opt, bool json) {
  const CmosDesign d = synthesize_cmos(spec.func, spec.vars);
  const std::string out = a.get("out", "out/render");
  auto fail = [&](const std::string& message) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "render";
      j["space"] = "static-cmos";
      j["message"] = message;
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "static complementary CMOS: " << message << "\n";
    }
    return 1;
  };
  const int max_fanin = std::stoi(a.get("max-fanin", "0"));
  if (max_fanin != 0 && (max_fanin < 2 || max_fanin > 4))
    throw CliError("--max-fanin must be 0 or 2..4");
  const int single_fanin = static_gate_fanin(d);
  const bool fanin_ok = max_fanin == 0 || single_fanin <= max_fanin;
  const bool single_ok = d.realizable && d.total <= opt.max_cost &&
      !d.pull_down.terms.empty() &&
      std::none_of(d.pull_down.terms.begin(), d.pull_down.terms.end(),
                   [](const Cube& c) { return c.literals() == 0; }) && fanin_ok;

  Json design;
  int total = 0;
  std::string topo;
  if (single_ok) {
    design = static_design_json(d, spec.vars);
    total = d.total;
    topo = design.at("topology").as_string();
  } else {
    const MultiStageDesign m =
        synthesize_multistage_static(spec.func, spec.vars, max_fanin);
    if (!m.ok || !m.verified) return fail(m.reason.empty() ? "multi-stage failed" : m.reason);
    if (m.total > opt.max_cost)
      return fail("multi-stage needs " + std::to_string(m.total) + "T, over --max-cost");
    design = multi_design_json(m);
    total = m.total;
    topo = design.at("topology").as_string();
  }

  Json req;
  req["op"] = "render";
  req["out"] = out;
  req["title"] = spec.description;
  req["vars"] = [&] {
    Json v = Json::Array{};
    for (const auto& s : spec.vars) v.push_back(s);
    return v;
  }();
  req["truth"] = spec.func.truth_string();
  req["png"] = a.is("png");
  req["canonical"] = false;
  req["logic"] = !a.is("no-logic");
  Json designs = Json::Array{};
  designs.push_back(design);
  req["designs"] = std::move(designs);

  const Json resp = render_call(req);
  Json j;
  j["ok"] = true;
  j["command"] = "render";
  j["space"] = "static-cmos";
  j["out"] = out;
  j["optimum"] = total;
  j["single_stage"] = single_ok;
  j["topology"] = topo;
  j["designs"] = [&] {
    Json v = Json::Array{};
    v.push_back(design);
    return v;
  }();
  if (resp.contains("index")) j["index"] = resp.at("index");
  if (resp.contains("files")) j["files"] = resp.at("files");
  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "rendered static CMOS " << total << "T to " << out << "\n";
    std::cout << "  " << topo << "\n";
    if (resp.contains("index")) std::cout << "  index: " << resp.at("index").as_string() << "\n";
  }
  return 0;
}

int cmd_render(int argc, char** argv) {
  const Args a = parse_args(argc, argv, 2);
  const bool json = a.is("json");
  const FuncSpec spec = get_spec(a);
  if (spec.vars.size() > 4) throw CliError("render supports at most 4 variables");
  const bool static_mode = a.is("static-cmos");
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", static_mode ? "32" : "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  if (opt.max_cost < 1 || opt.max_cost > 32) throw CliError("--max-cost out of range");
  if (opt.max_chain < 1 || opt.max_chain > 8) throw CliError("--max-chain out of range");
  const unsigned long limit_raw =
      std::stoul(a.get("limit", a.is("all") ? "64" : "8"));
  if (limit_raw == 0 || limit_raw > 1024)
    throw CliError("--limit must be in 1..1024");
  if (static_mode) return cmd_render_static(a, spec, opt, json);

  const auto result = synthesize_min(
      spec.vars, target_of(spec), opt, static_cast<std::size_t>(limit_raw),
      a.is("canonical"));
  if (!result.feasible) {
    if (json) {
      Json j;
      j["ok"] = false;
      j["command"] = "render";
      j["optimum"] = result.optimum;
      j["message"] = result.message;
      std::cout << j.dump() << "\n";
    } else {
      std::cout << "no design: " << result.message << "\n";
    }
    return 1;
  }

  const std::string out = a.get("out", "out/render");
  Json req;
  req["op"] = "render";
  req["out"] = out;
  req["title"] = spec.description;
  req["vars"] = [&] {
    Json v = Json::Array{};
    for (const auto& s : spec.vars) v.push_back(s);
    return v;
  }();
  req["truth"] = spec.func.truth_string();
  req["png"] = a.is("png");
  req["canonical"] = a.is("canonical");
  req["logic"] = !a.is("no-logic");
  Json designs = Json::Array{};
  for (const auto& d : result.designs) designs.push_back(design_json(d, spec.vars));
  req["designs"] = std::move(designs);

  const Json resp = render_call(req);
  Json j;
  j["ok"] = true;
  j["command"] = "render";
  j["out"] = out;
  j["optimum"] = result.optimum;
  j["designs"] = [&] {
    Json v = Json::Array{};
    for (const auto& d : result.designs) v.push_back(design_json(d, spec.vars));
    return v;
  }();
  if (resp.contains("index")) j["index"] = resp.at("index");
  if (resp.contains("files")) j["files"] = resp.at("files");
  if (resp.contains("designs")) j["rendered"] = resp.at("designs");

  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    std::cout << "rendered " << result.designs.size() << " design(s) to "
              << out << "\n";
    if (resp.contains("index"))
      std::cout << "  index: " << resp.at("index").as_string() << "\n";
    if (resp.contains("files"))
      for (const auto& f : resp.at("files").array())
        std::cout << "  " << f.as_string() << "\n";
  }
  return 0;
}

int cmd_pipeline(int argc, char** argv) {
  const Args a = parse_args(argc, argv, 2);
  const bool json = a.is("json");
  SpaceOptions opt;
  opt.max_cost = std::stoi(a.get("max-cost", "8"));
  opt.max_chain = std::stoi(a.get("max-chain", "4"));
  opt.allow_tg = !a.is("no-tg");
  if (opt.max_cost < 1 || opt.max_cost > 32) throw CliError("--max-cost out of range");
  if (opt.max_chain < 1 || opt.max_chain > 8) throw CliError("--max-chain out of range");
  const VerifyMode vmode = parse_verify(a, VerifyMode::Both);
  // "static" is the switch-level proof of a single-stage static gate and only
  // applies to `design min --static-cmos`; elsewhere it would verify nothing.
  if (vmode == VerifyMode::Static)
    throw CliError(
        "--verify static is only available for design min --static-cmos");
  const auto funcs = split_csv(a.need("funcs"));
  if (funcs.empty()) throw CliError("empty --funcs");

  Json runs = Json::Array{};
  bool all_ok = true;
  for (const auto& item : funcs) {
    std::string name, expr;
    const auto eq = item.find('=');
    if (eq == std::string::npos) {
      expr = item;
      name = item;
    } else {
      name = item.substr(0, eq);
      expr = item.substr(eq + 1);
    }
    FuncSpec spec;
    try {
      spec = spec_from_expr(expr, {});
    } catch (const std::exception& e) {
      throw CliError("bad function '" + item + "': " + e.what());
    }
    const auto result = synthesize_min(spec.vars, target_of(spec), opt,
                                       a.is("all") ? 64 : 1, a.is("canonical"));
    Json run;
    run["name"] = name;
    run["expr"] = expr;
    run["stats"] = spec.func.truth_string();
    run["feasible"] = result.feasible;
    run["optimum"] = result.optimum;
    run["designs"] = Json::Array{};
    bool ok = result.feasible;
    for (std::size_t i = 0; i < result.designs.size(); ++i) {
      const auto& d = result.designs[i];
      fs::path emit_dir;
      if (a.has("out"))
        emit_dir = fs::path(a.get("out")) / (name + "_" + std::to_string(i));
      const auto v = verify_design(d, spec, vmode, a.is("keep"),
                                   a.has("out") ? &emit_dir : nullptr);
      if (vmode != VerifyMode::None && !v.yosys && !v.sim) ok = false;
      if ((vmode == VerifyMode::Yosys || vmode == VerifyMode::Both) && !v.yosys) ok = false;
      if ((vmode == VerifyMode::Sim || vmode == VerifyMode::Both) && !v.sim) ok = false;
      Json dj = design_json(d, spec.vars);
      if (vmode != VerifyMode::None) dj["verification"] = v.json;
      if (a.has("out")) dj["files"] = emit_dir.string();
      run["designs"].push_back(std::move(dj));
    }
    run["ok"] = ok;
    runs.push_back(std::move(run));
    all_ok = all_ok && ok;
  }

  Json j;
  j["ok"] = all_ok;
  j["command"] = "pipeline";
  j["runs"] = std::move(runs);
  if (json) {
    std::cout << j.dump() << "\n";
  } else {
    for (const auto& r : j["runs"].array()) {
      std::cout << r.at("name").as_string() << ": " << r.at("expr").as_string() << "  ";
      if (r.at("feasible").dump(0) != "true") {
        std::cout << "no design\n";
        continue;
      }
      std::cout << r.at("optimum").dump(0) << "T  designs="
                << r.at("designs").size() << "\n";
      for (const auto& d : r.at("designs").array()) {
        std::cout << "  " << d.at("cost").dump(0) << "T  "
                  << d.at("topology").as_string() << "\n";
        if (d.contains("verification") &&
            d.at("verification").is_object()) {
          const Json& v = d.at("verification");
          if (v.contains("yosys") && v.at("yosys").is_object())
            std::cout << "    yosys: "
                      << (v.at("yosys").at("ok").as_bool() ? "PASS" : "FAIL")
                      << "\n";
          if (v.contains("verilator") && v.at("verilator").is_object())
            std::cout << "    verilator: "
                      << (v.at("verilator").at("ok").as_bool() ? "PASS" : "FAIL")
                      << "\n";
        }
      }
    }
  }
  return all_ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  bool want_json = false;
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--json") want_json = true;
  try {
    if (argc < 2) {
      help();
      return 2;
    }
    // Allow the global --json before the command: move it to the end so the
    // two-word command dispatch (and parse_args) still works.
    if (argc > 2 && std::string(argv[1]) == "--json") {
      char* json_arg = argv[1];
      for (int i = 1; i + 1 < argc; ++i) argv[i] = argv[i + 1];
      argv[argc - 1] = json_arg;
    }
    configure_mem_limit(argc, argv);
    g_oom_reserve = new (std::nothrow) char[64 * 1024];
    const std::string cmd = argv[1];
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
      help();
      return 0;
    }
    if (argc < 3) throw CliError("missing subcommand or arguments for " + cmd);
    const std::string sub = argv[2];
    if (cmd == "bool") return cmd_bool(sub, argc, argv);
    if (cmd == "cmos") return cmd_cmos(sub, argc, argv);
    if (cmd == "math") return cmd_math(sub, argc, argv);
    if (cmd == "cdl") return cmd_cdl(sub, argc, argv);
    if (cmd == "design") return cmd_design(sub, argc, argv);
    if (cmd == "verify") return cmd_verify(sub, argc, argv);
    if (cmd == "multi") return cmd_multi(sub, argc, argv);
    if (cmd == "render") return cmd_render(argc, argv);
    if (cmd == "pipeline") return cmd_pipeline(argc, argv);
    throw CliError("unknown command: " + cmd);
  } catch (const std::bad_alloc&) {
    delete[] g_oom_reserve;
    g_oom_reserve = nullptr;
    const unsigned long mb = current_mem_limit_mb();
    std::string why = "out of memory";
    if (mb != 0)
      why = "memory limit reached (" + std::to_string(mb) + " MB, " +
            (g_mem_limit_from_flag ? "--mem-limit" : "default cap") +
            "); raise the limit or shrink the search";
    emit_error(want_json, why, 2);
    return 2;
  } catch (const CliError& e) {
    emit_error(want_json, e.what(), e.code);
    return e.code;
  } catch (const std::exception& e) {
    std::string msg = e.what();
    // A capped child (Z3, yosys, node) reports its own "out of memory"; name
    // the cap so the fix is obvious from the error alone.
    if (const unsigned long cap = current_mem_limit_mb();
        cap != 0 && msg.find("out of memory") != std::string::npos)
      msg += " (the process tree is capped at " + std::to_string(cap) +
             " MB; raise --mem-limit)";
    emit_error(want_json, msg, 2);
    return 2;
  }
}
