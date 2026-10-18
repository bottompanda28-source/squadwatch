#include "match.hpp"
#include "text.hpp"
#include <algorithm>

namespace sw {
namespace {

bool isAsciiDigit(char32_t c) { return c >= '0' && c <= '9'; }
bool isAsciiWord(char32_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

std::u32string trimDecorU(const std::u32string& t0) {
  // JS: s.trim() first, then strip leading/trailing runs of non letter/number; fall back to trimmed text.
  size_t b = 0, e = t0.size();
  while (b < e && isJsSpace(t0[b])) ++b;
  while (e > b && isJsSpace(t0[e - 1])) --e;
  std::u32string t = t0.substr(b, e - b);
  size_t rb = 0, re = t.size();
  while (rb < re && !isLetterOrNumber(t[rb])) ++rb;
  while (re > rb && !isLetterOrNumber(t[re - 1])) --re;
  std::u32string r = t.substr(rb, re - rb);
  return r.empty() ? t : r;
}

size_t utf16Len(const std::u32string& s) { size_t n = 0; for (char32_t c : s) n += c >= 0x10000 ? 2 : 1; return n; }

struct Adder {
  std::vector<std::string>& out;
  void operator()(const std::u32string& s) {
    std::string k = normKey(toUtf8(s));
    if (k.empty()) return;
    if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
  }
};

// text.replace(/\([^)]*\)|\[[^\]]*\]|\{[^}]*\}/g, ' ')
std::u32string stripBrackets(const std::u32string& t) {
  std::u32string out;
  size_t i = 0;
  while (i < t.size()) {
    char32_t c = t[i];
    char32_t close = c == '(' ? ')' : c == '[' ? ']' : c == '{' ? '}' : 0;
    if (close) {
      size_t j = i + 1;
      while (j < t.size() && t[j] != close) ++j;
      if (j < t.size()) { out.push_back(' '); i = j + 1; continue; }
    }
    out.push_back(c);
    ++i;
  }
  return out;
}

// Length of a separator token at position i for /\s*(?:\/|\||,|;|&|\+|\band\b|\bor\b)\s*/i, or 0.
// Returns the length of the core token (without surrounding whitespace) via tokLen.
bool matchSepToken(const std::u32string& t, size_t i, size_t& tokLen) {
  char32_t c = t[i];
  if (c == '/' || c == '|' || c == ',' || c == ';' || c == '&' || c == '+') { tokLen = 1; return true; }
  auto ci = [&](size_t k, char lo) { return k < t.size() && (t[k] == (char32_t)lo || t[k] == (char32_t)(lo - 32)); };
  auto boundaryBefore = [&](size_t k) { bool prev = k > 0 && isAsciiWord(t[k - 1]); return !prev; };       // next char is a word char
  auto boundaryAfter = [&](size_t k) { return !(k < t.size() && isAsciiWord(t[k])); };
  if (ci(i, 'a') && ci(i + 1, 'n') && ci(i + 2, 'd') && boundaryBefore(i) && boundaryAfter(i + 3)) { tokLen = 3; return true; }
  if (ci(i, 'o') && ci(i + 1, 'r') && boundaryBefore(i) && boundaryAfter(i + 2)) { tokLen = 2; return true; }
  return false;
}

std::vector<std::u32string> splitPieces(const std::u32string& t) {
  std::vector<std::u32string> pieces;
  std::u32string cur;
  size_t i = 0;
  while (i < t.size()) {
    size_t tokLen = 0;
    if (matchSepToken(t, i, tokLen)) {
      // the regex also swallows whitespace on both sides; whitespace is trimmed later anyway
      pieces.push_back(cur);
      cur.clear();
      i += tokLen;
      continue;
    }
    cur.push_back(t[i]);
    ++i;
  }
  pieces.push_back(cur);
  return pieces;
}

} // namespace

std::string trimNameDecor(const std::string& s) { return toUtf8(trimDecorU(toU32(s))); }

std::string stripAccountSuffix(const std::string& name) {
  // s = name.replace(/^[:@\s]+/, '').replace(brackets, ' ').trim()
  std::u32string t = toU32(name);
  size_t b = 0;
  while (b < t.size() && (t[b] == ':' || t[b] == '@' || isJsSpace(t[b]))) ++b;
  std::u32string s = stripBrackets(t.substr(b));
  size_t sb = 0, se = s.size();
  while (sb < se && isJsSpace(s[sb])) ++sb;
  while (se > sb && isJsSpace(s[se - 1])) --se;
  s = s.substr(sb, se - sb);
  // /^(.*?)\.\d{2,6}(?!\d)/ : first '.' followed by a run of 2-6 digits
  bool found = false;
  for (size_t i = 0; i < s.size() && !found; ++i) {
    if (s[i] != '.') continue;
    size_t n = 0;
    while (i + 1 + n < s.size() && isAsciiDigit(s[i + 1 + n])) ++n;
    if (n >= 2 && n <= 6) { s = s.substr(0, i); found = true; }
  }
  if (!found) {
    // s.split(/\s*(?:\/|\||;|&|\+)\s*/)[0].replace(/\s+[-–—]\s+.*$/, '')
    size_t cut = s.size();
    for (size_t i = 0; i < s.size(); ++i)
      if (s[i] == '/' || s[i] == '|' || s[i] == ';' || s[i] == '&' || s[i] == '+') { cut = i; break; }
    s = s.substr(0, cut);
    for (size_t k = 1; k + 1 < s.size(); ++k)
      if ((s[k] == '-' || s[k] == 0x2013 || s[k] == 0x2014) && isJsSpace(s[k - 1]) && isJsSpace(s[k + 1])) {
        size_t c2 = k; while (c2 > 0 && isJsSpace(s[c2 - 1])) --c2;
        s = s.substr(0, c2); break;
      }
  }
  // .replace(/\.\d+$/, '').trim()  then trimNameDecor
  size_t e = s.size();
  size_t d = e; while (d > 0 && isAsciiDigit(s[d - 1])) --d;
  if (d < e && d > 0 && s[d - 1] == '.') s = s.substr(0, d - 1);
  size_t tb = 0, te = s.size();
  while (tb < te && isJsSpace(s[tb])) ++tb;
  while (te > tb && isJsSpace(s[te - 1])) --te;
  return toUtf8(trimDecorU(s.substr(tb, te - tb)));
}

std::vector<std::string> candidateVariants(const std::string& raw) {
  std::vector<std::string> out;
  Adder add{out};
  std::u32string text = toU32(raw);
  // text.replace(/^[:@\s]+/, '')
  size_t b = 0;
  while (b < text.size() && (text[b] == ':' || text[b] == '@' || isJsSpace(text[b]))) ++b;
  text = text.substr(b);
  add(text);

  // account pattern: [\p{L}\p{N}][\p{L}\p{N} ]*\.\d{4}(?!\d), global
  {
    size_t i = 0;
    while (i < text.size()) {
      bool matched = false;
      if (isLetterOrNumber(text[i])) {
        size_t j = i + 1;
        while (j < text.size() && (isLetterOrNumber(text[j]) || text[j] == ' ')) ++j;
        size_t d = j + 1;
        if (j < text.size() && text[j] == '.' && d + 3 < text.size() &&
            isAsciiDigit(text[d]) && isAsciiDigit(text[d + 1]) && isAsciiDigit(text[d + 2]) && isAsciiDigit(text[d + 3]) &&
            !(d + 4 < text.size() && isAsciiDigit(text[d + 4]))) {
          add(text.substr(i, d + 4 - i));
          i = d + 4;
          matched = true;
        }
      }
      if (!matched) ++i;
    }
  }

  std::u32string stripped = stripBrackets(text);
  add(stripped);
  add(trimDecorU(stripped));
  for (const std::u32string& piece : splitPieces(stripped)) {
    // piece.replace(/^[:@\s]+/, '').replace(/\s+[-–—]\s+.*$/, '').trim()
    size_t pb = 0;
    while (pb < piece.size() && (piece[pb] == ':' || piece[pb] == '@' || isJsSpace(piece[pb]))) ++pb;
    std::u32string p = piece.substr(pb);
    for (size_t k = 0; k < p.size(); ++k) {
      if ((p[k] == '-' || p[k] == 0x2013 || p[k] == 0x2014) && k > 0 && isJsSpace(p[k - 1]) && k + 1 < p.size() && isJsSpace(p[k + 1])) {
        size_t cut = k;
        while (cut > 0 && isJsSpace(p[cut - 1])) --cut;
        p = p.substr(0, cut);
        break;
      }
    }
    size_t tb = 0, te = p.size();
    while (tb < te && isJsSpace(p[tb])) ++tb;
    while (te > tb && isJsSpace(p[te - 1])) --te;
    p = p.substr(tb, te - tb);
    if (utf16Len(p) >= 2) { add(p); add(trimDecorU(p)); }
  }
  return out;
}

} // namespace sw
