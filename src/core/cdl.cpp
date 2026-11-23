#include "core/cdl.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace dcstudy {
namespace {

std::string upper(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

std::vector<std::string> split_ws(const std::string& s) {
  std::istringstream is(s);
  std::vector<std::string> out;
  std::string tok;
  while (is >> tok) out.push_back(tok);
  return out;
}

PinDir parse_dir(const std::string& d) {
  const std::string u = upper(d);
  if (u == "I") return PinDir::Input;
  if (u == "O") return PinDir::Output;
  if (u == "P") return PinDir::Power;
  if (u == "G") return PinDir::Ground;
  return PinDir::Unknown;
}

}  // namespace

std::string expand_home(const std::string& path) {
  if (path.empty() || path[0] != '~') return path;
  const char* home = std::getenv("HOME");
  if (!home) return path;
  if (path.size() == 1) return home;
  if (path[1] == '/') return std::string(home) + path.substr(1);
  return path;
}

std::string default_cdl_path() {
  if (const char* p = std::getenv("NANGATE45_CDL"); p && *p)
    return expand_home(p);
  if (const char* p = std::getenv("PDK_ROOT"); p && *p)
    return expand_home(std::string(p) +
                       "/nangate45/cdl/NangateOpenCellLibrary.cdl");
  if (const char* h = std::getenv("HOME"); h && *h)
    return std::string(h) + "/yosys-sta/pdk/nangate45/cdl/NangateOpenCellLibrary.cdl";
  return "nangate45/cdl/NangateOpenCellLibrary.cdl";
}

PinDir Subckt::direction(const std::string& port) const {
  auto it = pin_dir.find(port);
  if (it != pin_dir.end()) return it->second;
  const std::string u = upper(port);
  if (u == "VDD" || u == "VCC" || u == "VPWR") return PinDir::Power;
  if (u == "VSS" || u == "GND" || u == "VGND") return PinDir::Ground;
  return PinDir::Unknown;
}

std::vector<std::string> Subckt::inputs() const {
  std::vector<std::string> out;
  for (const auto& p : ports) {
    auto d = direction(p);
    if (d == PinDir::Input) out.push_back(p);
  }
  if (!out.empty()) return out;
  // Fallback for libraries without *.PININFO: everything that is not a supply
  // and not the (single) output.
  const auto outs = outputs();
  for (const auto& p : ports) {
    auto d = direction(p);
    if (d == PinDir::Power || d == PinDir::Ground) continue;
    if (std::find(outs.begin(), outs.end(), p) != outs.end()) continue;
    out.push_back(p);
  }
  return out;
}

std::vector<std::string> Subckt::outputs() const {
  std::vector<std::string> out;
  for (const auto& p : ports) {
    auto d = direction(p);
    if (d == PinDir::Output) out.push_back(p);
  }
  if (!out.empty()) return out;
  for (const auto& p : ports) {
    auto d = direction(p);
    if (d == PinDir::Unknown) out.push_back(p);
  }
  if (out.size() > 1) out.resize(1);
  return out;
}

CdlLibrary CdlLibrary::load(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open CDL file: " + path);
  CdlLibrary lib;
  Subckt* cur = nullptr;
  std::string physical, logical;
  std::size_t line_no = 0;

  auto handle = [&](const std::string& raw, std::size_t ln) {
    const std::string trimmed = [&] {
      auto b = raw.find_first_not_of(" \t\r");
      if (b == std::string::npos) return std::string();
      auto e = raw.find_last_not_of(" \t\r");
      return raw.substr(b, e - b + 1);
    }();
    if (trimmed.empty()) return;

    if (trimmed.rfind("*.PININFO", 0) == 0) {
      if (!cur) return;
      auto toks = split_ws(trimmed.substr(9));
      for (const auto& t : toks) {
        auto c = t.find(':');
        if (c == std::string::npos) continue;
        cur->pin_dir[t.substr(0, c)] = parse_dir(t.substr(c + 1));
      }
      return;
    }
    if (trimmed.rfind("*.EQN", 0) == 0) {
      if (cur) cur->eqn = trimmed.substr(5);
      return;
    }
    if (trimmed.rfind("*.ALIAS", 0) == 0) {
      if (!cur) return;
      auto toks = split_ws(trimmed.substr(7));
      if (toks.size() >= 2) cur->aliases[toks[0]] = toks[1];
      return;
    }
    if (trimmed[0] == '*') return;  // ordinary comment

    auto toks = split_ws(trimmed);
    if (toks.empty()) return;
    const std::string head = upper(toks[0]);
    if (head == ".SUBCKT") {
      if (toks.size() < 2) throw std::runtime_error("bad .SUBCKT at line " + std::to_string(ln));
      Subckt s;
      s.name = toks[1];
      for (std::size_t i = 2; i < toks.size(); ++i) s.ports.push_back(toks[i]);
      lib.cells_.push_back(std::move(s));
      cur = &lib.cells_.back();
    } else if (head == ".ENDS") {
      cur = nullptr;
    } else if (!toks[0].empty() &&
               (toks[0][0] == 'M' || toks[0][0] == 'm')) {
      if (!cur) return;
      // M<name> drain gate source [bulk] model [params...]
      if (toks.size() < 6)
        throw std::runtime_error("short MOSFET line " + std::to_string(ln));
      std::size_t model_idx = 0;
      for (std::size_t i = 4; i < toks.size(); ++i) {
        const std::string u = upper(toks[i]);
        if (u.find("NMOS") != std::string::npos ||
            u.find("PMOS") != std::string::npos) {
          model_idx = i;
          break;
        }
      }
      if (model_idx == 0)
        throw std::runtime_error("no NMOS/PMOS model on line " + std::to_string(ln));
      Device d;
      d.name = toks[0];
      d.drain = toks[1];
      d.gate = toks[2];
      d.source = toks[3];
      d.model = toks[model_idx];
      d.pmos = upper(d.model).find("PMOS") != std::string::npos;
      cur->devices.push_back(std::move(d));
    }
  };

  while (std::getline(in, physical)) {
    ++line_no;
    if (!physical.empty() && physical[0] == '+') {
      logical += " " + physical.substr(1);
      continue;
    }
    if (!logical.empty()) handle(logical, line_no - 1);
    logical = physical;
  }
  if (!logical.empty()) handle(logical, line_no);

  for (std::size_t i = 0; i < lib.cells_.size(); ++i) {
    lib.index_.emplace(lib.cells_[i].name, i);
    lib.upper_index_.emplace(upper(lib.cells_[i].name), i);
  }
  return lib;
}

bool CdlLibrary::has(const std::string& name) const {
  return index_.count(name) || upper_index_.count(upper(name));
}

const Subckt& CdlLibrary::cell(const std::string& name) const {
  auto it = index_.find(name);
  if (it != index_.end()) return cells_.at(it->second);
  auto uit = upper_index_.find(upper(name));
  if (uit != upper_index_.end()) return cells_.at(uit->second);
  throw std::runtime_error("cell not found: " + name);
}

}  // namespace dcstudy
