#include "core/verilog.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dcstudy {
namespace {

std::string lit_expr(const std::string &lit) {
  if (!lit.empty() && lit.back() == 'b')
    return "~" + lit.substr(0, lit.size() - 1);
  return lit;
}

std::string negate_expr(const std::string &e) {
  if (!e.empty() && e[0] == '~')
    return e.substr(1);
  return "~" + e;
}

std::string join(const std::vector<std::string> &v, const std::string &sep) {
  std::string out;
  for (const auto &s : v) {
    if (!out.empty())
      out += sep;
    out += s;
  }
  return out;
}

std::string ports(const std::vector<std::string> &vars, bool with_ok) {
  std::string out;
  for (const auto &v : vars)
    out += "input logic " + v + ", ";
  out += "output logic Y";
  if (with_ok)
    out += ", output logic ok";
  return out;
}

} // namespace

std::string emit_dut(const Design &design,
                     const std::vector<std::string> &vars) {
  if (design.branches.empty())
    throw std::invalid_argument("empty design");
  std::ostringstream os;
  os << "module dut (" << ports(vars, true) << ");\n";
  for (std::size_t i = 0; i < design.branches.size(); ++i) {
    const auto &b = design.branches[i];
    const std::string n = std::to_string(i + 1);
    const std::string src = (b.source == "0")   ? "1'b0"
                            : (b.source == "1") ? "1'b1"
                                                : lit_expr(b.source);
    os << "  wire [0:0] v" << n << " = " << src << ";\n";
    std::vector<std::string> ens;
    std::vector<std::string> sts;
    for (const auto &el : b.chain) {
      const std::string g = lit_expr(el.lit);
      switch (el.kind) {
      case SwKind::N:
        ens.push_back(g);
        sts.push_back("(v" + n + " == 1'b0)");
        break;
      case SwKind::P:
        ens.push_back(negate_expr(g));
        sts.push_back("(v" + n + " == 1'b1)");
        break;
      case SwKind::TGL:
        ens.push_back(negate_expr(g));
        sts.push_back("1'b1");
        break;
      case SwKind::TGH:
        ens.push_back(g);
        sts.push_back("1'b1");
        break;
      }
    }
    os << "  wire en" << n << " = " << (ens.empty() ? "1'b1" : join(ens, " & "))
       << ";\n";
    os << "  wire st" << n << " = " << (sts.empty() ? "1'b1" : join(sts, " & "))
       << ";\n";
  }
  const std::size_t nB = design.branches.size();
  std::vector<std::string> ens, sts, strong;
  for (std::size_t i = 1; i <= nB; ++i) {
    ens.push_back("en" + std::to_string(i));
    sts.push_back("st" + std::to_string(i));
    strong.push_back("(en" + std::to_string(i) + " & st" + std::to_string(i) +
                     ")");
  }
  os << "  wire any_en = " << join(ens, " | ") << ";\n";
  os << "  wire any_st = " << join(strong, " | ") << ";\n";
  os << "  logic agree;\n  always_comb begin\n    agree = 1'b1;\n";
  bool any_pair = false;
  for (std::size_t i = 1; i <= nB; ++i) {
    for (std::size_t j = i + 1; j <= nB; ++j) {
      any_pair = true;
      os << "    if (en" << i << " & st" << i << " & en" << j << " & st" << j
         << " && (v" << i << " !== v" << j << ")) agree = 1'b0;\n";
    }
  }
  if (!any_pair)
    os << "    // single branch: no cross-branch agreement check\n";
  os << "  end\n";
  os << "  assign ok = any_en & any_st & agree;\n";
  os << "  always_comb begin\n    if (1'b0) Y = 1'b0;\n";
  for (std::size_t i = 1; i <= nB; ++i)
    os << "    else if (en" << i << " & st" << i << ") Y = v" << i << ";\n";
  os << "    else Y = 1'b0;\n  end\nendmodule\n";
  return os.str();
}

std::string emit_ref(const BoolFunc &target,
                     const std::vector<std::string> &vars) {
  std::ostringstream os;
  os << "module ref (" << ports(vars, false) << ");\n";
  os << "  assign Y = " << target.sop(vars) << ";\n";
  os << "endmodule\n";
  return os.str();
}

std::string emit_miter(const std::vector<std::string> &vars) {
  std::ostringstream os;
  os << "module miter (";
  for (const auto &v : vars)
    os << "input logic " << v << ", ";
  os << "output logic mismatch);\n";
  os << "  wire y_ref, y_dut, ok;\n";
  os << "  ref u_ref(";
  for (const auto &v : vars)
    os << "." << v << "(" << v << "), ";
  os << ".Y(y_ref));\n";
  os << "  dut u_dut(";
  for (const auto &v : vars)
    os << "." << v << "(" << v << "), ";
  os << ".Y(y_dut), .ok(ok));\n";
  os << "  assign mismatch = (y_dut !== y_ref) | ~ok;\n";
  os << "endmodule\n";
  return os.str();
}

std::string emit_tb(const BoolFunc &target,
                    const std::vector<std::string> &vars) {
  const std::size_t n = vars.size();
  const std::size_t rows = std::size_t{1} << n;
  if (rows > 4096)
    throw std::invalid_argument("testbench too large");
  const std::string truth = target.truth_string();
  std::string exp_literal;
  for (std::size_t i = rows; i-- > 0;)
    exp_literal.push_back(truth[i]);

  std::ostringstream os;
  os << "module tb;\n";
  for (const auto &v : vars)
    os << "  logic " << v << ";\n";
  os << "  logic Y, ok;\n";
  os << "  dut u(";
  for (const auto &v : vars)
    os << "." << v << "(" << v << "), ";
  os << ".Y(Y), .ok(ok));\n";
  os << "  int f = 0;\n";
  os << "  logic [" << rows - 1 << ":0] EXP = " << rows << "'b" << exp_literal
     << ";\n";
  os << "  initial begin\n";
  os << "    for (int i = 0; i < " << rows << "; i++) begin\n";
  for (std::size_t j = 0; j < n; ++j) {
    const std::size_t shift = n - 1 - j;
    os << "      " << vars[j] << " = (i >> " << shift << ") & 1;\n";
  }
  os << "      #1ns;\n";
  os << "      if (!ok || (Y !== EXP[i])) f++;\n";
  os << "    end\n";
  os << "    if (f == 0) $display(\"PASS\");\n";
  os << "    else $display(\"FAIL %0d/" << rows << "\", f);\n";
  os << "    $finish;\n  end\nendmodule\n";
  return os.str();
}

} // namespace dcstudy
