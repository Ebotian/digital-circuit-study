#include "core/verify.hpp"
#include "core/limits.hpp"

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

#include "core/verilog.hpp"

namespace dcstudy {

constexpr std::size_t kLogTailLines = 60;
namespace {

namespace fs = std::filesystem;

class TempDir {
public:
  explicit TempDir(bool keep) : keep_(keep) {
    static std::atomic<int> counter{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = fs::temp_directory_path() /
            ("dcstudy-" + std::to_string(::getpid()) + "-" +
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

void write_file(const fs::path &p, const std::string &s) {
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

std::string shell_quote_str(const std::string &s) {
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

std::string shell_quote(const fs::path &p) {
  return shell_quote_str(p.string());
}

int run_shell(const std::string &command, const fs::path &log) {
  const std::string full = "timeout " + std::to_string(kVerifyTimeoutSeconds) +
                           " bash -c " + shell_quote_str(command) + " > " +
                           shell_quote(log) + " 2>&1";
  const int rc =
      std::system(full.c_str()); // NOLINT(bugprone-command-processor)
  if (rc == -1)
    return -1;
  if (WIFEXITED(rc))
    return WEXITSTATUS(rc);
  return -1;
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

} // namespace

ToolResult prove_yosys(const std::string &dut_source, const BoolFunc &target,
                       const std::vector<std::string> &vars, bool keep) {
  ToolResult result;
  TempDir dir(keep);
  if (keep)
    result.workdir = dir.path().string();
  write_file(dir.path() / "dut.v", dut_source);
  write_file(dir.path() / "ref.v", emit_ref(target, vars));
  write_file(dir.path() / "miter.v", emit_miter(vars));

  const std::string script =
      "read_verilog -sv ref.v dut.v miter.v; synth -top miter; flatten; "
      "opt -full; sat -prove " +
      miter_names(vars).mismatch + " 0 -verify -show-inputs";
  const std::string cmd = "cd " + shell_quote(dir.path()) + " && yosys -Q -p " +
                          shell_quote(fs::path(script));
  result.exit_code = run_shell(cmd, dir.path() / "yosys.log");
  const std::string log = read_file(dir.path() / "yosys.log");
  result.ok = result.exit_code == 0 &&
              log.find("SAT proof finished - no model found: SUCCESS!") !=
                  std::string::npos;
  result.output = tail(log);
  return result;
}

ToolResult simulate_verilator(const std::string &dut_source,
                              const BoolFunc &target,
                              const std::vector<std::string> &vars, bool keep) {
  ToolResult result;
  TempDir dir(keep);
  if (keep)
    result.workdir = dir.path().string();
  write_file(dir.path() / "dut.v", dut_source);
  write_file(dir.path() / "tb.v", emit_tb(target, vars));

  const std::string build =
      "cd " + shell_quote(dir.path()) +
      " && verilator --binary --timing -Wno-fatal -Wno-WIDTH "
      "-Wno-UNUSEDSIGNAL -Wno-DECLFILENAME --top-module tb -o simv "
      "-Mdir obj dut.v tb.v";
  const int build_rc = run_shell(build, dir.path() / "verilator.log");
  if (build_rc != 0) {
    result.exit_code = build_rc;
    result.output = tail(read_file(dir.path() / "verilator.log"));
    return result;
  }
  const std::string run = "cd " + shell_quote(dir.path()) + " && ./obj/simv";
  const int rc = run_shell(run, dir.path() / "sim.log");
  const std::string log = read_file(dir.path() / "verilator.log") +
                          read_file(dir.path() / "sim.log");
  result.exit_code = rc;
  result.ok = rc == 0 && log.find("PASS") != std::string::npos &&
              log.find("FAIL") == std::string::npos;
  result.output = tail(log);
  return result;
}

} // namespace dcstudy
