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
unsigned long parse_mem_limit_mb(const std::string& text);

// Install mb MiB as the soft RLIMIT_AS.  0 means "the current hard limit"
// (normally unlimited).  Requests above the hard limit are clamped, never
// raised.  Returns the soft limit actually in force, in MiB (0 = unlimited).
// Never throws: on a platform that refuses the request the old limit stays.
unsigned long apply_mem_limit(unsigned long mb);

// Soft RLIMIT_AS currently in force, in MiB (0 = unlimited).
unsigned long current_mem_limit_mb();

}  // namespace dcstudy
