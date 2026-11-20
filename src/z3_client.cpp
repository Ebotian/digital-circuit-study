#include "dcstudy/z3_client.hpp"

#include <stdexcept>

#include "dcstudy/worker_client.hpp"

namespace dcstudy {

std::string z3_helper_path() {
  return find_worker(WorkerLocator{"DCSTUDY_Z3_HELPER", "dcstudy-z3"});
}

Json z3_call(const Json& request, int timeout_seconds) {
  const std::string helper = z3_helper_path();
  if (helper.empty())
    throw std::runtime_error(
        "Z3 helper not found; build the dcstudy-z3 target or set "
        "DCSTUDY_Z3_HELPER");
  return call_worker(helper, request, timeout_seconds);
}

}  // namespace dcstudy
