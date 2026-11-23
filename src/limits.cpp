#include "dcstudy/limits.hpp"

#include <sys/resource.h>

#include <stdexcept>

namespace dcstudy {
namespace {

constexpr unsigned long kBytesPerMb = 1024UL * 1024UL;
// 1 PiB: far past any real machine, but small enough that mb * kBytesPerMb
// cannot overflow a 64-bit rlim_t.
constexpr unsigned long kMaxMb = 1024UL * 1024UL;

}  // namespace

unsigned long parse_mem_limit_mb(const std::string& text) {
  if (text.empty())
    throw std::invalid_argument("--mem-limit expects MiB as an integer");
  unsigned long mb = 0;
  for (char c : text) {
    if (c < '0' || c > '9')
      throw std::invalid_argument("--mem-limit expects MiB as an integer, got: " +
                                  text);
    const auto digit = static_cast<unsigned long>(c - '0');
    if (mb > (kMaxMb - digit) / 10)
      throw std::invalid_argument("--mem-limit out of range: " + text);
    mb = mb * 10 + digit;
  }
  return mb;
}

unsigned long current_mem_limit_mb() {
  struct rlimit rl {};
  if (getrlimit(RLIMIT_AS, &rl) != 0) return 0;
  if (rl.rlim_cur == RLIM_INFINITY) return 0;
  return static_cast<unsigned long>(rl.rlim_cur / kBytesPerMb);
}

unsigned long apply_mem_limit(unsigned long mb) {
  struct rlimit rl {};
  if (getrlimit(RLIMIT_AS, &rl) != 0) return current_mem_limit_mb();
  rlim_t want = rl.rlim_max;  // 0 = the hard limit, normally unlimited
  if (mb != 0) {
    want = static_cast<rlim_t>(mb) * static_cast<rlim_t>(kBytesPerMb);
    if (rl.rlim_max != RLIM_INFINITY && want > rl.rlim_max) want = rl.rlim_max;
  }
  rl.rlim_cur = want;
  if (setrlimit(RLIMIT_AS, &rl) != 0) return current_mem_limit_mb();
  return current_mem_limit_mb();
}

}  // namespace dcstudy
