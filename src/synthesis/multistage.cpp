#include "synthesis/multistage.hpp"

#include "core/limits.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "core/cdl.hpp"
#include "core/json.hpp"
#include "core/parallel.hpp"
#include "core/verify.hpp"
#include "switch/switchlevel.hpp"
#include "synthesis/static_exact.hpp"

namespace dcstudy {

constexpr std::size_t kLogTailLines = 30;

namespace {
constexpr int kMaxPorts = 128;
} // namespace
namespace {

namespace fs = std::filesystem;

std::string shell_quote(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += '\'';
  return out;
}

void write_text(const fs::path &p, const std::string &s) {
  std::ofstream out(p);
  if (!out)
    throw std::runtime_error("cannot write " + p.string());
  out << s;
}

std::string read_file(const fs::path &p) {
  std::ifstream in(p);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

std::string tail(const std::string &s, std::size_t max_lines = kLogTailLines) {
  std::vector<std::string> lines;
  std::istringstream is(s);
  std::string line;
  while (std::getline(is, line))
    lines.push_back(line);
  const std::size_t start =
      lines.size() > max_lines ? lines.size() - max_lines : 0;
  std::string out;
  for (std::size_t i = start; i < lines.size(); ++i)
    out += lines[i] + "\n";
  return out;
}

class TempDir {
public:
  TempDir() : keep_(false) {
    static std::atomic<int> counter{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = fs::temp_directory_path() /
            ("dcstudy-multi-" + std::to_string(::getpid()) + "-" +
             std::to_string(stamp) + "-" + std::to_string(counter++));
    fs::create_directories(path_);
  }
  ~TempDir() {
    if (!keep_) {
      std::error_code ec;
      fs::remove_all(path_, ec);
    }
  }
  const fs::path &path() const { return path_; }

private:
  fs::path path_;
  bool keep_;
};

bool static_cell(const std::string &type) {
  static const char *prefixes[] = {
      "INV_",    "BUF_",    "NAND2_",  "NAND3_",  "NAND4_",  "NOR2_",  "NOR3_",
      "NOR4_",   "AND2_",   "AND3_",   "AND4_",   "OR2_",    "OR3_",   "OR4_",
      "AOI21_",  "AOI22_",  "AOI211_", "AOI221_", "AOI222_", "OAI21_", "OAI22_",
      "OAI211_", "OAI221_", "OAI222_", "OAI33_"};
  for (const char *p : prefixes)
    if (type.rfind(p, 0) == 0)
      return true;
  return false;
}

// Cells the mapper must never use: not single-stage static CMOS / pass gates.
std::string base_dont_use() {
  return "-dont_use XOR2_X1 -dont_use XOR2_X2 -dont_use XOR2_X4 "
         "-dont_use XNOR2_X1 -dont_use XNOR2_X2 -dont_use XNOR2_X4 "
         "-dont_use MUX2_X1 -dont_use MUX2_X2 -dont_use MUX2_X4 "
         "-dont_use TBUF_X1 -dont_use TBUF_X2 -dont_use TBUF_X4 "
         "-dont_use HA_X1 -dont_use FA_X1";
}

std::string bit_net(const Json &bit,
                    const std::map<std::int64_t, std::string> &names) {
  if (bit.is_string()) {
    const std::string &s = bit.as_string();
    if (s == "0")
      return "VSS";
    if (s == "1")
      return "VDD";
    throw std::runtime_error("multi-stage: unsupported constant bit " + s);
  }
  if (!bit.is_number())
    throw std::runtime_error("multi-stage: bad net bit");
  const auto id = bit.as_int();
  auto it = names.find(id);
  if (it == names.end())
    throw std::runtime_error("multi-stage: net bit without name");
  return it->second;
}

std::string cdl_from_subckt(const Subckt &c) {
  std::string out = ".SUBCKT " + c.name;
  for (const auto &p : c.ports)
    out += " " + p;
  out += "\n*.PININFO";
  for (const auto &p : c.ports) {
    char d = 'B';
    switch (c.direction(p)) {
    case PinDir::Input:
      d = 'I';
      break;
    case PinDir::Output:
      d = 'O';
      break;
    case PinDir::Power:
      d = 'P';
      break;
    case PinDir::Ground:
      d = 'G';
      break;
    default:
      break;
    }
    out += " " + p + ":" + d;
  }
  out += '\n';
  for (const auto &[alias, canon] : c.aliases) {
    out += "*.ALIAS ";
    out += alias;
    out += ' ';
    out += canon;
    out += '\n';
  }
  for (const auto &dev : c.devices) {
    const std::string bulk = dev.pmos ? "VDD" : "VSS";
    out += dev.name + " " + dev.drain + " " + dev.gate + " " + dev.source +
           " " + bulk + " " + dev.model + "\n";
  }
  // Two ports on one net cannot be written in a transistor netlist, so tie
  // them with a 0 V source: SPICE simulators then see the alias connected
  // (the *.ALIAS line above is what this repo's own parser uses).
  for (const auto &[alias, canon] : c.aliases) {
    out += "V__alias_";
    out += alias;
    out += ' ';
    out += alias;
    out += ' ';
    out += canon;
    out += " 0\n";
  }
  out += ".ENDS\n";
  return out;
}

} // namespace

std::string default_liberty_path() {
  if (const char *p = std::getenv("NANGATE45_LIB"); p && *p)
    return expand_home(p);
  if (const char *p = std::getenv("PDK_ROOT"); p && *p)
    return expand_home(std::string(p) + "/nangate45/lib/Nangate45_typ.lib");
  if (const char *h = std::getenv("HOME"); h && *h)
    return std::string(h) + "/yosys-sta/pdk/nangate45/lib/Nangate45_typ.lib";
  return "nangate45/lib/Nangate45_typ.lib";
}

namespace {

MultiStageMultiDesign
synthesize_multistage_static_multi_impl(const MultiProblem &p, int max_fanin) {
  MultiStageMultiDesign out;
  out.outputs = p.names;
  const auto &out_names = p.names;
  const auto &vars = p.vars;
  std::vector<BoolFunc> funcs;
  funcs.reserve(p.size());
  for (const auto &o : p.outputs)
    funcs.push_back(o.func);
  if (funcs.empty() || funcs.size() != out_names.size()) {
    out.reason = "multi-stage synthesis needs one name per output";
    return out;
  }
  if (vars.empty()) {
    out.reason = "multi-stage synthesis needs at least one input";
    return out;
  }
  // The real limit is the truth-table footprint, not the input count:
  // 2^n rows x outputs, with room for completion/working copies.
  if (vars.size() >= kMaxAddressableVars) {
    out.reason = "truth table needs 2^n rows; n >= kMaxAddressableVars is not "
                 "addressable";
    return out;
  }
  const std::size_t rows = std::size_t{1} << vars.size();
  const unsigned long cap_mb = current_mem_limit_mb();
  const unsigned long avail_mb = available_memory_mb();
  const unsigned long budget_mb = cap_mb ? cap_mb : avail_mb;
  if (budget_mb != 0) {
    const std::size_t need = rows * (funcs.size() + 8);
    const std::size_t budget = budget_mb * 1024ull * 1024ull / 2;
    if (need > budget) {
      out.reason = "truth table needs " +
                   std::to_string(need / (kBytesPerMiB * kBytesPerMiB)) +
                   " MiB (2^" + std::to_string(vars.size()) + " rows x " +
                   std::to_string(funcs.size()) + " outputs), budget is " +
                   std::to_string(budget_mb / 2) +
                   " MiB; raise --mem-limit or reduce inputs";
      return out;
    }
  }
  // Complete partial functions first: the mapper needs a total function, and
  // the cheapest static-gate extension keeps the mapped logic small.
  std::vector<BoolFunc> work = funcs;
  for (std::size_t k = 0; k < work.size(); ++k)
    if (!p.any(k).none())
      work[k] = p.completed(k);
  const std::string lib = default_liberty_path();
  const std::string cdl = default_cdl_path();
  if (!fs::exists(lib)) {
    out.reason = "Nangate45 liberty not found: " + lib;
    return out;
  }
  if (!fs::exists(cdl)) {
    out.reason = "Nangate45 CDL not found: " + cdl;
    return out;
  }

  TempDir dir;
  const fs::path rtl = dir.path() / "rtl.v";
  const fs::path net_json = dir.path() / "net.json";
  const fs::path net_v = dir.path() / "net.v";
  const fs::path log = dir.path() / "yosys.log";
  {
    std::ofstream os(rtl);
    os << "module top(";
    for (const auto &v : vars)
      os << "input logic " << v << ", ";
    for (std::size_t i = 0; i < out_names.size(); ++i) {
      os << "output logic " << out_names[i];
      if (i + 1 < out_names.size())
        os << ", ";
    }
    os << ");\n";
    for (std::size_t i = 0; i < work.size(); ++i)
      os << "  assign " << out_names[i] << " = " << work[i].sop(vars) << ";\n";
    os << "endmodule\n";
  }

  // The CDL is the single source of truth for cell fan-in: pin directions
  // come from *.PININFO.  A hand-written dont_use table drifted from the
  // library and let AOI211/OAI211 (4 inputs, 8T) through --max-fanin 2.
  const CdlLibrary library = CdlLibrary::load(cdl);

  std::string dont_use = base_dont_use();
  if (max_fanin > 0) {
    // Every library cell over the limit is blocked, not only the static-cell
    // families: ABC will otherwise pick e.g. OAI221_X1 and the fan-in
    // contract is broken no matter what the post-check below says.
    for (const auto &cell : library.cells()) {
      const int inputs = static_cast<int>(cell.inputs().size());
      if (inputs > max_fanin)
        dont_use += " -dont_use " + cell.name;
    }
  }
  const std::string script =
      "read_verilog -sv rtl.v; synth -top top; abc -liberty " + lib + " " +
      dont_use +
      "; opt_clean; write_json net.json; write_verilog -noattr net.v";
  const std::string cmd =
      "cd " + shell_quote(dir.path().string()) + " && timeout " +
      std::to_string(kYosysTimeoutSeconds) + " yosys -Q -p " +
      shell_quote(script) + " > " + shell_quote(log.string()) + " 2>&1";
  const int rc = std::system(cmd.c_str()); // NOLINT(bugprone-command-processor)
  const int code = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
  const std::string ylog = read_file(log);
  if (code != 0) {
    out.reason =
        "yosys failed (exit " + std::to_string(code) + "): " + tail(ylog);
    return out;
  }
  if (!fs::exists(net_json) || !fs::exists(net_v)) {
    out.reason = "yosys produced no netlist: " + tail(ylog);
    return out;
  }

  try {
    const Json root = Json::parse(read_file(net_json));
    const Json &top = root.at("modules").at("top");
    const Json &netnames = top.at("netnames");
    std::map<std::int64_t, std::string> bits;
    for (const auto &[name, info] : netnames.object()) {
      const bool hidden =
          info.contains("hide_name") && info.at("hide_name").as_int() != 0;
      for (const auto &b : info.at("bits").array()) {
        if (!b.is_number())
          continue;
        const auto id = b.as_int();
        auto it = bits.find(id);
        if (it == bits.end() || !hidden)
          bits[id] = name;
      }
    }

    // Yosys may fold several output ports onto one bit (e.g. Y0 and Y1 both
    // ~(A&B)).  Keep the port->bit map: the first output names the shared
    // net, later ones become aliases instead of dangling unconnected.
    const Json &ports = top.at("ports");
    std::map<std::int64_t, std::string> input_of_bit;
    for (const auto &v : vars) {
      if (!ports.contains(v))
        continue;
      const auto &pb = ports.at(v).at("bits").array();
      if (!pb.empty() && pb[0].is_number())
        input_of_bit[pb[0].as_int()] = v;
    }
    std::map<std::int64_t, std::string> output_of_bit;
    std::vector<int> alias_of(out_names.size(), -1);
    for (std::size_t k = 0; k < out_names.size(); ++k) {
      if (!ports.contains(out_names[k]))
        continue;
      const auto &pb = ports.at(out_names[k]).at("bits").array();
      if (pb.empty() || !pb[0].is_number())
        continue;
      const auto id = pb[0].as_int();
      auto it = output_of_bit.find(id);
      if (it == output_of_bit.end()) {
        output_of_bit.emplace(id, out_names[k]);
        if (!input_of_bit.count(id))
          bits[id] = out_names[k];
      } else {
        const auto canon =
            std::find(out_names.begin(), out_names.end(), it->second) -
            out_names.begin();
        alias_of[k] = static_cast<int>(canon);
      }
    }

    const Json &cells = top.at("cells");
    for (const auto &[inst, cj] : cells.object()) {
      GateInstance gi;
      gi.type = cj.at("type").as_string();
      if (!static_cell(gi.type)) {
        out.reason = "mapped to non-static cell: " + gi.type +
                     " (choose a static standard-cell library)";
        return out;
      }
      for (const auto &[port, bits_json] : cj.at("connections").object()) {
        const auto &arr = bits_json.array();
        if (arr.size() != 1)
          throw std::runtime_error("multi-stage: multi-bit cell port");
        gi.ports[port] = bit_net(arr[0], bits);
      }
      out.cells.push_back(std::move(gi));
    }

    if (out.cells.empty()) {
      out.reason = "yosys produced an empty netlist";
      return out;
    }

    Subckt flat;
    flat.name = "MULTI_STAGE_STATIC_CMOS";
    for (const auto &v : vars) {
      flat.ports.push_back(v);
      flat.pin_dir[v] = PinDir::Input;
    }
    for (std::size_t k = 0; k < out_names.size(); ++k) {
      flat.ports.push_back(out_names[k]);
      flat.pin_dir[out_names[k]] = PinDir::Output;
      if (alias_of[k] >= 0)
        flat.aliases[out_names[k]] =
            out_names[static_cast<std::size_t>(alias_of[k])];
    }
    flat.ports.push_back("VDD");
    flat.pin_dir["VDD"] = PinDir::Power;
    flat.ports.push_back("VSS");
    flat.pin_dir["VSS"] = PinDir::Ground;

    int dev_id = 0;
    for (std::size_t ci = 0; ci < out.cells.size(); ++ci) {
      const auto &gi = out.cells[ci];
      const Subckt &cell = library.cell(gi.type);
      const int cell_inputs = static_cast<int>(cell.inputs().size());
      if (max_fanin > 0 && cell_inputs > max_fanin) {
        out.reason = "mapped cell " + gi.type + " has " +
                     std::to_string(cell_inputs) + " inputs, over " +
                     "--max-fanin " + std::to_string(max_fanin);
        return out;
      }
      out.total += static_cast<int>(cell.transistor_count());
      auto mapnet = [&](const std::string &n) -> std::string {
        if (n == "VDD" || n == "VSS")
          return n;
        auto it = gi.ports.find(n);
        if (it != gi.ports.end())
          return it->second;
        return "u" + std::to_string(ci) + "_" + n;
      };
      for (const auto &dev : cell.devices) {
        Device d = dev;
        d.name = "M_" + std::to_string(dev_id++);
        d.drain = mapnet(dev.drain);
        d.source = mapnet(dev.source);
        d.gate = mapnet(dev.gate);
        flat.devices.push_back(std::move(d));
      }
    }

    for (const auto &d : flat.devices) {
      if (d.pmos)
        ++out.pfet;
      else
        ++out.nfet;
    }
    out.cdl = cdl_from_subckt(flat);
    out.verilog = read_file(net_v);
    out.rtl = read_file(rtl);
    out.json = read_file(net_json);

    // Shared gates make the full seven-output switch graph a hard Z3
    // problem.  The mapped cells are static CMOS, so each output's cone is
    // independent; verify the seven cones in parallel instead.
    out.truths.resize(out_names.size());
    std::vector<char> output_ok(out_names.size(), 0);
    parallel_for(out_names.size(), parallel_jobs(), [&](std::size_t k) {
      if (alias_of[k] >= 0)
        return; // copied from the canonical output
      const Subckt one =
          switch_output_cone(flat, vars, out_names[k], "VDD", "VSS");
      const SwitchAnalysis a = analyze_switch(one, vars, {out_names[k]}, true,
                                              "VDD", "VSS", false, 1);
      std::string truth;
      for (const auto &row : a.rows)
        truth.push_back(logic_symbol(row.values[0]));
      out.truths[k] = std::move(truth);
      output_ok[k] = p.satisfies(k, out.truths[k]) ? 1 : 0;
    });
    for (std::size_t k = 0; k < out_names.size(); ++k) {
      if (alias_of[k] < 0)
        continue;
      out.truths[k] = out.truths[static_cast<std::size_t>(alias_of[k])];
      output_ok[k] = p.satisfies(k, out.truths[k]) ? 1 : 0;
    }
    out.verified = std::all_of(output_ok.begin(), output_ok.end(),
                               [](char v) { return v != 0; });
    out.ok = true;
    if (!out.verified)
      out.reason = "switch-level truth mismatch";
    return out;
  } catch (const std::exception &e) {
    out.reason = std::string("multi-stage parse failed: ") + e.what();
    return out;
  }
}

} // namespace

MultiStageMultiDesign
synthesize_multistage_static_multi(const MultiProblem &problem, int max_fanin) {
  return synthesize_multistage_static_multi_impl(problem, max_fanin);
}

MultiStageDesign synthesize_multistage_static(
    const BoolFunc &f, const std::vector<std::string> &vars, int max_fanin) {
  MultiProblem p;
  p.names = {"Y"};
  p.vars = vars;
  p.outputs.push_back({f, Bits::zeros(f.bits().size())});
  MultiStageMultiDesign m =
      synthesize_multistage_static_multi_impl(p, max_fanin);
  MultiStageDesign out;
  out.ok = m.ok;
  out.reason = m.reason;
  out.total = m.total;
  out.nfet = m.nfet;
  out.pfet = m.pfet;
  out.cells = std::move(m.cells);
  out.verilog = std::move(m.verilog);
  out.json = std::move(m.json);
  out.cdl = std::move(m.cdl);
  out.truth = m.truths.empty() ? "" : m.truths.front();
  out.verified = m.verified;
  return out;
}

namespace {

struct PortSpec {
  std::string dir;
  int width = 0;
};

std::map<std::string, PortSpec> module_ports(const Json &module) {
  std::map<std::string, PortSpec> out;
  for (const auto &[name, pj] : module.at("ports").object()) {
    PortSpec p;
    p.dir = pj.at("direction").as_string();
    p.width = static_cast<int>(pj.at("bits").size());
    out[name] = p;
  }
  return out;
}

// Reject name/direction/width mismatches before any miter is built: a silent
// pad or truncation would prove only a subset of the RTL inputs.
// Internal helper; call sites reviewed.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void require_same_ports(const std::map<std::string, PortSpec> &got,
                        const std::map<std::string, PortSpec> &want,
                        const std::string &what) {
  for (const auto &[name, w] : want) {
    const auto it = got.find(name);
    if (it == got.end()) {
      std::string msg = what;
      msg += ": missing port ";
      msg += name;
      throw std::runtime_error(msg);
    }
    if (it->second.dir != w.dir || it->second.width != w.width) {
      std::string msg = what;
      msg += ": port ";
      msg += name;
      msg += " is ";
      msg += it->second.dir;
      msg += '[';
      msg += std::to_string(it->second.width);
      msg += "], expected ";
      msg += w.dir;
      msg += '[';
      msg += std::to_string(w.width);
      msg += ']';
      throw std::runtime_error(msg);
    }
  }
  for (const auto &[name, g] : got)
    if (!want.count(name)) {
      std::string msg = what;
      msg += ": unexpected port ";
      msg += name;
      throw std::runtime_error(msg);
    }
}

std::string render_params(const Json &cell) {
  if (!cell.contains("parameters"))
    return "";
  std::string out;
  for (const auto &[k, v] : cell.at("parameters").object()) {
    if (!out.empty())
      out += ',';
    out += k + "=" + (v.is_string() ? v.as_string() : v.dump());
  }
  return out;
}

// Internal helper; call sites reviewed.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::string chparam_cmd(const std::string &module, const std::string &params) {
  if (params.empty())
    return "";
  std::string out;
  std::istringstream ss(params);
  std::string item;
  while (std::getline(ss, item, ',')) {
    const auto eq = item.find('=');
    if (eq == std::string::npos)
      throw std::runtime_error("bad atom parameter: " + item);
    out += "; chparam -set " + item.substr(0, eq) + " " + item.substr(eq + 1) +
           " " + module;
  }
  return out;
}

} // namespace

RtlSynthResult synthesize_rtl_atoms(const std::string &rtl_path,
                                    const std::string &atoms_path,
                                    const MultiProblem &spec,
                                    const std::string &ref_path, int max_fanin,
                                    bool proven_by_spec) {
  RtlSynthResult out;
  out.outputs = spec.names;
  const std::string lib = default_liberty_path();
  const std::string cdl = default_cdl_path();
  if (!fs::exists(rtl_path)) {
    out.reason = "cannot read RTL: " + rtl_path;
    return out;
  }
  if (!fs::exists(atoms_path)) {
    out.reason = "cannot read atoms: " + atoms_path;
    return out;
  }
  if (!fs::exists(lib)) {
    out.reason = "Nangate45 liberty not found: " + lib;
    return out;
  }
  if (!fs::exists(cdl)) {
    out.reason = "Nangate45 CDL not found: " + cdl;
    return out;
  }

  TempDir dir;
  write_text(dir.path() / "top.v", read_file(rtl_path));
  write_text(dir.path() / "atoms.v", read_file(atoms_path));
  const CdlLibrary library = CdlLibrary::load(cdl);
  std::string dont_use = base_dont_use();
  if (max_fanin > 0) {
    for (const auto &cell : library.cells()) {
      if (static_cast<int>(cell.inputs().size()) > max_fanin)
        dont_use += " -dont_use " + cell.name;
    }
  }
  auto run_yosys = [&](const std::string &script,
                       const std::string &log_name) -> std::string {
    const std::string cmd =
        "cd " + shell_quote(dir.path().string()) + " && timeout " +
        std::to_string(kYosysTimeoutSeconds) + " yosys -Q -p " +
        shell_quote(script) + " > " +
        shell_quote((dir.path() / log_name).string()) + " 2>&1";
    const int rc =
        std::system(cmd.c_str()); // NOLINT(bugprone-command-processor)
    const int code = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
    const std::string log = read_file(dir.path() / log_name);
    if (code != 0)
      throw std::runtime_error("yosys failed: " + tail(log));
    return log;
  };

  try {
    run_yosys("read_verilog -sv -lib atoms.v; read_verilog -sv top.v; "
              "synth -top top; abc -liberty " +
                  lib + " " + dont_use +
                  "; opt_clean; write_json net.json; "
                  "write_verilog -noattr net.v",
              "map.log");
    const Json root = Json::parse(read_file(dir.path() / "net.json"));
    if (!root.contains("modules") || !root.at("modules").contains("top"))
      throw std::runtime_error("mapped netlist has no top module");
    const Json &cells = root.at("modules").at("top").at("cells");
    // Parameters are part of an atom's identity: two instances of the same
    // module with different overrides cost different amounts.
    std::map<std::string, RtlAtomUse> atom_uses;
    int cell_cost = 0;
    for (const auto &[inst, cj] : cells.object()) {
      const std::string type = cj.at("type").as_string();
      if (library.has(type)) {
        if (!static_cell(type))
          throw std::runtime_error("mapped to non-static cell: " + type);
        cell_cost += static_cast<int>(library.cell(type).transistor_count());
      } else {
        const std::string params = render_params(cj);
        std::string key = type;
        key += '|';
        key += params;
        RtlAtomUse &use = atom_uses[key];
        use.module = type;
        use.parameters = params;
        ++use.instances;
      }
    }

    // Module names in the library: measuring one atom blackboxes the others,
    // so its cost is computed the same way its parent was charged.
    std::vector<std::string> lib_modules;
    {
      const std::string src = read_file(atoms_path);
      const std::regex re("\\bmodule\\s+([A-Za-z_]\\w*)");
      for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end;
           ++it)
        lib_modules.push_back((*it)[1].str());
    }
    std::map<std::string, int> atom_cost;
    std::set<std::string> measuring;
    int atom_index = 0;
    std::function<int(const std::string &, const std::string &)> measure =
        [&](const std::string &module, const std::string &params) -> int {
      const std::string key = module + "|" + params;
      if (const auto it = atom_cost.find(key); it != atom_cost.end())
        return it->second;
      if (!measuring.insert(key).second)
        throw std::runtime_error("recursive atom " + module);
      std::string blackbox;
      for (const auto &m : lib_modules)
        if (m != module)
          blackbox += m + " ";
      const std::string bb =
          blackbox.empty() ? std::string() : "; blackbox " + blackbox;
      const std::string json_name =
          "atom_" + std::to_string(atom_index++) + ".json";
      run_yosys("read_verilog -sv atoms.v" + bb + chparam_cmd(module, params) +
                    "; synth -top " + module + "; abc -liberty " + lib + " " +
                    dont_use + "; opt_clean; write_json " + json_name,
                "atom.log");
      const Json aroot = Json::parse(read_file(dir.path() / json_name));
      if (!aroot.contains("modules") || !aroot.at("modules").contains(module))
        throw std::runtime_error("atom " + module + " did not map");
      const Json &acells = aroot.at("modules").at(module).at("cells");
      int cost = 0;
      for (const auto &[inst, cj] : acells.object()) {
        const std::string type = cj.at("type").as_string();
        if (library.has(type)) {
          if (!static_cell(type)) {
            std::string msg = "atom ";
            msg += module;
            msg += " mapped to non-static cell: ";
            msg += type;
            throw std::runtime_error(msg);
          }
          cost += static_cast<int>(library.cell(type).transistor_count());
        } else {
          cost += measure(type, render_params(cj)); // nested atom
        }
      }
      measuring.erase(key);
      atom_cost[key] = cost;
      return cost;
    };

    for (const auto &[key, use] : atom_uses) {
      const int cost = measure(use.module, use.parameters);
      out.atoms.push_back({use.module, use.instances, cost, use.parameters});
    }

    for (const auto &use : out.atoms)
      out.atom_cost += use.instances * use.transistors;
    out.cell_cost = cell_cost;
    out.total = out.atom_cost + out.cell_cost;
    out.verilog = read_file(dir.path() / "net.v");
    out.json = read_file(dir.path() / "net.json");

    if (!ref_path.empty()) {
      // Structural miter: no truth table, so the input count is not bounded
      // by kMaxArity; only by the (128) port limit below.
      if (!fs::exists(ref_path))
        throw std::runtime_error("cannot read ref: " + ref_path);
      const std::string ref_src = read_file(ref_path);
      std::smatch m;
      const std::regex rm("\\bmodule\\s+([A-Za-z_]\\w*)");
      if (!std::regex_search(ref_src, m, rm))
        throw std::runtime_error("ref has no module: " + ref_path);
      const std::string ref_mod = m[1].str();
      write_text(dir.path() / "ref.v", ref_src);
      run_yosys("read_verilog -sv -lib atoms.v; read_verilog -sv ref.v; "
                "proc; write_json ref.json",
                "ref_ports.log");
      const Json rroot = Json::parse(read_file(dir.path() / "ref.json"));
      if (!rroot.contains("modules") || !rroot.at("modules").contains(ref_mod))
        throw std::runtime_error("ref module not found: " + ref_mod);
      require_same_ports(module_ports(rroot.at("modules").at(ref_mod)),
                         module_ports(root.at("modules").at("top")), "ref");
      const Json &ports = root.at("modules").at("top").at("ports");
      std::string header, wires, conns, cmps;
      int n_in = 0, n_out = 0;
      for (const auto &[name, pj] : ports.object()) {
        const std::string dir = pj.at("direction").as_string();
        const auto width = pj.at("bits").size();
        const std::string range =
            width > 1 ? " [" + std::to_string(width - 1) + ":0] " : " ";
        if (dir == "input") {
          header += "input logic";
          header += range;
          header += name;
          header += ", ";
          conns += '.';
          conns += name;
          conns += '(';
          conns += name;
          conns += "), ";
          ++n_in;
        } else {
          wires += "  wire";
          wires += range;
          wires += "t_";
          wires += name;
          wires += ", r_";
          wires += name;
          wires += ";\n";
          conns += '.';
          conns += name;
          conns += "(t_";
          conns += name;
          conns += "), ";
          cmps += " | (t_";
          cmps += name;
          cmps += " !== r_";
          cmps += name;
          cmps += ')';
          if (spec.names.empty())
            out.outputs.push_back(name);
          ++n_out;
        }
      }
      if (n_in == 0 || n_out == 0)
        throw std::runtime_error("mapped netlist has no usable ports");
      if (n_in > kMaxPorts)
        throw std::runtime_error("too many inputs: " + std::to_string(n_in) +
                                 " (max " + std::to_string(kMaxPorts) + ")");
      // Reference instance uses r_* outputs: rebuild the connection list for
      // it by replacing the output wires.
      std::string ref_conns;
      for (const auto &[name, pj] : ports.object()) {
        if (pj.at("direction").as_string() == "input") {
          ref_conns += '.';
          ref_conns += name;
          ref_conns += '(';
          ref_conns += name;
          ref_conns += "), ";
        } else {
          ref_conns += '.';
          ref_conns += name;
          ref_conns += "(r_";
          ref_conns += name;
          ref_conns += "), ";
        }
      }
      std::string miter =
          "module miter(" + header + "output logic mismatch);\n" + wires +
          "  top u_top(" + conns + ");\n  " + ref_mod + " u_ref(" + ref_conns +
          ");\n  assign mismatch = 1'b0" + cmps + ";\nendmodule\n";
      write_text(dir.path() / "miter.v", miter);
      run_yosys("read_verilog -sv atoms.v; read_verilog -sv top.v; "
                "read_verilog -sv ref.v; read_verilog -sv miter.v; "
                "prep -top miter; flatten; memory; delete t:$scopeinfo; opt "
                "-full; sat "
                "-verify -prove mismatch 0 miter",
                "ref.log");
      out.verified = true;
      out.ok = true;
      return out;
    }
    {
      std::map<std::string, PortSpec> want;
      for (const auto &v : spec.vars)
        want[v] = {"input", 1};
      for (const auto &n : spec.names)
        want[n] = {"output", 1};
      require_same_ports(module_ports(root.at("modules").at("top")), want,
                         "spec");
    }
    if (spec.has_dont_cares())
      throw std::runtime_error("--rtl does not support don't-care masks yet");
    if (proven_by_spec) {
      // The caller built this netlist from the spec truth table (every atom
      // output and the glue table were checked against tgt_rows), so the
      // structural miter would re-prove the same table with a SAT search.
      out.verified = true;
      out.ok = true;
      return out;
    }
    // Prove the whole composition in one miter: a single Yosys + SAT run
    // for all outputs (was one process per output).
    std::string ref = "module ref(";
    for (std::size_t i = 0; i < spec.vars.size(); ++i)
      ref += (i ? ", " : "") + std::string("input logic ") + spec.vars[i];
    for (std::size_t i = 0; i < spec.names.size(); ++i)
      ref += ", output logic " + spec.names[i];
    ref += ");\n";
    for (std::size_t k = 0; k < spec.outputs.size(); ++k)
      ref += "  assign " + spec.names[k] + " = " +
             spec.outputs[k].func.sop(spec.vars) + ";\n";
    ref += "endmodule\n";

    std::string miter = "module miter(";
    for (std::size_t i = 0; i < spec.vars.size(); ++i)
      miter += (i ? ", " : "") + std::string("input logic ") + spec.vars[i];
    miter += ", output logic mismatch);\n";
    for (const auto &n : spec.names) {
      miter += "  wire t_";
      miter += n;
      miter += ", r_";
      miter += n;
      miter += ";\n";
    }
    miter += "  top u_top(";
    for (std::size_t i = 0; i < spec.vars.size(); ++i)
      miter += (i ? ", " : "") + std::string(".") + spec.vars[i] + "(" +
               spec.vars[i] + ")";
    for (const auto &n : spec.names) {
      miter += ", .";
      miter += n;
      miter += "(t_";
      miter += n;
      miter += ')';
    }
    miter += ");\n  ref u_ref(";
    for (std::size_t i = 0; i < spec.vars.size(); ++i)
      miter += (i ? ", " : "") + std::string(".") + spec.vars[i] + "(" +
               spec.vars[i] + ")";
    for (const auto &n : spec.names) {
      miter += ", .";
      miter += n;
      miter += "(r_";
      miter += n;
      miter += ')';
    }
    miter += ");\n  assign mismatch = 1'b0";
    for (const auto &n : spec.names) {
      miter += " | (t_";
      miter += n;
      miter += " !== r_";
      miter += n;
      miter += ')';
    }
    miter += ";\nendmodule\n";
    write_text(dir.path() / "ref.v", ref);
    write_text(dir.path() / "miter.v", miter);
    run_yosys("read_verilog -sv top.v; read_verilog -sv atoms.v; "
              "read_verilog -sv ref.v; read_verilog -sv miter.v; "
              "prep -top miter; flatten; delete t:$scopeinfo; opt -full; "
              "sat -verify -prove mismatch 0 miter",
              "spec.log");
    out.verified = true;
    out.ok = true;
    return out;
  } catch (const std::exception &e) {
    out.reason = std::string("rtl mapping failed: ") + e.what();
    return out;
  }
}

} // namespace dcstudy
