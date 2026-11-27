// Sequential (FSM) commands: analysis, register pricing, product-machine check.
#pragma once

#include <string>

namespace dcstudy {
int cmd_seq(const std::string &sub, int argc, char **argv);
}
