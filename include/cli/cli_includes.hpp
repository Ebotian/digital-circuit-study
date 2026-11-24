// Common includes for the CLI translation units.  Keeps the per-command files
// focused on their dispatch code instead of repeating the same 30 headers.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cli/math_client.hpp"
#include "cli/render_client.hpp"
#include "core/boolfunc.hpp"
#include "core/cdl.hpp"
#include "core/cmos.hpp"
#include "core/designspace.hpp"
#include "core/driver_profile.hpp"
#include "core/json.hpp"
#include "core/limits.hpp"
#include "core/partial.hpp"
#include "core/verify.hpp"
#include "core/verilog.hpp"
#include "switch/bdd_switch.hpp"
#include "switch/switchlevel.hpp"
#include "synthesis/compose.hpp"
#include "synthesis/multi_path.hpp"
#include "synthesis/multistage.hpp"
#include "synthesis/path_search.hpp"
#include "synthesis/static_exact.hpp"
#include "synthesis/synthesis.hpp"
#include "synthesis/template_synth.hpp"
