// Client for the Python schematic-rendering worker.
#pragma once

#include "core/limits.hpp"

#include <string>

#include "core/json.hpp"

namespace dcstudy {

std::string render_helper_path();
Json render_call(const Json &request, int timeout_seconds = kDefaultWorkerTimeoutSeconds);

} // namespace dcstudy
