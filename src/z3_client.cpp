#include "dcstudy/z3_client.hpp"

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

std::string tail(const std::string& s, std::size_t max_lines = 40) {
  std::vector<std::string> lines;
  std::istringstream is(s);
  std::string line;
  while (std::getline(is, line)) lines.push_back(line);
  const std::size_t start = lines.size() > max_lines ? lines.size() - max_lines : 0;
  std::string out;
  for (std::size_t i = start; i < lines.size(); ++i) out += lines[i] + "\n";
  return out;
}

}  // namespace

std::string z3_helper_path() {
  const auto executable = [](const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec) && (::access(p.c_str(), X_OK) == 0);
  };

  if (const char* env = std::getenv("DCSTUDY_Z3_HELPER"); env && *env) {
    if (executable(env)) return env;
  }
  std::error_code ec;
  const fs::path self = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    const fs::path beside = self.parent_path() / "dcstudy-z3";
    if (executable(beside)) return beside.string();
  }
  if (executable("dcstudy-z3")) return "dcstudy-z3";
  return {};
}

Json z3_call(const Json& request, int timeout_seconds) {
  const std::string helper = z3_helper_path();
  if (helper.empty())
    throw std::runtime_error(
        "Z3 helper not found; build the dcstudy-z3 target or set "
        "DCSTUDY_Z3_HELPER");

  static std::atomic<int> counter{0};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir = fs::temp_directory_path() /
                       ("dcstudy-z3-" + std::to_string(::getpid()) + "-" +
                        std::to_string(stamp) + "-" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) throw std::runtime_error("cannot create Z3 exchange directory");

  const fs::path req = dir / "req.json";
  const fs::path resp = dir / "resp.json";
  const fs::path err = dir / "err.log";
  {
    std::ofstream out(req);
    if (!out) throw std::runtime_error("cannot write Z3 request");
    out << request.dump(0);
  }

  const std::string cmd = "timeout " + std::to_string(timeout_seconds) + " " +
                          shell_quote(helper) + " < " + shell_quote(req.string()) +
                          " > " + shell_quote(resp.string()) + " 2> " +
                          shell_quote(err.string());
  const int rc = std::system(cmd.c_str());
  const int code = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
  const std::string body = read_file(resp);
  const std::string error_log = read_file(err);
  fs::remove_all(dir, ec);

  if (code == 124)
    throw std::runtime_error("Z3 helper timed out after " +
                             std::to_string(timeout_seconds) + "s");
  if (!body.empty()) {
    const Json j = Json::parse(body);
    if (!j.is_object())
      throw std::runtime_error("Z3 helper returned a non-object");
    if (j.contains("ok") && j.at("ok").is_bool() && !j.at("ok").as_bool())
      throw std::runtime_error(j.contains("error") ? j.at("error").as_string()
                                                   : "Z3 helper error");
    if (code != 0)
      throw std::runtime_error("Z3 helper failed (exit " + std::to_string(code) +
                               "): " + tail(error_log));
    return j;
  }
  throw std::runtime_error("Z3 helper returned no response: " + tail(error_log));
}

}  // namespace dcstudy
