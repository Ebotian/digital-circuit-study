#include <cstddef>
#include <cstdint>
#include <string>

#include "core/boolfunc.hpp"

// libFuzzer for the Boolean expression / truth-table parser.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size > 4096) return 0;
  const std::string text(reinterpret_cast<const char*>(data), size);
  try {
    const dcstudy::FuncSpec s = dcstudy::spec_from_expr(text);
    (void)s;
  } catch (...) {
  }
  try {
    const dcstudy::FuncSpec t = dcstudy::spec_from_truth(text);
    (void)t;
  } catch (...) {
  }
  return 0;
}
