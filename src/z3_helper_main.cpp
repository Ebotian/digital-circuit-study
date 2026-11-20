// Out-of-process Z3 worker.  Reads one JSON request on stdin and writes one
// JSON response on stdout.  The safe CLI never links libz3.
#include <exception>
#include <iostream>
#include <iterator>
#include <string>

#include "dcstudy/json.hpp"
#include "dcstudy/symmetry.hpp"
#include "dcstudy/z3_protocol.hpp"
#include "dcstudy/z3_solvers.hpp"

using namespace dcstudy;

int main() {
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
      resp = encode_switch_solution(z3_solve_switch(decode_switch_problem(req)));
    } else if (op == "symmetry") {
      const int n = static_cast<int>(req.at("n").as_int());
      const std::string truth = req.at("truth").as_string();
      const auto perms = bliss_automorphisms(static_cast<std::size_t>(n), truth);
      Json arr = Json::Array{};
      for (const auto& p : perms) {
        Json one = Json::Array{};
        for (auto v : p) one.push_back(static_cast<std::int64_t>(v));
        arr.push_back(std::move(one));
      }
      resp["ok"] = true;
      resp["permutations"] = std::move(arr);
    } else if (op == "ping") {
      resp["ok"] = true;
      resp["version"] = 1;
    } else {
      throw std::runtime_error("unknown op: " + op);
    }
    std::cout << resp.dump(0) << "\n";
    return 0;
  } catch (const std::exception& e) {
    Json err;
    err["ok"] = false;
    err["error"] = e.what();
    std::cout << err.dump(0) << "\n";
    return 1;
  }
}
