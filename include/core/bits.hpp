// Dynamic bitset used for truth tables, cube covers and Boolean-matrix rows.
// Bits are stored little-endian within 64-bit words; bit index 0 is the LSB.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace dcstudy {

// Storage is an array of uint64_t words: 64 bits, shift 6, mask 63.
inline constexpr std::size_t kWordBits = 64;
inline constexpr std::size_t kWordShift = 6;
inline constexpr std::size_t kWordMask = kWordBits - 1;
static_assert((std::size_t{1} << kWordShift) == kWordBits,
              "kWordShift must match kWordBits");

class Bits {
public:
  Bits() = default;
  explicit Bits(std::size_t n, bool fill = false)
      : n_(n), w_((n + kWordMask) / kWordBits, fill ? ~std::uint64_t{0} : 0) {
    trim();
  }

  static Bits zeros(std::size_t n) { return Bits(n, false); }
  static Bits ones(std::size_t n) { return Bits(n, true); }

  std::size_t size() const { return n_; }
  bool empty() const { return n_ == 0; }

  bool test(std::size_t i) const {
    return i < n_ && ((w_[i >> kWordShift] >> (i & kWordMask)) & 1U) != 0;
  }
  void set(std::size_t i) {
    if (i >= n_)
      grow(i + 1);
    w_[i >> kWordShift] |= (std::uint64_t{1} << (i & kWordMask));
  }
  void reset(std::size_t i) {
    if (i < n_)
      w_[i >> kWordShift] &= ~(std::uint64_t{1} << (i & kWordMask));
  }
  void flip(std::size_t i) {
    if (i >= n_)
      grow(i + 1);
    w_[i >> kWordShift] ^= (std::uint64_t{1} << (i & kWordMask));
  }

  std::size_t count() const {
    std::size_t c = 0;
    for (auto x : w_)
      c += static_cast<std::size_t>(__builtin_popcountll(x));
    return c;
  }
  bool any() const {
    for (auto x : w_)
      if (x)
        return true;
    return false;
  }
  bool none() const { return !any(); }

  Bits &operator&=(const Bits &o) {
    ensure_same(o);
    for (std::size_t i = 0; i < w_.size(); ++i)
      w_[i] &= o.w_[i];
    return *this;
  }
  Bits &operator|=(const Bits &o) {
    ensure_same(o);
    for (std::size_t i = 0; i < w_.size(); ++i)
      w_[i] |= o.w_[i];
    return *this;
  }
  Bits &operator^=(const Bits &o) {
    ensure_same(o);
    for (std::size_t i = 0; i < w_.size(); ++i)
      w_[i] ^= o.w_[i];
    return *this;
  }
  Bits operator~() const {
    Bits r(*this);
    for (auto &x : r.w_)
      x = ~x;
    r.trim();
    return r;
  }
  Bits operator&(const Bits &o) const { return Bits(*this) &= o; }
  Bits operator|(const Bits &o) const { return Bits(*this) |= o; }
  Bits operator^(const Bits &o) const { return Bits(*this) ^= o; }

  bool operator==(const Bits &o) const { return n_ == o.n_ && w_ == o.w_; }
  bool operator!=(const Bits &o) const { return !(*this == o); }

  bool intersects(const Bits &o) const {
    ensure_same(o);
    for (std::size_t i = 0; i < w_.size(); ++i)
      if (w_[i] & o.w_[i])
        return true;
    return false;
  }
  bool subset_of(const Bits &o) const {
    ensure_same(o);
    for (std::size_t i = 0; i < w_.size(); ++i)
      if (w_[i] & ~o.w_[i])
        return false;
    return true;
  }

  // Index of the first set bit, or size() when empty.
  std::size_t find_first() const {
    for (std::size_t i = 0; i < w_.size(); ++i) {
      if (w_[i])
        return i * kWordBits + static_cast<std::size_t>(__builtin_ctzll(w_[i]));
    }
    return n_;
  }

  std::uint64_t word(std::size_t i) const { return i < w_.size() ? w_[i] : 0; }
  std::size_t words() const { return w_.size(); }
  void set_word(std::size_t i, std::uint64_t v) {
    if (i >= w_.size())
      w_.resize(i + 1, 0);
    w_[i] = v;
    n_ = std::max(n_, (i + 1) * kWordBits);
    trim();
  }

  // "0101" with bit 0 first (truth-table reading order).
  std::string to_binary_string() const {
    std::string s;
    s.reserve(n_);
    for (std::size_t i = 0; i < n_; ++i)
      s.push_back(test(i) ? '1' : '0');
    return s;
  }

  std::uint64_t to_u64() const { return w_.empty() ? 0 : w_[0]; }

private:
  void trim() {
    if (w_.empty())
      return;
    const std::size_t valid = n_ % 64;
    if (valid != 0)
      w_.back() &= (std::uint64_t{1} << valid) - 1;
  }
  void grow(std::size_t n) {
    n_ = std::max(n_, n);
    w_.resize((n_ + kWordMask) / kWordBits, 0);
  }
  void ensure_same(const Bits &o) const {
    if (n_ != o.n_) {
      // Callers only combine same-width sets; a mismatch is a programming
      // error.
      throw std::invalid_argument("Bits width mismatch");
    }
  }

  std::size_t n_ = 0;
  std::vector<std::uint64_t> w_;
};

} // namespace dcstudy
