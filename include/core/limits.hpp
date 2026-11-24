// Process memory limit helpers (RLIMIT_AS).
//
// The cap is installed once, before any work starts, so every child process
// we spawn (yosys, dcstudy-z3, node) inherits it: one setrlimit covers the
// whole process tree.  Allocations past the cap fail instead of growing, and
// the CLI turns std::bad_alloc into a clean error instead of being OOM-killed
// after the machine has already started swapping.
#pragma once

#include <string>

namespace dcstudy {

// Parse a --mem-limit value in MiB.  Accepts a plain non-negative decimal
// integer only; throws std::invalid_argument otherwise.
unsigned long parse_mem_limit_mb(const std::string &text);

// Install mb MiB as the soft RLIMIT_AS.  0 means "the current hard limit"
// (normally unlimited).  Requests above the hard limit are clamped, never
// raised.  Returns the soft limit actually in force, in MiB (0 = unlimited).
// Never throws: on a platform that refuses the request the old limit stays.
unsigned long apply_mem_limit(unsigned long mb);

// Soft RLIMIT_AS currently in force, in MiB (0 = unlimited).
unsigned long current_mem_limit_mb();

// Headroom the default cap leaves for the rest of the system, in MiB.
inline constexpr unsigned long kMemReserveMb = 2048;

// Default timeout for out-of-process workers (render, z3).
inline constexpr int kDefaultWorkerTimeoutSeconds = 120;
inline constexpr int kYosysTimeoutSeconds = 180;
inline constexpr int kVerifyTimeoutSeconds = 240;

// Memory a new workload can still get without swapping, from MemAvailable,
// in MiB.  0 when it cannot be determined.
unsigned long available_memory_mb();

// Default cap for a machine with this much memory available: everything past
// a 2 GiB reserve, and nothing else -- the process may use the rest.  When the
// available memory is already at or below the reserve there is nothing to hand
// out, so the cap is 1 MiB and the command fails instead of eating it.
// 0 (unknown) means "no cap".
unsigned long mem_limit_from_available(unsigned long available_mb);

// default_mem_limit_mb() == mem_limit_from_available(available_memory_mb()).
unsigned long default_mem_limit_mb();

} // namespace dcstudy
