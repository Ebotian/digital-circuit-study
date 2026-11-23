#include "cli/math_client.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/json.hpp"
#include "cli/z3_client.hpp"

namespace dcstudy {

std::vector<std::vector<int>> graph_closure(
    int n, const std::vector<std::pair<int, int>>& edges) {
  Json req;
  req["op"] = "closure";
  req["n"] = n;
  Json e = Json::Array{};
  for (auto [u, v] : edges) {
    Json pair = Json::Array{};
    pair.push_back(u);
    pair.push_back(v);
    e.push_back(std::move(pair));
  }
  req["edges"] = std::move(e);
  const Json resp = z3_call(req);
  std::vector<std::vector<int>> out;
  for (const auto& row : resp.at("rows").array()) {
    std::vector<int> r;
    for (const auto& v : row.array()) r.push_back(static_cast<int>(v.as_int()));
    out.push_back(std::move(r));
  }
  return out;
}

MathGf2 gf2_analysis(const std::vector<std::vector<int>>& rows, int ncols) {
  Json req;
  req["op"] = "gf2";
  req["ncols"] = ncols;
  Json rr = Json::Array{};
  for (const auto& row : rows) {
    Json r = Json::Array{};
    for (int v : row) r.push_back(v ? 1 : 0);
    rr.push_back(std::move(r));
  }
  req["rows"] = std::move(rr);
  const Json resp = z3_call(req);
  MathGf2 out;
  out.rank = static_cast<int>(resp.at("rank").as_int());
  out.nullity = static_cast<int>(resp.at("nullity").as_int());
  out.ncols = ncols;
  for (const auto& row : resp.at("nullspace").array()) {
    std::vector<int> v;
    for (const auto& x : row.array()) v.push_back(static_cast<int>(x.as_int()));
    out.nullspace.push_back(std::move(v));
  }
  return out;
}

}  // namespace dcstudy
