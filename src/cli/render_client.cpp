#include "cli/render_client.hpp"

#include <stdexcept>

#include "cli/worker_client.hpp"

namespace dcstudy {

std::string render_helper_path() {
  std::string ts =
      find_worker(WorkerLocator{"DCSTUDY_RENDER_HELPER", "render_worker.ts"});
  if (!ts.empty())
    return ts;
  return find_worker(WorkerLocator{"", "render_worker.py"});
}

Json render_call(const Json &request, int timeout_seconds) {
  const std::string helper = render_helper_path();
  if (helper.empty())
    throw std::runtime_error("render worker not found; build the tree or set "
                             "DCSTUDY_RENDER_HELPER=workers/render_worker.py");
  return call_worker(helper, request, timeout_seconds);
}

} // namespace dcstudy
