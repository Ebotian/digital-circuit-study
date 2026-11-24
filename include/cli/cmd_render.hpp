#pragma once
#include <string>
#include <vector>
namespace dcstudy {
int cmd_render(int argc, char **argv);
// Render a Verilog netlist through Yosys' graphviz export and Graphviz:
// writes <out>/<top>.dot/.svg (+ .png when png=true) and index.html.
bool render_netlist_files(const std::vector<std::string> &files,
                          const std::string &top, const std::string &out,
                          bool png, std::string &err);
} // namespace dcstudy
