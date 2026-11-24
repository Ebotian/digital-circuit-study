#pragma once
#include <string>
namespace dcstudy {
int cmd_bool(const std::string &sub, int argc, char **argv);
int cmd_cmos(const std::string &sub, int argc, char **argv);
int cmd_math(const std::string &sub, int argc, char **argv);
int cmd_cdl(const std::string &sub, int argc, char **argv);
int cmd_verify(const std::string &sub, int argc, char **argv);
} // namespace dcstudy
