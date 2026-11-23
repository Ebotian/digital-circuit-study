#include "z3/z3_protocol.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace dcstudy {
namespace {

const Json& need(const Json& j, const std::string& key) {
  if (!j.is_object() || !j.contains(key))
    throw std::runtime_error("protocol: missing field '" + key + "'");
  return j.at(key);
}

std::vector<std::string> strings(const Json& j, const std::string& what) {
  if (!j.is_array()) throw std::runtime_error("protocol: " + what + " must be an array");
  std::vector<std::string> out;
  out.reserve(j.size());
  for (const auto& v : j.array()) {
    if (!v.is_string()) throw std::runtime_error("protocol: " + what + " must contain strings");
    out.push_back(v.as_string());
  }
  return out;
}

Json string_array(const std::vector<std::string>& v) {
  Json a = Json::Array{};
  for (const auto& s : v) a.push_back(s);
  return a;
}

char logic_char(Logic v) {
  switch (v) {
    case Logic::Zero: return '0';
    case Logic::One: return '1';
    case Logic::Float: return 'Z';
    case Logic::Race: return 'X';
  }
  return '?';
}

Logic parse_logic(const std::string& s) {
  if (s == "0") return Logic::Zero;
  if (s == "1") return Logic::One;
  if (s == "Z" || s == "z") return Logic::Float;
  if (s == "X" || s == "x") return Logic::Race;
  throw std::runtime_error("protocol: bad logic value '" + s + "'");
}

}  // namespace

Json encode_synth_problem(const SynthProblem& p) {
  Json j;
  j["op"] = "synth";
  j["n_rows"] = p.n_rows;
  j["n_inverters"] = p.n_inverters;
  j["max_cost"] = p.max_cost;
  j["limit"] = p.limit;
  Json branches = Json::Array{};
  for (std::size_t i = 0; i < p.costs.size(); ++i) {
    Json b;
    b["cost"] = p.costs[i];
    b["req"] = static_cast<std::int64_t>(p.req[i]);
    if (i < p.groups.size()) b["group"] = p.groups[i];
    branches.push_back(std::move(b));
  }
  j["branches"] = std::move(branches);
  Json covers = Json::Array{};
  for (const auto& row : p.covers) {
    Json ids = Json::Array{};
    for (int id : row) ids.push_back(id);
    covers.push_back(std::move(ids));
  }
  j["covers"] = std::move(covers);
  if (!p.conducts.empty()) {
    Json conducts = Json::Array{};
    for (const auto& row : p.conducts) {
      Json ids = Json::Array{};
      for (int id : row) ids.push_back(id);
      conducts.push_back(std::move(ids));
    }
    j["conducts"] = std::move(conducts);
  }
  Json syms = Json::Array{};
  for (const auto& sym : p.symmetries) {
    Json a = Json::Array{};
    for (int v : sym) a.push_back(v);
    syms.push_back(std::move(a));
  }
  j["symmetries"] = std::move(syms);
  return j;
}

SynthProblem decode_synth_problem(const Json& j) {
  SynthProblem p;
  p.n_rows = static_cast<int>(need(j, "n_rows").as_int());
  p.n_inverters = static_cast<int>(need(j, "n_inverters").as_int());
  p.max_cost = static_cast<int>(need(j, "max_cost").as_int());
  p.limit = static_cast<int>(need(j, "limit").as_int());
  const Json& bs = need(j, "branches");
  if (!bs.is_array()) throw std::runtime_error("protocol: branches must be an array");
  for (const auto& b : bs.array()) {
    p.costs.push_back(static_cast<int>(need(b, "cost").as_int()));
    p.req.push_back(static_cast<std::uint32_t>(need(b, "req").as_int()));
    p.groups.push_back(b.contains("group") ? static_cast<int>(b.at("group").as_int()) : -1);
  }
  const Json& cs = need(j, "covers");
  if (!cs.is_array()) throw std::runtime_error("protocol: covers must be an array");
  if (static_cast<int>(cs.size()) != p.n_rows)
    throw std::runtime_error("protocol: covers size does not match n_rows");
  for (const auto& row : cs.array()) {
    if (!row.is_array()) throw std::runtime_error("protocol: cover row must be an array");
    std::vector<int> ids;
    for (const auto& c : row.array()) ids.push_back(static_cast<int>(c.as_int()));
    p.covers.push_back(std::move(ids));
  }
  if (p.costs.size() != p.req.size() || p.groups.size() != p.costs.size())
    throw std::runtime_error("protocol: cost/req/group size mismatch");
  if (j.contains("conducts")) {
    const Json& rows = j.at("conducts");
    if (!rows.is_array() || static_cast<int>(rows.size()) != p.n_rows)
      throw std::runtime_error("protocol: conducts size mismatch");
    for (const auto& row : rows.array()) {
      if (!row.is_array()) throw std::runtime_error("protocol: conducts row must be an array");
      std::vector<int> ids;
      for (const auto& id : row.array()) ids.push_back(static_cast<int>(id.as_int()));
      p.conducts.push_back(std::move(ids));
    }
  }
  if (j.contains("symmetries")) {
    const Json& syms = j.at("symmetries");
    if (!syms.is_array()) throw std::runtime_error("protocol: symmetries must be an array");
    for (const auto& a : syms.array()) {
      if (!a.is_array()) throw std::runtime_error("protocol: symmetry must be an array");
      std::vector<int> sym;
      for (const auto& v : a.array()) sym.push_back(static_cast<int>(v.as_int()));
      if (sym.size() != p.costs.size())
        throw std::runtime_error("protocol: symmetry size mismatch");
      p.symmetries.push_back(std::move(sym));
    }
  }
  if (p.n_rows < 1 || p.n_inverters < 0 || p.limit < 1)
    throw std::runtime_error("protocol: bad synthesis dimensions");
  return p;
}

Json encode_synth_solution(const SynthSolution& s) {
  Json j;
  j["ok"] = true;
  j["feasible"] = s.feasible;
  j["optimum"] = s.optimum;
  j["message"] = s.message;
  Json designs = Json::Array{};
  for (const auto& d : s.designs) {
    Json ids = Json::Array{};
    for (int id : d) ids.push_back(id);
    designs.push_back(std::move(ids));
  }
  j["designs"] = std::move(designs);
  return j;
}

SynthSolution decode_synth_solution(const Json& j) {
  if (j.contains("ok") && j.at("ok").is_bool() && !j.at("ok").as_bool())
    throw std::runtime_error(j.contains("error") ? j.at("error").as_string()
                                                 : "Z3 helper failed");
  SynthSolution s;
  s.feasible = need(j, "feasible").as_bool();
  s.optimum = static_cast<int>(need(j, "optimum").as_int());
  if (j.contains("message") && j.at("message").is_string())
    s.message = j.at("message").as_string();
  const Json& ds = need(j, "designs");
  if (!ds.is_array()) throw std::runtime_error("protocol: designs must be an array");
  for (const auto& d : ds.array()) {
    if (!d.is_array()) throw std::runtime_error("protocol: design must be an array");
    std::vector<int> ids;
    for (const auto& id : d.array()) ids.push_back(static_cast<int>(id.as_int()));
    s.designs.push_back(std::move(ids));
  }
  return s;
}

Json encode_switch_problem(const SwitchProblem& p) {
  Json j;
  j["op"] = "switch";
  j["vdd"] = p.vdd;
  j["vss"] = p.vss;
  j["inputs"] = string_array(p.inputs);
  j["outputs"] = string_array(p.outputs);
  Json rows = Json::Array{};
  for (auto r : p.rows) rows.push_back(static_cast<std::int64_t>(r));
  j["rows"] = std::move(rows);
  auto names_json = [](const std::vector<std::vector<std::string>>& rows) {
    Json out = Json::Array{};
    for (const auto& row : rows) {
      Json names = Json::Array{};
      for (const auto& n : row) names.push_back(n);
      out.push_back(std::move(names));
    }
    return out;
  };
  if (!p.forced0.empty()) j["forced0"] = names_json(p.forced0);
  if (!p.forced1.empty()) j["forced1"] = names_json(p.forced1);
  Json devs = Json::Array{};
  for (const auto& d : p.devices) {
    Json dj;
    dj["d"] = d.drain;
    dj["g"] = d.gate;
    dj["s"] = d.source;
    dj["p"] = d.pmos;
    devs.push_back(std::move(dj));
  }
  j["devices"] = std::move(devs);
  return j;
}

SwitchProblem decode_switch_problem(const Json& j) {
  SwitchProblem p;
  p.vdd = need(j, "vdd").as_string();
  p.vss = need(j, "vss").as_string();
  p.inputs = strings(need(j, "inputs"), "inputs");
  p.outputs = strings(need(j, "outputs"), "outputs");
  const Json& rs = need(j, "rows");
  if (!rs.is_array()) throw std::runtime_error("protocol: rows must be an array");
  for (const auto& r : rs.array())
    p.rows.push_back(static_cast<std::uint32_t>(r.as_int()));
  auto read_rows = [&](const char* key,
                       std::vector<std::vector<std::string>>& dst) {
    if (!j.contains(key)) return;
    for (const auto& row : j.at(key).array()) {
      std::vector<std::string> names;
      for (const auto& n : row.array()) names.push_back(n.as_string());
      dst.push_back(std::move(names));
    }
    if (dst.size() != p.rows.size())
      throw std::runtime_error(std::string("protocol: ") + key +
                               " rows do not match rows");
  };
  read_rows("forced0", p.forced0);
  read_rows("forced1", p.forced1);
  const Json& ds = need(j, "devices");
  if (!ds.is_array()) throw std::runtime_error("protocol: devices must be an array");
  for (const auto& dj : ds.array()) {
    Device d;
    d.drain = need(dj, "d").as_string();
    d.gate = need(dj, "g").as_string();
    d.source = need(dj, "s").as_string();
    d.pmos = need(dj, "p").as_bool();
    d.model = d.pmos ? "PMOS" : "NMOS";
    p.devices.push_back(std::move(d));
  }
  if (p.inputs.empty() || p.outputs.empty() || p.rows.empty())
    throw std::runtime_error("protocol: empty switch problem");
  return p;
}

Json encode_switch_solution(const SwitchSolution& s) {
  Json j;
  j["ok"] = true;
  Json rows = Json::Array{};
  for (const auto& r : s.rows) {
    Json rj;
    Json vals = Json::Array{};
    for (auto v : r.values) vals.push_back(std::string(1, logic_char(v)));
    rj["values"] = std::move(vals);
    rj["conflict"] = r.conflict;
    rj["unsat"] = r.unsat;
    rows.push_back(std::move(rj));
  }
  j["rows"] = std::move(rows);
  return j;
}

SwitchSolution decode_switch_solution(const Json& j) {
  if (j.contains("ok") && j.at("ok").is_bool() && !j.at("ok").as_bool())
    throw std::runtime_error(j.contains("error") ? j.at("error").as_string()
                                                 : "Z3 helper failed");
  SwitchSolution s;
  const Json& rows = need(j, "rows");
  if (!rows.is_array()) throw std::runtime_error("protocol: rows must be an array");
  for (const auto& rj : rows.array()) {
    SwitchSolutionRow r;
    const Json& vals = need(rj, "values");
    if (!vals.is_array()) throw std::runtime_error("protocol: values must be an array");
    for (const auto& v : vals.array()) r.values.push_back(parse_logic(v.as_string()));
    r.conflict = rj.contains("conflict") && rj.at("conflict").as_bool();
    r.unsat = rj.contains("unsat") && rj.at("unsat").as_bool();
    s.rows.push_back(std::move(r));
  }
  return s;
}

}  // namespace dcstudy
