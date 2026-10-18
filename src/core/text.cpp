#include "text.hpp"
#include "unicode_tables.hpp"
#include <algorithm>

namespace sw {

std::u32string toU32(const std::string& s) {
  std::u32string out;
  out.reserve(s.size());
  size_t i = 0, n = s.size();
  while (i < n) {
    unsigned char c = (unsigned char)s[i];
    char32_t cp; int len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c >> 5) == 0x6) { cp = c & 0x1F; len = 2; }
    else if ((c >> 4) == 0xE) { cp = c & 0x0F; len = 3; }
    else if ((c >> 3) == 0x1E) { cp = c & 0x07; len = 4; }
    else { out.push_back(0xFFFD); ++i; continue; }
    if (i + len > n) { out.push_back(0xFFFD); break; }
    bool ok = true;
    for (int k = 1; k < len; ++k) {
      unsigned char cc = (unsigned char)s[i + k];
      if ((cc >> 6) != 0x2) { ok = false; break; }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!ok) { out.push_back(0xFFFD); ++i; continue; }
    out.push_back(cp);
    i += len;
  }
  return out;
}

std::string toUtf8(const std::u32string& s) {
  std::string out;
  out.reserve(s.size());
  for (char32_t cp : s) {
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) { out.push_back((char)(0xC0 | (cp >> 6))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) { out.push_back((char)(0xE0 | (cp >> 12))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
    else { out.push_back((char)(0xF0 | (cp >> 18))); out.push_back((char)(0x80 | ((cp >> 12) & 0x3F))); out.push_back((char)(0x80 | ((cp >> 6) & 0x3F))); out.push_back((char)(0x80 | (cp & 0x3F))); }
  }
  return out;
}

bool isLetterOrNumber(char32_t c) {
  size_t lo = 0, hi = uni::kLetterNumberCount;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    const uni::Range& r = uni::kLetterNumber[mid];
    if (c < r.lo) hi = mid;
    else if (c > r.hi) lo = mid + 1;
    else return true;
  }
  return false;
}

bool isJsSpace(char32_t c) {
  switch (c) {
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20: case 0xA0:
    case 0x1680: case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000: case 0xFEFF:
      return true;
  }
  return c >= 0x2000 && c <= 0x200A;
}

std::u32string lowerU32(const std::u32string& s) {
  std::u32string out;
  out.reserve(s.size());
  for (char32_t c : s) {
    size_t lo = 0, hi = uni::kLowerCount;
    bool found = false;
    while (lo < hi) {
      size_t mid = (lo + hi) / 2;
      const uni::LowerMap& m = uni::kLower[mid];
      if (c < m.from) hi = mid;
      else if (c > m.from) lo = mid + 1;
      else { out.push_back(m.to1); if (m.to2) out.push_back(m.to2); found = true; break; }
    }
    if (!found) out.push_back(c);
  }
  return out;
}

std::string lowerUtf8(const std::string& s) { return toUtf8(lowerU32(toU32(s))); }

std::string trimJs(const std::string& s) {
  std::u32string u = toU32(s);
  size_t b = 0, e = u.size();
  while (b < e && isJsSpace(u[b])) ++b;
  while (e > b && isJsSpace(u[e - 1])) --e;
  return toUtf8(u.substr(b, e - b));
}

std::string normKey(const std::string& s) { return lowerUtf8(trimJs(s)); }

std::vector<std::string> splitLines(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '\r') { out.push_back(cur); cur.clear(); if (i + 1 < s.size() && s[i + 1] == '\n') ++i; }
    else if (c == '\n') { out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

} // namespace sw
