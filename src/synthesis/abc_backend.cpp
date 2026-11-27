#include "synthesis/abc_backend.hpp"

#include <sys/wait.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace dcstudy {

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
  out += "'";
  return out;
}

bool executable_file(const std::string &path) {
  std::error_code ec;
  if (path.empty() || !fs::is_regular_file(path, ec))
    return false;
  const auto perms = fs::status(path, ec).permissions();
  return (perms & (fs::perms::owner_exec | fs::perms::group_exec |
                   fs::perms::others_exec)) != fs::perms::none;
}

class TempDir {
public:
  TempDir() {
    static std::atomic<int> counter{0};
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ =
        fs::temp_directory_path() / ("dcstudy-abc-" + std::to_string(stamp) +
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

// ABC echoes the -c command line before its banner, so the version is the
// first line that is neither blank nor an echoed command.
std::string version_line(const std::string &text) {
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    if (line.empty() || line.rfind("=====", 0) == 0)
      continue;
    return line;
  }
  return {};
}

std::string tail(const std::string &text, std::size_t max_lines = 20) {
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

// Run one external step under the wall-clock limit, keeping its output in a log
// file.  timeout(1) reports 124, which the caller turns into an explicit
// reason.
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

struct BlifStats {
  std::size_t names = 0;
  bool flat = true; // .subckt would mean the netlist never reached gate level
};

BlifStats blif_stats(const std::string &blif) {
  BlifStats stats;
  std::istringstream is(blif);
  std::string line;
  while (std::getline(is, line)) {
    if (line.rfind(".names", 0) == 0)
      ++stats.names;
    else if (line.rfind(".subckt", 0) == 0)
      stats.flat = false;
  }
  return stats;
}

} // namespace
std::string find_abc_binary(const std::string &explicit_path) {
  if (!explicit_path.empty())
    return explicit_path;
  if (const char *env = std::getenv("DCSTUDY_ABC"); env && *env)
    return env;
  const char *path_env = std::getenv("PATH");
  if (path_env == nullptr)
    return {};
  const std::string paths(path_env);
  std::size_t start = 0;
  while (start <= paths.size()) {
    const std::size_t end = paths.find(':', start);
    const std::string dir = paths.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (!dir.empty()) {
      const std::string candidate = dir + "/abc";
      if (executable_file(candidate))
        return candidate;
    }
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return {};
}

AbcResult abc_rewrite(const std::string &verilog, const std::string &top,
                      const AbcOptions &options) {
  AbcResult out;
  if (verilog.empty() || top.empty()) {
    out.reason = "abc needs a module to rewrite";
    return out;
  }
  if (options.timeout_seconds <= 0) {
    out.reason = "abc timeout must be positive";
    return out;
  }
  if (options.optimize)
    out.commands =
        options.commands.empty() ? kDefaultAbcCommands : options.commands;
  const std::string binary = find_abc_binary(options.binary);
  if (binary.empty()) {
    out.reason = "abc not found: set DCSTUDY_ABC or put abc on PATH";
    return out;
  }

  TempDir dir;
  const fs::path &work = dir.path();
  write_file(work / "in.v", verilog);

  // 1. Verilog -> gate-level, flattened BLIF.  techmap is what turns Yosys
  //    internal cells into .names tables, which is the only BLIF ABC reads.
  const std::string to_blif =
      "cd " + shell_quote(work.string()) + " && yosys -Q -p " +
      shell_quote("read_verilog -sv in.v; hierarchy -top " + top +
                  "; flatten; techmap; opt_clean; write_blif in.blif");
  if (run_logged(to_blif, work / "yosys_in.log", options.timeout_seconds) !=
      0) {
    out.reason =
        "yosys could not write BLIF: " + tail(read_file(work / "yosys_in.log"));
    return out;
  }
  const BlifStats before = blif_stats(read_file(work / "in.blif"));
  out.nodes_before = before.names;
  if (!before.flat) {
    out.reason = "netlist did not flatten to gate-level BLIF";
    return out;
  }
  if (before.names == 0) {
    out.reason = "netlist has no nodes";
    return out;
  }
  if (before.names > kMaxAbcNodes) {
    out.reason = "netlist has " + std::to_string(before.names) +
                 " nodes, over " + "the " + std::to_string(kMaxAbcNodes) +
                 " node cap";
    return out;
  }

  // 2. Version banner, recorded so a rewrite can be reproduced.
  const std::string version_cmd = "cd " + shell_quote(work.string()) + " && " +
                                  shell_quote(binary) + " -c " +
                                  shell_quote("version");
  if (run_logged(version_cmd, work / "abc_version.log",
                 options.timeout_seconds) != 0) {
    out.reason = "abc could not report its version: " +
                 tail(read_file(work / "abc_version.log"));
    return out;
  }
  out.version = version_line(read_file(work / "abc_version.log"));
  while (!out.version.empty() && out.version.back() == '\r')
    out.version.pop_back();

  // 3. The rewrite itself.  No amap/liberty here: mapping stays in the flow.
  std::string script = "read_blif in.blif; ";
  if (!out.commands.empty())
    script += out.commands + "; ";
  script += "write_blif out.blif";
  const std::string abc_cmd = "cd " + shell_quote(work.string()) + " && " +
                              shell_quote(binary) + " -c " +
                              shell_quote(script);
  const int rc = run_logged(abc_cmd, work / "abc.log", options.timeout_seconds);
  out.log = tail(read_file(work / "abc.log"));
  if (rc == 124) {
    out.reason =
        "abc timed out after " + std::to_string(options.timeout_seconds) + "s";
    return out;
  }
  if (rc != 0) {
    out.reason = "abc failed: " + out.log;
    return out;
  }
  if (!fs::exists(work / "out.blif")) {
    out.reason = "abc wrote no netlist: " + out.log;
    return out;
  }
  const BlifStats after = blif_stats(read_file(work / "out.blif"));
  out.nodes_after = after.names;
  if (!after.flat) {
    out.reason = "abc output is not flat BLIF";
    return out;
  }
  if (after.names > before.names) {
    out.reason = "abc grew the netlist (" + std::to_string(before.names) +
                 " -> " + std::to_string(after.names) +
                 " nodes), keeping the original";
    return out;
  }

  // 4. BLIF -> Verilog.
  const std::string to_verilog =
      "cd " + shell_quote(work.string()) + " && yosys -Q -p " +
      shell_quote("read_blif out.blif; write_verilog -noattr out.v");
  if (run_logged(to_verilog, work / "yosys_out.log", options.timeout_seconds) !=
      0) {
    out.reason = "yosys could not read ABC's BLIF: " +
                 tail(read_file(work / "yosys_out.log"));
    return out;
  }
  out.verilog = read_file(work / "out.v");
  if (out.verilog.empty()) {
    out.reason = "yosys wrote no Verilog";
    return out;
  }
  if (out.verilog.find("module " + top) == std::string::npos) {
    out.reason = "ABC output lost module " + top;
    return out;
  }
  out.ok = true;
  return out;
}

} // namespace dcstudy
