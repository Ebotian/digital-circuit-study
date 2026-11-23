#include "cli/worker_client.hpp"

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

std::string read_file(const fs::path &p) {
  std::ifstream in(p);
  std::ostringstream os;
  os << in.rdbuf();
  return os.str();
}

std::string tail(const std::string &s, std::size_t max_lines = 40) {
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

bool is_file(const fs::path &p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

bool is_executable(const fs::path &p) {
  return is_file(p) && ::access(p.c_str(), X_OK) == 0;
}

std::string executable_command(const std::string &helper) {
  if (helper.size() >= 3 && helper.substr(helper.size() - 3) == ".py")
    return "python3 " + shell_quote(helper);
  if (helper.size() >= 3 && helper.substr(helper.size() - 3) == ".ts")
    return "node " + shell_quote(helper);
  if (helper.size() >= 3 && helper.substr(helper.size() - 3) == ".js")
    return "node " + shell_quote(helper);
  return shell_quote(helper);
}

} // namespace

std::string find_worker(const WorkerLocator &locator) {
  if (!locator.env_var.empty()) {
    if (const char *env = std::getenv(locator.env_var.c_str()); env && *env)
      return env;
  }

  std::vector<fs::path> dirs;
  std::error_code ec;
  const fs::path self = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    dirs.push_back(self.parent_path());
    dirs.push_back(self.parent_path() / ".." / "workers");
  }
  dirs.push_back(fs::current_path() / "workers");
  dirs.push_back(fs::current_path());
  for (const auto &d : dirs) {
    const fs::path p = d / locator.filename;
    if (is_file(p))
      return fs::weakly_canonical(p, ec).string();
  }
  if (is_executable(locator.filename))
    return locator.filename;
  return {};
}

Json call_worker(const std::string &helper, const Json &request,
                 int timeout_seconds) {
  if (helper.empty())
    throw std::runtime_error("worker helper not found");

  static std::atomic<int> counter{0};
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path dir =
      fs::temp_directory_path() /
      ("dcstudy-worker-" + std::to_string(::getpid()) + "-" +
       std::to_string(stamp) + "-" + std::to_string(counter++));
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec)
    throw std::runtime_error("cannot create worker exchange directory");

  const fs::path req = dir / "req.json";
  const fs::path resp = dir / "resp.json";
  const fs::path err = dir / "err.log";
  {
    std::ofstream out(req);
    if (!out)
      throw std::runtime_error("cannot write worker request");
    out << request.dump(0);
  }

  const std::string cmd =
      "timeout " + std::to_string(timeout_seconds) + " " +
      executable_command(helper) + " < " + shell_quote(req.string()) + " > " +
      shell_quote(resp.string()) + " 2> " + shell_quote(err.string());
  const int rc = std::system(cmd.c_str());
  const int code = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
  const std::string body = read_file(resp);
  const std::string error_log = read_file(err);
  fs::remove_all(dir, ec);

  if (code == 124)
    throw std::runtime_error("worker timed out after " +
                             std::to_string(timeout_seconds) + "s");
  if (!body.empty()) {
    const Json j = Json::parse(body);
    if (!j.is_object())
      throw std::runtime_error("worker returned a non-object");
    if (j.contains("ok") && j.at("ok").is_bool() && !j.at("ok").as_bool())
      throw std::runtime_error(j.contains("error") ? j.at("error").as_string()
                                                   : "worker error");
    if (code != 0)
      throw std::runtime_error("worker failed (exit " + std::to_string(code) +
                               "): " + tail(error_log));
    return j;
  }
  throw std::runtime_error("worker returned no response: " + tail(error_log));
}

} // namespace dcstudy
