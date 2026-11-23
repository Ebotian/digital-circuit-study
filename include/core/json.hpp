// Minimal ordered JSON value: writer plus a small parser used by the Z3 helper
// protocol.  Objects keep deterministic key order.
#pragma once

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace dcstudy {

class Json {
 public:
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

  Json() : value_(nullptr) {}
  Json(std::nullptr_t) : value_(nullptr) {}
  Json(bool v) : value_(v) {}
  Json(int v) : value_(static_cast<std::int64_t>(v)) {}
  Json(std::int64_t v) : value_(v) {}
  Json(std::size_t v) : value_(static_cast<std::int64_t>(v)) {}
  Json(double v) : value_(v) {}
  Json(const char* v) : value_(std::string(v)) {}
  Json(std::string v) : value_(std::move(v)) {}
  Json(Array v) : value_(std::move(v)) {}
  Json(Object v) : value_(std::move(v)) {}

  bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }
  bool is_bool() const { return std::holds_alternative<bool>(value_); }
  bool is_number() const {
    return std::holds_alternative<std::int64_t>(value_) ||
           std::holds_alternative<double>(value_);
  }
  bool is_string() const { return std::holds_alternative<std::string>(value_); }
  bool is_object() const { return std::holds_alternative<Object>(value_); }
  bool is_array() const { return std::holds_alternative<Array>(value_); }

  Json& operator[](const std::string& key) {
    if (!is_object()) value_ = Object{};
    return std::get<Object>(value_)[key];
  }
  const Json& at(const std::string& key) const { return std::get<Object>(value_).at(key); }
  bool contains(const std::string& key) const {
    auto p = std::get_if<Object>(&value_);
    return p && p->count(key) != 0;
  }
  const Array& array() const { return std::get<Array>(value_); }
  const Object& object() const { return std::get<Object>(value_); }
  const std::string& as_string() const { return std::get<std::string>(value_); }
  bool as_bool() const { return std::get<bool>(value_); }
  std::int64_t as_int() const {
    if (auto p = std::get_if<std::int64_t>(&value_)) return *p;
    if (auto p = std::get_if<double>(&value_)) return static_cast<std::int64_t>(*p);
    throw std::runtime_error("JSON value is not an integer");
  }
  double as_double() const {
    if (auto p = std::get_if<double>(&value_)) return *p;
    if (auto p = std::get_if<std::int64_t>(&value_)) return static_cast<double>(*p);
    throw std::runtime_error("JSON value is not a number");
  }

  Json& push_back(Json v) {
    if (!is_array()) value_ = Array{};
    std::get<Array>(value_).push_back(std::move(v));
    return *this;
  }
  std::size_t size() const {
    if (is_array()) return std::get<Array>(value_).size();
    if (is_object()) return std::get<Object>(value_).size();
    return 0;
  }

  std::string dump(int indent = 2) const {
    std::ostringstream os;
    write(os, indent, 0);
    return os.str();
  }

  static Json parse(const std::string& text);

 private:
  static void escape(std::ostringstream& os, const std::string& s) {
    os << '"';
    for (unsigned char c : s) {
      switch (c) {
        case '"': os << "\\\""; break;
        case '\\': os << "\\\\"; break;
        case '\b': os << "\\b"; break;
        case '\f': os << "\\f"; break;
        case '\n': os << "\\n"; break;
        case '\r': os << "\\r"; break;
        case '\t': os << "\\t"; break;
        default:
          if (c < 0x20) {
            os << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<int>(c) << std::dec;
          } else {
            os << static_cast<char>(c);
          }
      }
    }
    os << '"';
  }

  void write(std::ostringstream& os, int indent, int level) const {
    const std::string pad(static_cast<std::size_t>(indent * level), ' ');
    const std::string pad2(static_cast<std::size_t>(indent * (level + 1)), ' ');
    if (std::holds_alternative<std::nullptr_t>(value_)) {
      os << "null";
    } else if (auto p = std::get_if<bool>(&value_)) {
      os << (*p ? "true" : "false");
    } else if (auto p = std::get_if<std::int64_t>(&value_)) {
      os << *p;
    } else if (auto p = std::get_if<double>(&value_)) {
      if (*p == static_cast<double>(static_cast<std::int64_t>(*p)) &&
          std::abs(*p) < 1e15) {
        os << static_cast<std::int64_t>(*p);
      } else {
        std::ostringstream tmp;
        tmp << std::setprecision(17) << *p;
        os << tmp.str();
      }
    } else if (auto p = std::get_if<std::string>(&value_)) {
      escape(os, *p);
    } else if (auto p = std::get_if<Array>(&value_)) {
      if (p->empty()) {
        os << "[]";
        return;
      }
      os << "[\n";
      for (std::size_t i = 0; i < p->size(); ++i) {
        os << pad2;
        (*p)[i].write(os, indent, level + 1);
        os << (i + 1 == p->size() ? "\n" : ",\n");
      }
      os << pad << "]";
    } else if (auto p = std::get_if<Object>(&value_)) {
      if (p->empty()) {
        os << "{}";
        return;
      }
      os << "{\n";
      std::size_t i = 0;
      for (const auto& [k, v] : *p) {
        os << pad2;
        escape(os, k);
        os << (indent > 0 ? ": " : ":");
        v.write(os, indent, level + 1);
        os << (++i == p->size() ? "\n" : ",\n");
      }
      os << pad << "}";
    }
  }

  std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array,
               Object>
      value_;
};

namespace json_detail {

class Parser {
 public:
  explicit Parser(const std::string& text) : s_(text) {}

  Json parse() {
    skip();
    Json v = value();
    skip();
    if (i_ != s_.size()) fail("trailing characters");
    return v;
  }

 private:
  const std::string& s_;
  std::size_t i_ = 0;

  [[noreturn]] void fail(const std::string& why) const {
    throw std::runtime_error("JSON parse error at byte " + std::to_string(i_) +
                             ": " + why);
  }
  void skip() {
    while (i_ < s_.size() &&
           std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
  }
  char peek() const {
    if (i_ >= s_.size()) fail("unexpected end");
    return s_[i_];
  }
  char take() {
    char c = peek();
    ++i_;
    return c;
  }
  void expect(char c) {
    if (take() != c) fail(std::string("expected '") + c + "'");
  }
  static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  void utf8(std::uint32_t cp, std::string& out) {
    if (cp <= 0x7f) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
      out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
      out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
  }
  std::uint32_t unicode_escape() {
    std::uint32_t cp = 0;
    for (int k = 0; k < 4; ++k) {
      int h = hex(take());
      if (h < 0) fail("bad \\u escape");
      cp = (cp << 4) | static_cast<std::uint32_t>(h);
    }
    return cp;
  }
  std::string string() {
    expect('"');
    std::string out;
    while (i_ < s_.size()) {
      unsigned char c = static_cast<unsigned char>(take());
      if (c == '"') return out;
      if (c == '\\') {
        char e = take();
        switch (e) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            std::uint32_t cp = unicode_escape();
            if (cp >= 0xd800 && cp <= 0xdbff) {
              if (take() != '\\' || take() != 'u') fail("bad surrogate pair");
              std::uint32_t low = unicode_escape();
              cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
            }
            utf8(cp, out);
            break;
          }
          default: fail("bad escape");
        }
      } else {
        out.push_back(static_cast<char>(c));
      }
    }
    fail("unterminated string");
  }
  Json number() {
    const std::size_t start = i_;
    if (peek() == '-') ++i_;
    while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
    bool real = false;
    if (i_ < s_.size() && s_[i_] == '.') {
      real = true;
      ++i_;
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      real = true;
      ++i_;
      if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    const std::string tok = s_.substr(start, i_ - start);
    if (tok.empty() || tok == "-") fail("bad number");
    if (real) return Json(std::stod(tok));
    return Json(static_cast<std::int64_t>(std::stoll(tok)));
  }
  Json value() {
    skip();
    const char c = peek();
    if (c == '{') return object();
    if (c == '[') return array();
    if (c == '"') return Json(string());
    if (c == 't') {
      if (s_.compare(i_, 4, "true") != 0) fail("bad literal");
      i_ += 4;
      return Json(true);
    }
    if (c == 'f') {
      if (s_.compare(i_, 5, "false") != 0) fail("bad literal");
      i_ += 5;
      return Json(false);
    }
    if (c == 'n') {
      if (s_.compare(i_, 4, "null") != 0) fail("bad literal");
      i_ += 4;
      return Json(nullptr);
    }
    if (c == '-' || (c >= '0' && c <= '9')) return number();
    fail("unexpected character");
  }
  Json object() {
    expect('{');
    Json::Object out;
    skip();
    if (peek() == '}') {
      take();
      return Json(std::move(out));
    }
    while (true) {
      skip();
      std::string key = string();
      skip();
      expect(':');
      out[std::move(key)] = value();
      skip();
      char c = take();
      if (c == '}') break;
      if (c != ',') fail("expected ',' or '}'");
    }
    return Json(std::move(out));
  }
  Json array() {
    expect('[');
    Json::Array out;
    skip();
    if (peek() == ']') {
      take();
      return Json(std::move(out));
    }
    while (true) {
      out.push_back(value());
      skip();
      char c = take();
      if (c == ']') break;
      if (c != ',') fail("expected ',' or ']'");
    }
    return Json(std::move(out));
  }
};

}  // namespace json_detail

inline Json Json::parse(const std::string& text) {
  return json_detail::Parser(text).parse();
}

}  // namespace dcstudy
