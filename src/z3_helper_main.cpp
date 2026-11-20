// Out-of-process Z3 worker.  Reads one JSON request on stdin and writes one
// JSON response on stdout.  The safe CLI never links libz3.
#include <exception>
#include <iostream>
#include <iterator>
#include <string>

#include "dcstudy/json.hpp"
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
