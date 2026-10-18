// Reads names (one per line, UTF-8) from argv[1]; prints their candidate variants joined by U+001F, one line each.
#include "../src/core/match.hpp"
#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::ifstream in(argv[1], std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (argc > 2 && std::string(argv[2]) == "strip") { std::cout << sw::stripAccountSuffix(line) << '\n'; continue; }
    auto v = sw::candidateVariants(line);
    for (size_t i = 0; i < v.size(); ++i) { if (i) std::cout << '\x1f'; std::cout << v[i]; }
    std::cout << '\n';
  }
}
