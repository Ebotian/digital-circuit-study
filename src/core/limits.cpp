#include "core/limits.hpp"

#include <sys/resource.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
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

unsigned long available_memory_mb() {
  // MemAvailable is the kernel's own estimate of what a new workload can get
  // without swapping; it already counts reclaimable page cache.
  std::ifstream in("/proc/meminfo");
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream is(line);
    std::string key;
    unsigned long kb = 0;
    is >> key >> kb;
    if (key == "MemAvailable:") return kb / 1024;  // kB -> MiB
  }
  // Fallback for kernels without MemAvailable: count free pages only.
  const long pages = sysconf(_SC_AVPHYS_PAGES);
  const long page_size = sysconf(_SC_PAGESIZE);
  if (pages > 0 && page_size > 0)
    return static_cast<unsigned long>(pages) *
           static_cast<unsigned long>(page_size) / kBytesPerMb;
  return 0;
}

unsigned long mem_limit_from_available(unsigned long available_mb) {
  if (available_mb == 0) return 0;  // cannot see the machine: no cap
  if (available_mb <= kMemReserveMb) return 1;  // nothing past the reserve
  return available_mb - kMemReserveMb;
}

unsigned long default_mem_limit_mb() {
  return mem_limit_from_available(available_memory_mb());
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
