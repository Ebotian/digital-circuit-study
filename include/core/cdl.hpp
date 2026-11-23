// CDL (SPICE-like) standard-cell netlist reader.
#pragma once

#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace dcstudy {

enum class PinDir { Unknown, Input, Output, Power, Ground };

struct Device {
  std::string name;
  std::string drain;
  std::string gate;
  std::string source;
  std::string model;
  bool pmos = false;
};

struct Subckt {
  std::string name;
  std::vector<std::string> ports;
  std::map<std::string, PinDir> pin_dir;
  // Port aliases (`*.ALIAS alias canon`): a net that is exposed under two
  // names, e.g. two outputs defined by the same expression.
  std::map<std::string, std::string> aliases;
  std::vector<Device> devices;
  std::string eqn;

  std::size_t transistor_count() const { return devices.size(); }
  std::vector<std::string> inputs() const;
  std::vector<std::string> outputs() const;
  PinDir direction(const std::string &port) const;
};

class CdlLibrary {
public:
  static CdlLibrary load(const std::string &path);

  const std::vector<Subckt> &cells() const { return cells_; }
  const Subckt &cell(const std::string &name) const;
  bool has(const std::string &name) const;

private:
  std::vector<Subckt> cells_;
  std::unordered_map<std::string, std::size_t> index_;
  std::unordered_map<std::string, std::size_t> upper_index_;
};

std::string expand_home(const std::string &path);
std::string default_cdl_path();

} // namespace dcstudy
