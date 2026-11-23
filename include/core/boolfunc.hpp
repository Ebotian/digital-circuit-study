// Boolean functions as truth-table bitsets.  Minterm m encodes the inputs in
// big-endian order: the first variable name is the most significant bit, so
// "01" means A=0, B=1.  All operations are on the Boolean semiring {0,1}.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bits.hpp"

namespace dcstudy {

class BoolFunc {
 public:
  BoolFunc() = default;
  BoolFunc(std::size_t n, Bits bits);

  std::size_t arity() const { return n_; }
  const Bits& bits() const { return bits_; }
  std::size_t num_minterms() const { return std::size_t{1} << n_; }

  bool value(std::uint32_t minterm) const { return bits_.test(minterm); }

  BoolFunc& operator&=(const BoolFunc& o);
  BoolFunc& operator|=(const BoolFunc& o);
  BoolFunc& operator^=(const BoolFunc& o);
  BoolFunc operator&(const BoolFunc& o) const;
  BoolFunc operator|(const BoolFunc& o) const;
  BoolFunc operator^(const BoolFunc& o) const;
  BoolFunc operator~() const;
  bool operator==(const BoolFunc& o) const;

  // Monotone increasing: x <= y (bitwise) implies f(x) <= f(y).
  bool is_monotone() const;
  // ANF-affine (XOR/XNOR are affine). This is NOT the kappa-linearity
  // criterion used for static complementary CMOS.
  bool is_anf_affine() const;
  bool is_self_dual() const;
  BoolFunc dual() const;  // f*(x) = !f(!x)

  // Algebraic normal form: bit m of the result is the coefficient of the
  // monomial prod_{i in m} x_i (fast Moebius transform).
  Bits anf() const;

  std::string truth_string() const;
  std::vector<std::uint32_t> minterms() const;
  std::string sop(const std::vector<std::string>& vars) const;
  std::string anf_string(const std::vector<std::string>& vars) const;

  static BoolFunc constant(std::size_t n, bool v);
  static BoolFunc variable(std::size_t n, std::size_t var_index);

 private:
  std::size_t n_ = 0;
  Bits bits_;
};

struct FuncSpec {
  BoolFunc func;
  std::vector<std::string> vars;
  std::string description;
};

// Parse a Python-like Boolean expression: ~ ! & | ^ and or not, 0/1.
// Variable order is either explicit or the sorted unique identifiers.
FuncSpec spec_from_expr(const std::string& expr,
                        const std::vector<std::string>& vars = {});

// Parse a truth table string "0110" (bit 0 first) or a hexadecimal "0x6".
FuncSpec spec_from_truth(const std::string& text,
                         const std::vector<std::string>& vars = {});

// Count monotone Boolean functions of n variables (Dedekind number).
// Exact for n <= 6; uses antichain constraints, not a scan of all 2^(2^n)
// functions.
std::uint64_t count_monotone(std::size_t n);

std::string default_var_name(std::size_t i);

}  // namespace dcstudy
