// Tri-state one-hot decoder: a TG demux tree routes VSS to exactly one output.
// Unselected outputs are high-Z, so this is not a static complementary circuit.
#pragma once

#include <string>
#include <vector>

#include "dcstudy/driver_profile.hpp"

namespace dcstudy {

struct TriDecoderResult {
  bool ok = false;
  bool verified = false;
  std::string reason;
  int total = 0;
  int tg_count = 0;
  int inverter_cost = 0;
  std::vector<std::string> outputs;
  SwitchGraph graph;
};

TriDecoderResult synthesize_tri_decoder(int bits,
                                        const std::vector<std::string>& vars);

}  // namespace dcstudy
