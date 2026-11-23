#pragma once
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dcstudy/bits.hpp"
#include "dcstudy/boolfunc.hpp"
#include "dcstudy/boolmatrix.hpp"
#include "dcstudy/cdl.hpp"
#include "dcstudy/cmos.hpp"
#include "dcstudy/designspace.hpp"
#include "dcstudy/driver_profile.hpp"
#include "dcstudy/limits.hpp"
#include "dcstudy/parallel.hpp"
#include "dcstudy/static_exact.hpp"
#include "dcstudy/switchlevel.hpp"
#include "dcstudy/synthesis.hpp"
#include "dcstudy/verilog.hpp"
#include "dcstudy/z3_protocol.hpp"

using namespace dcstudy;

inline std::vector<int> target_of(const FuncSpec& s) {
  std::vector<int> t;
  for (std::uint32_t m = 0; m < s.func.num_minterms(); ++m)
    t.push_back(s.func.value(m) ? 1 : 0);
  return t;
}

