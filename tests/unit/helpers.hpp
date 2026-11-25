#pragma once
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/bits.hpp"
#include "core/boolfunc.hpp"
#include "core/boolmatrix.hpp"
#include "core/cdl.hpp"
#include "core/cmos.hpp"
#include "core/designspace.hpp"
#include "core/driver_profile.hpp"
#include "core/limits.hpp"
#include "core/parallel.hpp"
#include "core/verilog.hpp"
#include "switch/static_cells.hpp"
#include "switch/switchlevel.hpp"
#include "synthesis/static_exact.hpp"
#include "synthesis/synthesis.hpp"
#include "z3/z3_protocol.hpp"

using namespace dcstudy;

inline std::vector<int> target_of(const FuncSpec &s) {
  std::vector<int> t;
  t.reserve(s.func.num_minterms());
  for (std::uint32_t m = 0; m < s.func.num_minterms(); ++m)
    t.push_back(s.func.value(m) ? 1 : 0);
  return t;
}

inline bool z3_helper_available() {
  if (const char *p = std::getenv("DCSTUDY_Z3_HELPER"); p && *p)
    return std::filesystem::exists(p);
  return false;
}

#define REQUIRE_Z3_HELPER()                                                    \
  do {                                                                         \
    if (!z3_helper_available())                                                \
      SKIP("dcstudy-z3 not built");                                            \
  } while (0)
