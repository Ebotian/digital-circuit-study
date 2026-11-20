// Client side of the out-of-process Z3 worker.
#pragma once

#include <string>

#include "dcstudy/json.hpp"

namespace dcstudy {

// Resolve the dcstudy-z3 binary from $DCSTUDY_Z3_HELPER, then next to this
// executable.  Returns an empty string when no helper can be found.
std::string z3_helper_path();

// Send one JSON request and parse the JSON response.  Throws on transport,
// timeout, helper error, or an {"ok":false} response.
Json z3_call(const Json& request, int timeout_seconds = 120);

}  // namespace dcstudy
