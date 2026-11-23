#include "core/partial.hpp"

#include "synthesis/static_exact.hpp"

namespace dcstudy {

bool MultiProblem::has_dont_cares() const {
  for (const auto &o : outputs)
    if (!o.any.none())
      return true;
  return false;
}

BoolFunc MultiProblem::completed(std::size_t k) const {
  return complete_with_cheapest_gate(outputs[k].func, outputs[k].any);
}

bool MultiProblem::satisfies(std::size_t k, const std::string &truth) const {
  return truth_satisfies(truth, outputs[k].func.truth_string(), outputs[k].any);
}

} // namespace dcstudy
