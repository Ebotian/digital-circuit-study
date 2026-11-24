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
// Same inputs, but draws through the custom gate-box renderer (scheme-0.*)
// instead of Yosys/Graphviz.
bool render_netlist_native(const std::vector<std::string> &files,
                           const std::string &top, const std::string &out,
                           bool png, std::string &err, int cost = 0,
                           const std::string &title = {});
} // namespace dcstudy
