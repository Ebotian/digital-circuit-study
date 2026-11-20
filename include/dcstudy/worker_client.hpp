// Generic out-of-process worker locator and JSON exchange.
//
// Workers receive one JSON document on stdin and write one JSON document on
// stdout.  A worker whose path ends in .py is launched with python3.
#pragma once

#include <string>

#include "dcstudy/json.hpp"

namespace dcstudy {

struct WorkerLocator {
  std::string env_var;
  std::string filename;
};

// Search $<env_var>, the directory of this executable, ../workers and
// ./workers relative to the executable, the current directory, then PATH.
// Returns an empty string if nothing is found.
std::string find_worker(const WorkerLocator& locator);

// Run the worker with a timeout and return its parsed JSON response.  Throws
// on transport errors, timeouts, non-zero exit, or {"ok": false}.
Json call_worker(const std::string& helper, const Json& request,
                 int timeout_seconds = 120);

}  // namespace dcstudy
