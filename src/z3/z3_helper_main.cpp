// Out-of-process Z3 worker.  Reads one JSON request on stdin and writes one
// JSON response on stdout.  The safe CLI never links libz3.
#include <exception>
#include <iostream>
#include <iterator>
#include <string>

#include "core/json.hpp"
#include "z3/math_engine.hpp"
#include "z3/symmetry.hpp"
#include "z3/z3_protocol.hpp"
#include "z3/z3_solvers.hpp"

using namespace dcstudy;

static int run() {
  try {
    const std::string input((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
    const Json req = Json::parse(input);
    if (!req.is_object() || !req.contains("op") || !req.at("op").is_string())
      throw std::runtime_error("missing op");
    const std::string op = req.at("op").as_string();
    Json resp;
    if (op == "synth") {
      resp = encode_synth_solution(z3_solve_synth(decode_synth_problem(req)));
    } else if (op == "switch") {
      resp =
          encode_switch_solution(z3_solve_switch(decode_switch_problem(req)));
    } else if (op == "symmetry") {
      const int n = static_cast<int>(req.at("n").as_int());
      const std::string truth = req.at("truth").as_string();
      const auto perms =
          bliss_automorphisms(static_cast<std::size_t>(n), truth);
      Json arr = Json::Array{};
      for (const auto &p : perms) {
        Json one = Json::Array{};
        for (auto v : p)
          one.push_back(static_cast<std::int64_t>(v));
        arr.push_back(std::move(one));
      }
      resp["ok"] = true;
      resp["permutations"] = std::move(arr);
    } else if (op == "closure") {
      const int n = static_cast<int>(req.at("n").as_int());
      std::vector<std::pair<int, int>> edges;
      for (const auto &e : req.at("edges").array()) {
        const auto &p = e.array();
        if (p.size() != 2)
          throw std::runtime_error("closure: bad edge");
        edges.emplace_back(static_cast<int>(p[0].as_int()),
                           static_cast<int>(p[1].as_int()));
      }
      const auto rows = graphblas_closure(n, edges);
      Json out_rows = Json::Array{};
      for (const auto &row : rows) {
        Json r = Json::Array{};
        for (int v : row)
          r.push_back(v);
        out_rows.push_back(std::move(r));
      }
      resp["ok"] = true;
      resp["rows"] = std::move(out_rows);
    } else if (op == "gf2") {
      const int ncols = static_cast<int>(req.at("ncols").as_int());
      std::vector<std::vector<int>> rows;
      for (const auto &row : req.at("rows").array()) {
        std::vector<int> r;
        for (const auto &v : row.array())
          r.push_back(static_cast<int>(v.as_int()));
        rows.push_back(std::move(r));
      }
      const Gf2Result g = gf2_analysis(rows, ncols);
      Json ns = Json::Array{};
      for (const auto &v : g.nullspace) {
        Json one = Json::Array{};
        for (int x : v)
          one.push_back(x);
        ns.push_back(std::move(one));
      }
      resp["ok"] = true;
      resp["rank"] = g.rank;
      resp["nullity"] = g.nullity;
      resp["nullspace"] = std::move(ns);
    } else if (op == "ping") {
      resp["ok"] = true;
      resp["version"] = 1;
    } else {
      throw std::runtime_error("unknown op: " + op);
    }
    std::cout << resp.dump(0) << "\n";
    return 0;
  } catch (const std::exception &e) {
    Json err;
    err["ok"] = false;
    err["error"] = e.what();
    std::cout << err.dump(0) << "\n";
    return 1;
  } catch (...) {
    Json err;
    err["ok"] = false;
    err["error"] = "unknown exception";
    std::cout << err.dump(0) << "\n";
    return 1;
  }
}

int main() {
  try {
    return run();
  } catch (...) {
    return 1;
  }
}
