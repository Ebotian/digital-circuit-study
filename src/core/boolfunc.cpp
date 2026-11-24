#include "core/boolfunc.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace dcstudy {
namespace {

constexpr std::size_t kMaxArity = 16;

struct Token {
  enum Kind { Ident, Const, And, Or, Xor, Not, LParen, RParen, End } kind;
  std::string text;
  bool value = false;
};

std::vector<Token> lex(const std::string &s) {
  std::vector<Token> out;
  std::size_t i = 0;
  auto push = [&](Token::Kind k, std::string t = {}, bool v = false) {
    Token tok;
    tok.kind = k;
    tok.text = std::move(t);
    tok.value = v;
    out.push_back(std::move(tok));
  };
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (std::isspace(c)) {
      ++i;
    } else if (c == '(') {
      push(Token::LParen);
      ++i;
    } else if (c == ')') {
      push(Token::RParen);
      ++i;
    } else if (c == '~' || c == '!') {
      push(Token::Not);
      ++i;
    } else if (c == '&') {
      ++i;
      if (i < s.size() && s[i] == '&')
        ++i;
      push(Token::And);
    } else if (c == '|') {
      ++i;
      if (i < s.size() && s[i] == '|')
        ++i;
      push(Token::Or);
    } else if (c == '^') {
      push(Token::Xor);
      ++i;
    } else if (std::isalpha(c) || c == '_') {
      std::size_t j = i;
      while (j < s.size() &&
             (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_'))
        ++j;
      std::string id = s.substr(i, j - i);
      if (id == "and") {
        push(Token::And);
      } else if (id == "or") {
        push(Token::Or);
      } else if (id == "xor") {
        push(Token::Xor);
      } else if (id == "not") {
        push(Token::Not);
      } else {
        push(Token::Ident, id);
      }
      i = j;
    } else if (c == '0' || c == '1') {
      push(Token::Const, std::string(1, static_cast<char>(c)), c == '1');
      ++i;
    } else {
      throw std::invalid_argument("unexpected character in expression: " +
                                  std::string(1, static_cast<char>(c)));
    }
  }
  push(Token::End);
  return out;
}

struct Parser {
  const std::vector<Token> &toks;
  const std::unordered_map<std::string, std::size_t> &index;
  std::size_t n;
  std::size_t pos = 0;

  const Token &peek() const { return toks[pos]; }
  const Token &take() { return toks[pos++]; }

  Bits ones() const { return Bits::ones(std::size_t{1} << n); }
  Bits zeros() const { return Bits::zeros(std::size_t{1} << n); }

  Bits parse_or() {
    Bits v = parse_xor();
    while (peek().kind == Token::Or) {
      take();
      v |= parse_xor();
    }
    return v;
  }
  Bits parse_xor() {
    Bits v = parse_and();
    while (peek().kind == Token::Xor) {
      take();
      v ^= parse_and();
    }
    return v;
  }
  Bits parse_and() {
    Bits v = parse_unary();
    while (peek().kind == Token::And) {
      take();
      v &= parse_unary();
    }
    return v;
  }
  Bits parse_unary() {
    if (peek().kind == Token::Not) {
      take();
      return ~parse_unary();
    }
    return parse_primary();
  }
  Bits parse_primary() {
    if (peek().kind == Token::LParen) {
      take();
      Bits v = parse_or();
      if (take().kind != Token::RParen)
        throw std::invalid_argument("missing ')' in expression");
      return v;
    }
    if (peek().kind == Token::Const)
      return take().value ? ones() : zeros();
    if (peek().kind == Token::Ident) {
      const std::string name = take().text;
      auto it = index.find(name);
      if (it == index.end())
        throw std::invalid_argument("unknown variable: " + name);
      std::size_t p = n - 1 - it->second;
      Bits v = zeros();
      for (std::uint32_t m = 0; m < (std::uint32_t{1} << n); ++m)
        if ((m >> p) & 1U)
          v.set(m);
      return v;
    }
    throw std::invalid_argument("unexpected token in expression");
  }
};

} // namespace

BoolFunc::BoolFunc(std::size_t n, Bits bits) : n_(n), bits_(std::move(bits)) {
  if (n_ > kMaxArity)
    throw std::invalid_argument("arithmetic limited to n <= 16");
  if (bits_.size() != (std::size_t{1} << n_))
    throw std::invalid_argument("truth table width does not match arity");
}

BoolFunc &BoolFunc::operator&=(const BoolFunc &o) {
  bits_ &= o.bits_;
  return *this;
}
BoolFunc &BoolFunc::operator|=(const BoolFunc &o) {
  bits_ |= o.bits_;
  return *this;
}
BoolFunc &BoolFunc::operator^=(const BoolFunc &o) {
  bits_ ^= o.bits_;
  return *this;
}
BoolFunc BoolFunc::operator&(const BoolFunc &o) const {
  return BoolFunc(*this) &= o;
}
BoolFunc BoolFunc::operator|(const BoolFunc &o) const {
  return BoolFunc(*this) |= o;
}
BoolFunc BoolFunc::operator^(const BoolFunc &o) const {
  return BoolFunc(*this) ^= o;
}
BoolFunc BoolFunc::operator~() const { return BoolFunc(n_, ~bits_); }
bool BoolFunc::operator==(const BoolFunc &o) const {
  return n_ == o.n_ && bits_ == o.bits_;
}

bool BoolFunc::is_monotone() const {
  const std::uint32_t N = static_cast<std::uint32_t>(num_minterms());
  for (std::uint32_t m = 0; m < N; ++m) {
    if (!value(m))
      continue;
    for (std::size_t p = 0; p < n_; ++p) {
      const std::uint32_t bit = std::uint32_t{1} << p;
      if ((m & bit) == 0 && !value(m | bit))
        return false;
    }
  }
  return true;
}

bool BoolFunc::is_anf_affine() const {
  const Bits a = anf();
  for (std::uint32_t m = 0; m < num_minterms(); ++m)
    if (__builtin_popcount(m) >= 2 && a.test(m))
      return false;
  return true;
}

bool BoolFunc::is_self_dual() const {
  const std::uint32_t N = static_cast<std::uint32_t>(num_minterms());
  const std::uint32_t mask = N - 1;
  for (std::uint32_t m = 0; m < N; ++m)
    if (value(m) == value((~m) & mask))
      return false;
  return true;
}

BoolFunc BoolFunc::dual() const {
  BoolFunc out = *this;
  for (std::uint32_t m = 0; m < num_minterms(); ++m) {
    bool v = !value((~m) & (static_cast<std::uint32_t>(num_minterms()) - 1));
    if (v)
      out.bits_.set(m);
    else
      out.bits_.reset(m);
  }
  return out;
}

Bits BoolFunc::anf() const {
  Bits out = bits_;
  for (std::size_t p = 0; p < n_; ++p) {
    const std::uint32_t bit = std::uint32_t{1} << p;
    for (std::uint32_t m = 0; m < num_minterms(); ++m)
      if ((m & bit) && out.test(m ^ bit))
        out.flip(m);
  }
  return out;
}

std::string BoolFunc::truth_string() const { return bits_.to_binary_string(); }

std::vector<std::uint32_t> BoolFunc::minterms() const {
  std::vector<std::uint32_t> out;
  for (std::uint32_t m = 0; m < num_minterms(); ++m)
    if (value(m))
      out.push_back(m);
  return out;
}

std::string BoolFunc::sop(const std::vector<std::string> &vars) const {
  const std::uint32_t N = static_cast<std::uint32_t>(num_minterms());
  const std::size_t cnt = bits_.count();
  if (cnt == 0)
    return "0";
  if (cnt == N)
    return "1";
  std::string out;
  for (std::uint32_t m = 0; m < N; ++m) {
    if (!value(m))
      continue;
    if (!out.empty())
      out += " | ";
    std::string term;
    for (std::size_t i = 0; i < n_; ++i) {
      const std::size_t p = n_ - 1 - i;
      const bool one = ((m >> p) & 1U) != 0;
      if (!term.empty())
        term += " & ";
      if (!one)
        term += '~';
      term += vars.at(i);
    }
    out += (n_ == 1 ? term : "(" + term + ")");
  }
  return out;
}

std::string BoolFunc::anf_string(const std::vector<std::string> &vars) const {
  Bits a = anf();
  std::string out;
  for (std::uint32_t m = 0; m < num_minterms(); ++m) {
    if (!a.test(m))
      continue;
    std::string term;
    for (std::size_t i = 0; i < n_; ++i) {
      if (!((m >> (n_ - 1 - i)) & 1U))
        continue;
      if (!term.empty())
        term += " & ";
      term += vars.at(i);
    }
    if (term.empty())
      term = "1";
    if (!out.empty())
      out += " ^ ";
    out += term;
  }
  return out.empty() ? "0" : out;
}

BoolFunc BoolFunc::constant(std::size_t n, bool v) {
  return BoolFunc(n, Bits(std::size_t{1} << n, v));
}

BoolFunc BoolFunc::variable(std::size_t n, std::size_t var_index) {
  const std::size_t p = n - 1 - var_index;
  Bits b(std::size_t{1} << n);
  for (std::uint32_t m = 0; m < (std::uint32_t{1} << n); ++m)
    if ((m >> p) & 1U)
      b.set(m);
  return BoolFunc(n, b);
}

FuncSpec spec_from_expr(const std::string &expr,
                        const std::vector<std::string> &vars) {
  auto toks = lex(expr);
  std::vector<std::string> collected;
  std::unordered_set<std::string> seen;
  for (const auto &t : toks) {
    if (t.kind == Token::Ident && seen.insert(t.text).second)
      collected.push_back(t.text);
  }
  std::vector<std::string> names = vars;
  if (names.empty()) {
    std::sort(collected.begin(), collected.end());
    names = collected;
  }
  if (names.empty())
    throw std::invalid_argument("expression has no variables");
  if (names.size() > kMaxArity)
    throw std::invalid_argument("too many variables (max 12)");
  std::unordered_map<std::string, std::size_t> index;
  for (std::size_t i = 0; i < names.size(); ++i)
    index[names[i]] = i;
  for (const auto &c : collected)
    if (!index.count(c))
      throw std::invalid_argument("variable not in --vars list: " + c);
  Parser p{toks, index, names.size()};
  Bits bits = p.parse_or();
  if (p.peek().kind != Token::End)
    throw std::invalid_argument("trailing tokens in expression");
  return FuncSpec{BoolFunc(names.size(), std::move(bits)), names, expr};
}

FuncSpec spec_from_truth(const std::string &text,
                         const std::vector<std::string> &vars) {
  std::string s = text;
  s.erase(std::remove_if(s.begin(), s.end(),
                         [](unsigned char c) { return std::isspace(c); }),
          s.end());
  if (s.empty())
    throw std::invalid_argument("empty truth table");
  // Check the width before computing 1 << n: a caller could pass a huge --vars
  // list, and the shift (and the Bits allocation) would be undefined.
  if (vars.size() > kMaxArity)
    throw std::invalid_argument("too many variables (max 12)");
  std::size_t n;
  Bits bits;
  if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) {
    if (vars.empty())
      throw std::invalid_argument("hex truth tables need --vars");
    n = vars.size();
    const std::size_t width = std::size_t{1} << n;
    bits = Bits(width);
    // Fill the table bit by bit instead of packing into a 64-bit integer:
    // hex tables can be wider than 64 rows and the shift would be undefined
    // (and silently wrap) past that.  A digit is four bits, least significant
    // digit last, and any bit outside the table is a width error.
    const std::size_t digits = s.size() - 2;
    for (std::size_t k = 0; k < digits; ++k) {
      const char c = s[2 + k];
      const int d =
          std::isdigit(static_cast<unsigned char>(c))
              ? c - '0'
              : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
      if (d < 0 || d > 15)
        throw std::invalid_argument("bad hex digit in truth table");
      const std::size_t base = 4 * (digits - 1 - k);
      for (int b = 0; b < 4; ++b) {
        if (!((d >> b) & 1))
          continue;
        const std::size_t pos = base + static_cast<std::size_t>(b);
        if (pos >= width)
          throw std::invalid_argument(
              "hex truth table does not fit the variables");
        bits.set(pos);
      }
    }
  } else {
    for (char c : s)
      if (c != '0' && c != '1')
        throw std::invalid_argument("truth table must contain only 0/1");
    const std::size_t len = s.size();
    if (len == 0 || (len & (len - 1)) != 0)
      throw std::invalid_argument("truth table length must be a power of two");
    n = 0;
    while ((std::size_t{1} << n) < len)
      ++n;
    if (!vars.empty() && vars.size() != n)
      throw std::invalid_argument("truth table length does not match --vars");
    bits = Bits(len);
    for (std::size_t i = 0; i < len; ++i)
      if (s[i] == '1')
        bits.set(i);
  }
  std::vector<std::string> names = vars;
  if (names.empty())
    for (std::size_t i = 0; i < n; ++i)
      names.push_back(default_var_name(i));
  if (names.size() != n)
    throw std::invalid_argument("variable count mismatch");
  return FuncSpec{BoolFunc(n, std::move(bits)), names, "tt=" + text};
}

std::string default_var_name(std::size_t i) {
  if (i < 26)
    return std::string(1, static_cast<char>('A' + i));
  return "V" + std::to_string(i);
}

std::uint64_t count_monotone(std::size_t n) {
  if (n == 0)
    return 1;
  if (n > 6)
    throw std::invalid_argument("exact Dedekind count supported for n <= 6");
  const std::uint32_t N = std::uint32_t{1} << n; // number of lattice elements
  std::vector<std::uint64_t> down(N), up(N);
  for (std::uint32_t x = 0; x < N; ++x) {
    for (std::uint32_t y = 0; y < N; ++y) {
      if ((y & ~x) == 0)
        down[x] |= (std::uint64_t{1} << y);
      if ((x & ~y) == 0)
        up[x] |= (std::uint64_t{1} << y);
    }
  }
  std::vector<std::uint32_t> order(N);
  for (std::uint32_t i = 0; i < N; ++i)
    order[i] = i;
  std::sort(order.begin(), order.end(), [](std::uint32_t a, std::uint32_t b) {
    const auto pa = __builtin_popcount(a), pb = __builtin_popcount(b);
    return pa != pb ? pa < pb : a < b;
  });

  struct Key {
    std::uint32_t i;
    std::uint64_t f0, f1;
    bool operator==(const Key &o) const {
      return i == o.i && f0 == o.f0 && f1 == o.f1;
    }
  };
  struct Hash {
    std::size_t operator()(const Key &k) const {
      std::uint64_t h = k.f0 * 0x9e3779b97f4a7c15ULL;
      h ^= k.f1 + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      h ^= k.i + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
      return static_cast<std::size_t>(h);
    }
  };
  std::unordered_map<Key, std::uint64_t, Hash> memo;
  memo.reserve(1 << 20);

  auto dfs = [&](auto &&self, std::uint32_t i, std::uint64_t f0,
                 std::uint64_t f1) -> std::uint64_t {
    if (f0 & f1)
      return 0;
    if (i == N)
      return 1;
    Key key{i, f0, f1};
    auto it = memo.find(key);
    if (it != memo.end())
      return it->second;
    const std::uint64_t bit = std::uint64_t{1} << order[i];
    std::uint64_t total = 0;
    if ((f1 & bit) == 0)
      total += self(self, i + 1, f0 | down[order[i]], f1);
    if ((f0 & bit) == 0)
      total += self(self, i + 1, f0, f1 | up[order[i]]);
    memo.emplace(key, total);
    return total;
  };
  return dfs(dfs, 0, 0, 0);
}

} // namespace dcstudy
