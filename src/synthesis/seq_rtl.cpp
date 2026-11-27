#include "synthesis/seq_rtl.hpp"

#include <sys/wait.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/cdl.hpp"
#include "core/json.hpp"
#include "synthesis/multistage.hpp"

namespace dcstudy {

namespace {

namespace fs = std::filesystem;
using u32 = std::uint32_t;

std::string shell_quote(const std::string &s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}

class TempDir {
public:
  TempDir() {
    static std::atomic<int> counter{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ =
        fs::temp_directory_path() / ("dcstudy-seq-" + std::to_string(stamp) +
                                     "-" + std::to_string(counter++) + ".d");
    fs::create_directories(path_);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;
  const fs::path &path() const { return path_; }

private:
  fs::path path_;
};

std::string read_file(const fs::path &path) {
  std::ifstream in(path);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

void write_file(const fs::path &path, const std::string &text) {
  std::ofstream out(path);
  out << text;
}

std::string tail(const std::string &text, std::size_t max_lines = 25) {
  std::vector<std::string> lines;
  std::istringstream is(text);
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

int run_logged(const std::string &command, const fs::path &log,
               int timeout_seconds) {
  const std::string full = "timeout " + std::to_string(timeout_seconds) +
                           " bash -c " + shell_quote(command) + " > " +
                           shell_quote(log.string()) + " 2>&1";
  const int rc =
      std::system(full.c_str()); // NOLINT(bugprone-command-processor)
  if (rc == -1)
    return -1;
  if (WIFEXITED(rc))
    return WEXITSTATUS(rc);
  return -1;
}

// External steps: Yosys proofs are quick, a Verilator build is not.
constexpr int kSeqYosysTimeoutSeconds = 240;
constexpr int kSeqVerilatorTimeoutSeconds = 600;
// The golden model enumerates the full next-state table; above this many
// state+input bits the source would be bigger than the design deserves.
constexpr std::size_t kGoldTableBits = 12;

std::string reset_port(const FsmSpec &fsm) {
  return fsm.reset_active == "low" ? "rst_n" : "rst";
}

bool active_low(const FsmSpec &fsm) { return fsm.reset_active == "low"; }

std::string reset_edge(const FsmSpec &fsm) {
  if (fsm.reset_mode != "async")
    return {};
  return active_low(fsm) ? " or negedge rst_n" : " or posedge rst";
}

std::string sop(const FsmSpec &fsm, const BoolFunc &func) {
  std::vector<std::string> vars = fsm.state_vars;
  vars.insert(vars.end(), fsm.inputs.begin(), fsm.inputs.end());
  return func.sop(vars);
}

std::string bit_string(std::size_t width, std::uint32_t value) {
  std::string bits(width, '0');
  for (std::size_t i = 0; i < width; ++i)
    if (((value >> (width - 1 - i)) & 1u) != 0)
      bits[i] = '1';
  return bits;
}

std::string port_block(const FsmSpec &fsm) {
  std::ostringstream os;
  os << "  input logic clk,\n  input logic " << reset_port(fsm);
  for (const auto &in : fsm.inputs)
    os << ",\n  input logic " << in;
  for (const auto &out : fsm.outputs)
    os << ",\n  output logic " << out.name;
  return os.str();
}

std::string instance_ports(const FsmSpec &fsm, const std::string &suffix) {
  std::ostringstream os;
  os << ".clk(clk), ." << reset_port(fsm) << "(" << reset_port(fsm) << ")";
  for (const auto &in : fsm.inputs)
    os << ", ." << in << "(" << in << ")";
  for (const auto &out : fsm.outputs)
    os << ", ." << out.name << "(" << out.name << suffix << ")";
  return os.str();
}

u32 next_code(const FsmSpec &fsm, u32 state, u32 input) {
  const std::size_t m = fsm.inputs.size();
  const u32 minterm = (state << m) | input;
  u32 code = 0;
  for (std::size_t i = 0; i < fsm.state_bits; ++i)
    if (fsm.next[i].value(minterm))
      code |= u32{1} << (fsm.state_bits - 1 - i);
  return code;
}

} // namespace

namespace {

void require_complete(const FsmSpec &fsm) {
  if (fsm.next.size() != fsm.state_bits)
    throw std::runtime_error("FSM has " + std::to_string(fsm.next.size()) +
                             " next-state functions for " +
                             std::to_string(fsm.state_bits) + " state bits");
}

} // namespace

std::string emit_fsm_rtl(const FsmSpec &fsm) {
  require_complete(fsm);
  const std::string rst = reset_port(fsm);
  const bool low = active_low(fsm);
  const u32 reset = fsm.reset_code();
  std::ostringstream os;
  os << "// Generated by dcstudy: sequential FSM top (REQ-SEQ-020/022).\n";
  os << "module top(\n" << port_block(fsm) << "\n);\n";
  for (const auto &var : fsm.state_vars)
    os << "  logic " << var << ";\n";
  for (std::size_t i = 0; i < fsm.next.size(); ++i)
    os << "  wire nxt_" << fsm.state_vars[i] << " = " << sop(fsm, fsm.next[i])
       << ";\n";
  os << "  always_ff @(posedge clk" << reset_edge(fsm) << ") begin\n";
  os << "    if (" << (low ? "!" : "") << rst << ") begin\n";
  for (std::size_t i = 0; i < fsm.state_bits; ++i) {
    const u32 bit = (reset >> (fsm.state_bits - 1 - i)) & 1u;
    os << "      " << fsm.state_vars[i] << " <= 1'b" << bit << ";\n";
  }
  os << "    end else begin\n";
  for (const auto &var : fsm.state_vars)
    os << "      " << var << " <= nxt_" << var << ";\n";
  os << "    end\n  end\n";
  for (const auto &out : fsm.outputs)
    os << "  assign " << out.name << " = " << sop(fsm, out.func) << ";\n";
  os << "endmodule\n";
  return os.str();
}

std::string emit_fsm_gold(const FsmSpec &fsm) {
  require_complete(fsm);
  if (fsm.state_bits + fsm.inputs.size() > kGoldTableBits)
    throw std::runtime_error(
        "golden state table needs at most " + std::to_string(kGoldTableBits) +
        " state+input bits (S1 cap; larger FSMs need the S2 flow)");
  const std::string rst = reset_port(fsm);
  const bool low = active_low(fsm);
  const u32 reset = fsm.reset_code();
  const std::size_t k = fsm.state_bits;
  const std::size_t m = fsm.inputs.size();
  std::ostringstream os;
  os << "// Generated by dcstudy: golden FSM (state table).\n";
  os << "module gold(\n" << port_block(fsm) << "\n);\n";
  os << "  logic [" << (k - 1) << ":0] st;\n";
  for (std::size_t i = 0; i < k; ++i)
    os << "  wire " << fsm.state_vars[i] << " = st[" << (k - 1 - i) << "];\n";
  os << "  always_ff @(posedge clk" << reset_edge(fsm) << ") begin\n";
  os << "    if (" << (low ? "!" : "") << rst << ") begin\n";
  os << "      st <= " << k << "'d" << reset << ";\n";
  os << "    end else begin\n      case (st)\n";
  const u32 states = u32{1} << k;
  const u32 inputs = m == 0 ? 1u : (u32{1} << m);
  for (u32 state = 0; state < states; ++state) {
    os << "        " << k << "'d" << state << ": begin\n";
    if (m == 0) {
      os << "          st <= " << k << "'d" << next_code(fsm, state, 0)
         << ";\n";
    } else {
      os << "          case ({";
      for (std::size_t j = 0; j < m; ++j)
        os << (j ? ", " : "") << fsm.inputs[j];
      os << "})\n";
      for (u32 x = 0; x < inputs; ++x) {
        os << "            " << m << "'b" << bit_string(m, x) << ": st <= " << k
           << "'d" << next_code(fsm, state, x) << ";\n";
      }
      os << "          endcase\n";
    }
    os << "        end\n";
  }
  os << "        default: st <= " << k << "'d" << reset << ";\n";
  os << "      endcase\n    end\n  end\n";
  for (const auto &out : fsm.outputs)
    os << "  assign " << out.name << " = " << sop(fsm, out.func) << ";\n";
  os << "endmodule\n";
  return os.str();
}

std::string emit_seq_miter(const FsmSpec &fsm, int cycles) {
  if (cycles < 1)
    throw std::runtime_error("cycles must be >= 1");
  const std::string rst = reset_port(fsm);
  const bool low = active_low(fsm);
  const u32 last = static_cast<u32>(cycles) + 1;
  std::size_t width = 1;
  while ((u32{1} << width) <= last)
    ++width;
  std::ostringstream os;
  os << "// Generated by dcstudy: bounded-model-check miter.\n";
  os << "module miter(\n  input logic clk";
  for (const auto &in : fsm.inputs)
    os << ",\n  input logic " << in;
  os << ",\n  output logic mismatch\n);\n";
  os << "  localparam logic [" << (width - 1) << ":0] LAST = " << width << "'d"
     << last << ";\n";
  os << "  logic [" << (width - 1) << ":0] cyc = " << width << "'d0;\n";
  os << "  wire " << rst << " = (cyc " << (low ? "==" : "!=") << " " << width
     << "'d0);\n";
  os << "  wire valid = (cyc > " << width << "'d1);\n";
  for (const auto &out : fsm.outputs)
    os << "  wire " << out.name << "_dut, " << out.name << "_gold;\n";
  os << "  top u_dut(" << instance_ports(fsm, "_dut") << ");\n";
  os << "  gold u_gold(" << instance_ports(fsm, "_gold") << ");\n";
  os << "  always_ff @(posedge clk) begin\n";
  os << "    if (cyc != LAST) cyc <= cyc + " << width << "'d1;\n";
  os << "  end\n";
  os << "  assign mismatch = valid & (";
  for (std::size_t i = 0; i < fsm.outputs.size(); ++i) {
    const std::string &name = fsm.outputs[i].name;
    os << (i ? " | " : "") << "(" << name << "_dut !== " << name << "_gold)";
  }
  os << ");\nendmodule\n";
  return os.str();
}

std::string emit_seq_tb(const FsmSpec &fsm, int cycles, std::uint32_t seed) {
  if (cycles < 1)
    throw std::runtime_error("cycles must be >= 1");
  const std::string rst = reset_port(fsm);
  const bool low = active_low(fsm);
  std::ostringstream os;
  os << "// Generated by dcstudy: sequential testbench (seeded LCG).\n";
  os << "module tb;\n  logic clk = 1'b0;\n";
  os << "  logic " << rst << " = 1'b" << (low ? "0" : "1") << ";\n";
  for (const auto &in : fsm.inputs)
    os << "  logic " << in << " = 1'b0;\n";
  for (const auto &out : fsm.outputs)
    os << "  logic " << out.name << "_dut, " << out.name << "_gold;\n";
  os << "  int f = 0;\n  logic [31:0] seed = 32'd" << seed << ";\n";
  os << "  always #5 clk = ~clk;\n";
  os << "  top u_dut(" << instance_ports(fsm, "_dut") << ");\n";
  os << "  gold u_gold(" << instance_ports(fsm, "_gold") << ");\n";
  os << "  initial begin\n";
  os << "    " << rst << " = 1'b" << (low ? "0" : "1") << ";\n";
  os << "    @(posedge clk); #1;\n";
  os << "    " << rst << " = 1'b" << (low ? "1" : "0") << ";\n";
  os << "    repeat (" << cycles << ") begin\n";
  os << "      seed = seed * 32'd1664525 + 32'd1013904223;\n";
  for (std::size_t j = 0; j < fsm.inputs.size(); ++j)
    os << "      " << fsm.inputs[j] << " = seed[" << j << "];\n";
  os << "      @(posedge clk); #1;\n";
  os << "      if (";
  for (std::size_t i = 0; i < fsm.outputs.size(); ++i) {
    const std::string &name = fsm.outputs[i].name;
    os << (i ? " | " : "") << "(" << name << "_dut !== " << name << "_gold)";
  }
  os << ") f++;\n    end\n";
  os << "    if (f == 0) $display(\"PASS\"); else $display(\"FAIL %0d\", f);\n";
  os << "    $finish;\n  end\nendmodule\n";
  return os.str();
}

namespace {

bool is_sequential_cell(const std::string &type) {
  for (const char *prefix : {"DFF", "SDFF", "TLAT", "LATCH", "EDFF"})
    if (type.rfind(prefix, 0) == 0)
      return true;
  return false;
}

} // namespace

SeqProof verify_seq_rtl(const FsmSpec &fsm, const std::string &top_rtl,
                        int cycles, bool simulate, std::uint32_t seed) {
  SeqProof out;
  if (cycles < 1) {
    out.reason = "cycles must be >= 1";
    return out;
  }
  std::string gold;
  std::string miter;
  std::string tb;
  try {
    gold = emit_fsm_gold(fsm);
    miter = emit_seq_miter(fsm, cycles);
    tb = emit_seq_tb(fsm, cycles, seed);
  } catch (const std::exception &e) {
    out.reason = e.what();
    return out;
  }
  TempDir dir;
  write_file(dir.path() / "top.v", top_rtl);
  write_file(dir.path() / "gold.v", gold);
  write_file(dir.path() / "miter.v", miter);
  write_file(dir.path() / "tb.v", tb);

  const std::string steps = std::to_string(cycles + 2);
  const std::string bmc_cmd =
      "cd " + shell_quote(dir.path().string()) + " && yosys -Q -p " +
      shell_quote("read_verilog -sv top.v gold.v miter.v; prep -top miter; "
                  "flatten; delete t:$scopeinfo; async2sync; opt -full; "
                  "sat -seq " +
                  steps + " -prove mismatch 0 miter");
  const int bmc_rc =
      run_logged(bmc_cmd, dir.path() / "bmc.log", kSeqYosysTimeoutSeconds);
  out.log = read_file(dir.path() / "bmc.log");
  if (bmc_rc != 0 ||
      out.log.find("no model found: SUCCESS!") == std::string::npos) {
    out.reason = "bmc failed: " + tail(out.log);
    return out;
  }
  out.bmc = true;

  if (simulate) {
    const std::string build =
        "cd " + shell_quote(dir.path().string()) +
        " && verilator --binary --timing -Wno-fatal -Wno-WIDTH "
        "-Wno-UNUSEDSIGNAL -Wno-DECLFILENAME --top-module tb -o simv "
        "-Mdir obj top.v gold.v tb.v";
    if (run_logged(build, dir.path() / "verilator.log",
                   kSeqVerilatorTimeoutSeconds) != 0) {
      out.log += read_file(dir.path() / "verilator.log");
      out.reason = "verilator build failed: " +
                   tail(read_file(dir.path() / "verilator.log"));
      return out;
    }
    const std::string run =
        "cd " + shell_quote(dir.path().string()) + " && ./obj/simv";
    const int sim_rc =
        run_logged(run, dir.path() / "sim.log", kSeqYosysTimeoutSeconds);
    const std::string sim_log = read_file(dir.path() / "verilator.log") +
                                read_file(dir.path() / "sim.log");
    out.log += sim_log;
    if (sim_rc != 0 || sim_log.find("PASS") == std::string::npos ||
        sim_log.find("FAIL") != std::string::npos) {
      out.reason = "simulation mismatch: " + tail(sim_log);
      return out;
    }
    out.sim = true;
  }
  out.ok = true;
  return out;
}

SeqSynthResult synthesize_fsm(const FsmSpec &fsm,
                              const SeqSynthOptions &options) {
  SeqSynthResult out;
  out.cycles = options.cycles;
  if (options.cycles < 1) {
    out.reason = "cycles must be >= 1";
    return out;
  }
  try {
    out.rtl = emit_fsm_rtl(fsm);
    out.gold = emit_fsm_gold(fsm);
    out.miter = emit_seq_miter(fsm, options.cycles);
    out.tb = emit_seq_tb(fsm, options.cycles, options.seed);
  } catch (const std::exception &e) {
    out.reason = e.what();
    return out;
  }
  const std::string liberty =
      options.liberty.empty() ? default_liberty_path() : options.liberty;
  const std::string cdl =
      options.cdl.empty() ? default_cdl_path() : options.cdl;
  if (!fs::exists(liberty)) {
    out.reason = "Nangate45 liberty not found: " + liberty;
    return out;
  }
  if (!fs::exists(cdl)) {
    out.reason = "Nangate45 CDL not found: " + cdl;
    return out;
  }
  CdlLibrary library;
  try {
    library = CdlLibrary::load(cdl);
  } catch (const std::exception &e) {
    out.reason = e.what();
    return out;
  }

  TempDir dir;
  write_file(dir.path() / "top.v", out.rtl);
  const std::string map_cmd =
      "cd " + shell_quote(dir.path().string()) + " && yosys -Q -p " +
      shell_quote("read_verilog -sv top.v; synth -top top; dfflibmap "
                  "-liberty " +
                  liberty + "; abc -liberty " + liberty +
                  "; opt_clean; write_json net.json; write_verilog -noattr "
                  "net.v");
  if (run_logged(map_cmd, dir.path() / "map.log", kSeqYosysTimeoutSeconds) !=
      0) {
    out.log = read_file(dir.path() / "map.log");
    out.reason = "mapping failed: " + tail(out.log);
    return out;
  }
  out.netlist = read_file(dir.path() / "net.v");
  out.netlist_json = read_file(dir.path() / "net.json");

  try {
    const Json root = Json::parse(out.netlist_json);
    if (!root.contains("modules") || !root.at("modules").contains("top"))
      throw std::runtime_error("mapped netlist has no top module");
    const Json &top = root.at("modules").at("top");
    std::map<std::string, int> counts;
    if (top.contains("cells"))
      for (const auto &[inst, cell] : top.at("cells").object()) {
        (void)inst;
        counts[cell.at("type").as_string()]++;
      }
    int seq_types = 0;
    for (const auto &[type, instances] : counts) {
      if (!library.has(type))
        throw std::runtime_error("mapped to unknown cell: " + type);
      SeqCellUse use;
      use.cell = type;
      use.instances = instances;
      use.transistors = static_cast<int>(library.cell(type).transistor_count());
      use.sequential = is_sequential_cell(type);
      if (use.sequential) {
        out.dffs += instances;
        out.dff_cost += instances * use.transistors;
        out.dff_unit = use.transistors;
        ++seq_types;
      } else {
        out.comb_cost += instances * use.transistors;
      }
      out.cells.push_back(std::move(use));
    }
    if (seq_types > 1)
      out.dff_unit = 0; // mixed flop types: the per-cell list is authoritative
    out.total = out.dff_cost + out.comb_cost;
    if (out.dffs == 0)
      throw std::runtime_error(
          "mapped design has no flip-flops; the state register disappeared");
  } catch (const std::exception &e) {
    out.reason = e.what();
    return out;
  }

  const SeqProof proof = verify_seq_rtl(fsm, out.rtl, options.cycles,
                                        options.simulate, options.seed);
  out.bmc = proof.bmc;
  out.sim = proof.sim;
  out.log = proof.log;
  if (!proof.ok) {
    out.reason = proof.reason;
    return out;
  }
  out.ok = true;
  out.verified = true;
  out.proof_method =
      proof.bmc && proof.sim ? "bmc+sim" : (proof.bmc ? "bmc" : "sim");

  if (!options.out_dir.empty()) {
    std::error_code ec;
    fs::create_directories(options.out_dir, ec);
    const std::vector<std::pair<std::string, std::string>> files = {
        {"top.v", out.rtl},     {"gold.v", out.gold},
        {"miter.v", out.miter}, {"tb.v", out.tb},
        {"net.v", out.netlist}, {"net.json", out.netlist_json}};
    for (const auto &[name, text] : files) {
      write_file(fs::path(options.out_dir) / name, text);
      out.files.push_back(name);
    }
  }
  return out;
}

} // namespace dcstudy
