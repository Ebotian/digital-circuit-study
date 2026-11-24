// Shared CLI helpers: argument parsing, spec loading, JSON/verify plumbing.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/boolfunc.hpp"
#include "core/cmos.hpp"
#include "core/designspace.hpp"
#include "core/driver_profile.hpp"
#include "core/json.hpp"
#include "core/verify.hpp"
#include "synthesis/multistage.hpp"
#include "synthesis/synthesis.hpp"

namespace dcstudy {

struct CliError : std::runtime_error {
  int code;
  explicit CliError(const std::string &msg, int c = 2)
      : std::runtime_error(msg), code(c) {}
};

struct StaticCheck {
  bool ok = false;
  std::string truth;
  std::string target;
};

struct Args {
  std::map<std::string, std::string> opt;
  std::set<std::string> flag;

  bool has(const std::string &k) const { return opt.count(k) || flag.count(k); }
  bool is(const std::string &k) const { return flag.count(k) != 0; }
  std::string get(const std::string &k, const std::string &def = {}) const {
    auto it = opt.find(k);
    return it == opt.end() ? def : it->second;
  }
  std::string need(const std::string &k) const {
    auto it = opt.find(k);
    if (it == opt.end())
      throw CliError("missing --" + k);
    return it->second;
  }
};

enum class VerifyMode { None, Yosys, Sim, Both, Static };

struct Verified {
  bool yosys = false;
  bool sim = false;
  Json json;
};

Args parse_args(int argc, char **argv, int start);
std::vector<std::string> split_csv(const std::string &s);
FuncSpec get_spec(const Args &a);
std::vector<int> target_of(const FuncSpec &s);
std::string join(const std::vector<std::string> &v, const std::string &sep);
Json driver_profile_json(const DriverProfile &p);
Json design_json(const Design &d, const std::vector<std::string> &vars);
Json static_design_json(const CmosDesign &d,
                        const std::vector<std::string> &vars);
std::string subckt_to_cdl(const Subckt &c);
void write_files(const std::filesystem::path &dir, const Design &d,
                 const FuncSpec &spec);
VerifyMode parse_verify(const Args &a, VerifyMode def);
Json verify_json(const ToolResult &r);
Verified verify_design(const Design &d, const FuncSpec &spec, VerifyMode mode,
                       bool keep, const std::filesystem::path *emit_dir);
void emit_error(bool want_json, const std::string &message, int code);
Json cells_json(const std::vector<GateInstance> &instances);
Json multi_design_json(const MultiStageDesign &m);
Json multi_design_json(const MultiStageMultiDesign &m);

} // namespace dcstudy
