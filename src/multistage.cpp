#include "dcstudy/multistage.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "dcstudy/cdl.hpp"
#include "dcstudy/json.hpp"
#include "dcstudy/switchlevel.hpp"

namespace dcstudy {
namespace {

namespace fs = std::filesystem;

std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

std::string tail(const std::string& s, std::size_t max_lines = 30) {
  std::vector<std::string> lines;
  std::istringstream is(s);
  std::string line;
  while (std::getline(is, line)) lines.push_back(line);
  const std::size_t start = lines.size() > max_lines ? lines.size() - max_lines : 0;
  std::string out;
  for (std::size_t i = start; i < lines.size(); ++i) out += lines[i] + "\n";
  return out;
}

class TempDir {
 public:
  TempDir() : keep_(false) {
    static std::atomic<int> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
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
  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
  bool keep_;
};

bool static_cell(const std::string& type) {
  static const char* prefixes[] = {
      "INV_", "BUF_", "NAND2_", "NAND3_", "NAND4_", "NOR2_", "NOR3_",
      "NOR4_", "AND2_", "AND3_", "AND4_", "OR2_", "OR3_", "OR4_",
      "AOI21_", "AOI22_", "AOI211_", "AOI222_", "OAI21_", "OAI22_",
      "OAI211_", "OAI222_"};
  for (const char* p : prefixes)
    if (type.rfind(p, 0) == 0) return true;
  return false;
}

std::string bit_net(const Json& bit,
                    const std::map<std::int64_t, std::string>& names) {
  if (bit.is_string()) {
    const std::string s = bit.as_string();
    if (s == "0") return "VSS";
    if (s == "1") return "VDD";
    throw std::runtime_error("multi-stage: unsupported constant bit " + s);
  }
  if (!bit.is_number()) throw std::runtime_error("multi-stage: bad net bit");
  const auto id = bit.as_int();
  auto it = names.find(id);
  if (it == names.end()) throw std::runtime_error("multi-stage: net bit without name");
  return it->second;
}

std::string cdl_from_subckt(const Subckt& c) {
  std::string out = ".SUBCKT " + c.name;
  for (const auto& p : c.ports) out += " " + p;
  out += "\n*.PININFO";
  for (const auto& p : c.ports) {
    char d = 'B';
    switch (c.direction(p)) {
      case PinDir::Input: d = 'I'; break;
      case PinDir::Output: d = 'O'; break;
      case PinDir::Power: d = 'P'; break;
      case PinDir::Ground: d = 'G'; break;
      default: break;
    }
    out += " " + p + ":" + d;
  }
  out += "\n";
  for (const auto& dev : c.devices) {
    const std::string bulk = dev.pmos ? "VDD" : "VSS";
    out += dev.name + " " + dev.drain + " " + dev.gate + " " + dev.source +
           " " + bulk + " " + dev.model + "\n";
  }
  out += ".ENDS\n";
  return out;
}

}  // namespace

std::string default_liberty_path() {
  if (const char* p = std::getenv("NANGATE45_LIB"); p && *p) return expand_home(p);
  if (const char* p = std::getenv("PDK_ROOT"); p && *p)
    return expand_home(std::string(p) + "/nangate45/lib/Nangate45_typ.lib");
  if (const char* h = std::getenv("HOME"); h && *h)
    return std::string(h) + "/yosys-sta/pdk/nangate45/lib/Nangate45_typ.lib";
  return "nangate45/lib/Nangate45_typ.lib";
}

namespace {

MultiStageMultiDesign synthesize_multistage_static_multi_impl(
    const std::vector<BoolFunc>& funcs, const std::vector<std::string>& out_names,
    const std::vector<std::string>& vars, int max_fanin) {
  MultiStageMultiDesign out;
  out.outputs = out_names;
  if (funcs.empty() || funcs.size() != out_names.size()) {
    out.reason = "multi-stage synthesis needs one name per output";
    return out;
  }
  if (vars.empty() || vars.size() > 4) {
    out.reason = "multi-stage synthesis supports 1..4 inputs";
    return out;
  }
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
    for (const auto& v : vars) os << "input logic " << v << ", ";
    for (std::size_t i = 0; i < out_names.size(); ++i) {
      os << "output logic " << out_names[i];
      if (i + 1 < out_names.size()) os << ", ";
    }
    os << ");\n";
    for (std::size_t i = 0; i < funcs.size(); ++i)
      os << "  assign " << out_names[i] << " = " << funcs[i].sop(vars) << ";\n";
    os << "endmodule\n";
  }

  std::string dont_use =
      "-dont_use XOR2_X1 -dont_use XOR2_X2 -dont_use XOR2_X4 "
      "-dont_use XNOR2_X1 -dont_use XNOR2_X2 -dont_use XNOR2_X4 "
      "-dont_use MUX2_X1 -dont_use MUX2_X2 -dont_use MUX2_X4 "
      "-dont_use TBUF_X1 -dont_use TBUF_X2 -dont_use TBUF_X4 "
      "-dont_use HA_X1 -dont_use FA_X1";
  if (max_fanin > 0 && max_fanin <= 2) {
    for (const char* c : {
             "NAND3_X1", "NAND3_X2", "NAND3_X4", "NAND4_X1", "NAND4_X2",
             "NAND4_X4", "NOR3_X1", "NOR3_X2", "NOR3_X4", "NOR4_X1",
             "NOR4_X2", "NOR4_X4", "AND3_X1", "AND3_X2", "AND3_X4",
             "AND4_X1", "AND4_X2", "AND4_X4", "OR3_X1", "OR3_X2", "OR3_X4",
             "OR4_X1", "OR4_X2", "OR4_X4", "AOI21_X1", "AOI21_X2",
             "AOI21_X4", "AOI22_X1", "AOI22_X2", "AOI22_X4", "OAI21_X1",
             "OAI21_X2", "OAI21_X4", "OAI22_X1", "OAI22_X2", "OAI22_X4"})
      dont_use += std::string(" -dont_use ") + c;
  }
  const std::string script =
      "read_verilog -sv rtl.v; synth -top top; abc -liberty " + lib + " " +
      dont_use + "; opt_clean; write_json net.json; write_verilog -noattr net.v";
  const std::string cmd = "cd " + shell_quote(dir.path().string()) +
                          " && timeout 180 yosys -Q -p " +
                          shell_quote(script) + " > " +
                          shell_quote(log.string()) + " 2>&1";
  const int rc = std::system(cmd.c_str());
  const int code = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
  const std::string ylog = read_file(log);
  if (code != 0) {
    out.reason = "yosys failed (exit " + std::to_string(code) + "): " + tail(ylog);
    return out;
  }
  if (!fs::exists(net_json) || !fs::exists(net_v)) {
    out.reason = "yosys produced no netlist: " + tail(ylog);
    return out;
  }

  try {
    const Json root = Json::parse(read_file(net_json));
    const Json& top = root.at("modules").at("top");
    const Json& netnames = top.at("netnames");
    std::map<std::int64_t, std::string> bits;
    for (const auto& [name, info] : netnames.object()) {
      const bool hidden = info.contains("hide_name") && info.at("hide_name").as_int() != 0;
      for (const auto& b : info.at("bits").array()) {
        if (!b.is_number()) continue;
        const auto id = b.as_int();
        auto it = bits.find(id);
        if (it == bits.end() || !hidden) bits[id] = name;
      }
    }

    const Json& cells = top.at("cells");
    for (const auto& [inst, cj] : cells.object()) {
      GateInstance gi;
      gi.type = cj.at("type").as_string();
      if (!static_cell(gi.type)) {
        out.reason = "mapped to non-static cell: " + gi.type +
                     " (choose a static standard-cell library)";
        return out;
      }
      for (const auto& [port, bits_json] : cj.at("connections").object()) {
        const auto& arr = bits_json.array();
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

    const CdlLibrary library = CdlLibrary::load(cdl);
    Subckt flat;
    flat.name = "MULTI_STAGE_STATIC_CMOS";
    for (const auto& v : vars) {
      flat.ports.push_back(v);
      flat.pin_dir[v] = PinDir::Input;
    }
    for (const auto& name : out_names) {
      flat.ports.push_back(name);
      flat.pin_dir[name] = PinDir::Output;
    }
    flat.ports.push_back("VDD");
    flat.pin_dir["VDD"] = PinDir::Power;
    flat.ports.push_back("VSS");
    flat.pin_dir["VSS"] = PinDir::Ground;

    int dev_id = 0;
    for (std::size_t ci = 0; ci < out.cells.size(); ++ci) {
      const auto& gi = out.cells[ci];
      const Subckt& cell = library.cell(gi.type);
      out.total += static_cast<int>(cell.transistor_count());
      auto mapnet = [&](const std::string& n) -> std::string {
        if (n == "VDD" || n == "VSS") return n;
        auto it = gi.ports.find(n);
        if (it != gi.ports.end()) return it->second;
        return "u" + std::to_string(ci) + "_" + n;
      };
      for (const auto& dev : cell.devices) {
        Device d = dev;
        d.name = "M_" + std::to_string(dev_id++);
        d.drain = mapnet(dev.drain);
        d.source = mapnet(dev.source);
        d.gate = mapnet(dev.gate);
        flat.devices.push_back(std::move(d));
      }
    }

    for (const auto& d : flat.devices) {
      if (d.pmos) ++out.pfet;
      else ++out.nfet;
    }
    out.cdl = cdl_from_subckt(flat);
    out.verilog = read_file(net_v);
    out.json = read_file(net_json);

    const SwitchAnalysis a =
        analyze_switch(flat, vars, out_names, true, "VDD", "VSS");
    out.verified = true;
    for (std::size_t k = 0; k < out_names.size(); ++k) {
      std::string truth;
      for (const auto& row : a.rows)
        truth.push_back(logic_symbol(row.values[k]));
      out.truths.push_back(truth);
      if (truth != funcs[k].truth_string()) out.verified = false;
    }
    out.ok = true;
    if (!out.verified) out.reason = "switch-level truth mismatch";
    return out;
  } catch (const std::exception& e) {
    out.reason = std::string("multi-stage parse failed: ") + e.what();
    return out;
  }
}

}  // namespace

MultiStageMultiDesign synthesize_multistage_static_multi(
    const std::vector<BoolFunc>& funcs, const std::vector<std::string>& out_names,
    const std::vector<std::string>& vars, int max_fanin) {
  return synthesize_multistage_static_multi_impl(funcs, out_names, vars, max_fanin);
}

MultiStageDesign synthesize_multistage_static(
    const BoolFunc& f, const std::vector<std::string>& vars, int max_fanin) {
  MultiStageMultiDesign m =
      synthesize_multistage_static_multi_impl({f}, {"Y"}, vars, max_fanin);
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

}  // namespace dcstudy
