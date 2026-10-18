// usage: match_dump watchlist.json names.txt  -> per line: standing|id|shown(heal)|hidden(heal)|shown(none)|derived
#include "../src/core/watchlist.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace sw;
static std::string join(const std::vector<std::string>& v) { std::string s; for (size_t i = 0; i < v.size(); ++i) { if (i) s += ","; s += v[i]; } return s; }
int main(int argc, char** argv) {
  if (argc < 3) return 2;
  std::ifstream f(argv[1], std::ios::binary); std::stringstream ss; ss << f.rdbuf();
  Watchlist wl; std::string err;
  if (!wl.loadFromJson(ss.str(), &err)) { std::cerr << err << "\n"; return 1; }
  std::ifstream in(argv[2], std::ios::binary); std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const WlEntry* e = wl.match({line});
    if (!e) { std::cout << "none\n"; continue; }
    const char* st[] = {"none", "trusted", "flagged", "blocked"};
    auto h = wl.pickVisibleTags(*e, "heal", 3), n = wl.pickVisibleTags(*e, "", 3);
    std::vector<std::string> a, b;
    for (auto& t : h.shown) a.push_back(t.name + (t.role ? "*" : "") + (t.pin ? "^" : ""));
    for (auto& t : n.shown) b.push_back(t.name + (t.role ? "*" : "") + (t.pin ? "^" : ""));
    std::string id = e->accounts.empty() ? ("c:" + (e->characters.empty() ? std::string() : e->characters[0])) : e->accounts[0];
    std::cout << st[(int)Watchlist::standing(e)] << "|" << id << "|" << join(a) << "|" << join(h.hidden) << "|" << join(b) << "|" << wl.derivedText(*e) << "\n";
  }
}
