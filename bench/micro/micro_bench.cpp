#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

#include "core/boolfunc.hpp"

using namespace dcstudy;

namespace {
template <typename F>
double ns_per_op(const char* name, int iters, F&& f) {
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < iters; ++i) f(i);
  const auto t1 = std::chrono::steady_clock::now();
  const double ns =
      std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
  std::cout << name << ": " << ns << " ns/op\n";
  return ns;
}
}  // namespace

int main() {
  volatile std::uint32_t sink = 0;
  ns_per_op("spec_from_expr(A^B|C&D)", 20000, [&](int) {
    const FuncSpec s = spec_from_expr("A^B|C&D");
    sink ^= s.func.value(5) ? 1u : 0u;
  });
  ns_per_op("spec_from_truth(0110)", 20000, [&](int) {
    const FuncSpec s = spec_from_truth("0110");
    sink ^= s.func.value(1) ? 1u : 0u;
  });
  const FuncSpec a = spec_from_expr("A&B");
  ns_per_op("BoolFunc and/or/xor", 200000, [&](int) {
    const BoolFunc r = (a.func & (~a.func)) | (a.func ^ a.func);
    sink ^= r.value(0) ? 1u : 0u;
  });
  std::cout << "sink=" << sink << "\n";
  return 0;
}
