#pragma once
#include "core/cmos.hpp"
#include <string>
namespace dcstudy {
int cmd_design(const std::string &sub, int argc, char **argv);
int static_gate_fanin(const CmosDesign &d);
} // namespace dcstudy
