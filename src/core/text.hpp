#pragma once
// UTF-8 / Unicode helpers. Portable (no Windows or ImGui dependency).
#include <string>
#include <vector>

namespace sw {
std::u32string toU32(const std::string& utf8);
std::string toUtf8(const std::u32string& s);
bool isLetterOrNumber(char32_t c);   // \p{L} | \p{N}, same as the composer's JS
bool isJsSpace(char32_t c);          // what JavaScript's \s / trim() treat as whitespace
std::u32string lowerU32(const std::u32string& s);
std::string lowerUtf8(const std::string& s);
std::string trimJs(const std::string& s);
std::string normKey(const std::string& s);   // wlNormKey: trim + lowercase
std::vector<std::string> splitLines(const std::string& s);  // handles \r\n, \n, \r
}
