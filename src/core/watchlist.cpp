#include "watchlist.hpp"
#include "match.hpp"
#include "text.hpp"
#include <algorithm>
#include <set>
#include <nlohmann/json.hpp>

namespace sw {
using nlohmann::json;

namespace {
std::vector<std::string> strArray(const json& j, const char* key) {
  std::vector<std::string> out;
  auto it = j.find(key);
  if (it == j.end() || !it->is_array()) return out;
  for (const auto& v : *it) if (v.is_string()) out.push_back(v.get<std::string>());
  return out;
}
std::string str(const json& j, const char* key) {
  auto it = j.find(key);
  return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}
void addUnique(std::vector<std::string>& v, const std::string& s) {
  if (!s.empty() && std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}
int rankOf(const WlEntry* e) { return Watchlist::isBlocked(e) ? 2 : (Watchlist::isFlagged(e) ? 1 : 0); }
bool has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
}

bool Watchlist::loadFromJson(const std::string& text, std::string* err) {
  json root;
  try { root = json::parse(text); }
  catch (const std::exception& ex) { if (err) *err = std::string("Not valid JSON: ") + ex.what(); return false; }
  if (!root.is_object() || !root.contains("players") || !root["players"].is_array()) {
    if (err) *err = "Not a watchlist export (no \"players\" list).";
    return false;
  }
  players_.clear(); acctIdx_.clear(); charIdx_.clear(); meta_.clear(); categoryIds_.clear();

  const json empty = json::object();
  const json& cfg = (root.contains("tagConfig") && root["tagConfig"].is_object()) ? root["tagConfig"] : empty;
  if (cfg.contains("categories") && cfg["categories"].is_array())
    for (const auto& c : cfg["categories"]) { std::string id = str(c, "id"); if (!id.empty()) categoryIds_.push_back(id); }
  if (cfg.contains("tagMeta") && cfg["tagMeta"].is_object()) {
    for (auto it = cfg["tagMeta"].begin(); it != cfg["tagMeta"].end(); ++it) {
      if (!it.value().is_object()) continue;
      const json& m = it.value();
      TagMeta t;
      t.definition = str(m, "definition"); t.phrase = str(m, "phrase"); t.status = str(m, "status"); t.parent = str(m, "parent");
      t.pin = m.contains("pin") && m["pin"].is_boolean() && m["pin"].get<bool>();
      t.roles = strArray(m, "roles");
      meta_[it.key()] = t;
    }
  }
  for (const auto& p : root["players"]) {
    if (!p.is_object()) continue;
    WlEntry e;
    e.accounts = strArray(p, "accounts"); e.characters = strArray(p, "characters"); e.sections = strArray(p, "sections");
    e.tags = strArray(p, "tags"); e.archetypes = strArray(p, "archetypes"); e.notes = strArray(p, "notes");
    e.severity = str(p, "severity");
    for (const auto& id : categoryIds_) for (const auto& v : strArray(p, id.c_str())) addUnique(e.extraTags, v);
    players_.push_back(std::move(e));
  }
  for (size_t i = 0; i < players_.size(); ++i) {
    for (const auto& a : players_[i].accounts) { std::string k = normKey(a); if (!k.empty()) acctIdx_[k] = i; }
    for (const auto& c : players_[i].characters) { std::string k = normKey(c); if (!k.empty()) charIdx_[k] = i; }
  }
  return true;
}

const WlEntry* Watchlist::match(const std::vector<std::string>& candidates) const {
  std::vector<std::string> keys;
  for (const auto& c : candidates) for (const auto& k : candidateVariants(c)) addUnique(keys, k);
  auto pickWorst = [&](const std::map<std::string, size_t>& index) -> const WlEntry* {
    const WlEntry* best = nullptr;
    for (const auto& k : keys) {
      auto it = index.find(k);
      if (it == index.end()) continue;
      const WlEntry* e = &players_[it->second];
      if (!best || rankOf(e) > rankOf(best)) best = e;
    }
    return best;
  };
  if (const WlEntry* e = pickWorst(acctIdx_)) return e;
  return pickWorst(charIdx_);
}

const WlEntry* Watchlist::match(const std::string& account, const std::string& character) const {
  return match(std::vector<std::string>{account, character});
}

bool Watchlist::isBlocked(const WlEntry* e) { return e && has(e->sections, "blocklist"); }
bool Watchlist::isFlagged(const WlEntry* e) { return e && !isBlocked(e) && has(e->sections, "badplayers"); }
bool Watchlist::isTrusted(const WlEntry* e) { return e && !isBlocked(e) && !isFlagged(e) && has(e->sections, "trusted"); }
Standing Watchlist::standing(const WlEntry* e) {
  if (isBlocked(e)) return Standing::Blocked;
  if (isFlagged(e)) return Standing::Flagged;
  if (isTrusted(e)) return Standing::Trusted;
  return Standing::None;
}
std::string Watchlist::severityLabel(const std::string& c) {
  if (c == "very") return "Terrible";
  if (c == "really") return "Awful";
  if (c == "pretty") return "Bad";
  if (c == "mildly") return "Mediocre";
  return c;
}

std::vector<std::string> Watchlist::entryTagNames(const WlEntry& e) const {
  std::vector<std::string> out;
  for (const auto& v : e.tags) addUnique(out, v);
  for (const auto& v : e.archetypes) addUnique(out, v);
  for (const auto& v : e.extraTags) addUnique(out, v);
  return out;
}

namespace {
std::set<std::string> parentSet(const std::map<std::string, TagMeta>& meta, const std::vector<std::string>& names) {
  std::set<std::string> parents;
  for (const auto& n : names) {
    auto it = meta.find(n);
    std::string p = it != meta.end() ? it->second.parent : std::string();
    int guard = 0;
    while (!p.empty() && guard++ < 10) {
      parents.insert(p);
      auto pit = meta.find(p);
      p = pit != meta.end() ? pit->second.parent : std::string();
    }
  }
  return parents;
}
}

VisibleTags Watchlist::pickVisibleTags(const WlEntry& e, const std::string& roleClass, size_t max) const {
  std::vector<std::string> names = entryTagNames(e);
  auto parents = parentSet(meta_, names);
  names.erase(std::remove_if(names.begin(), names.end(), [&](const std::string& n) { return parents.count(n) > 0; }), names.end());
  auto isRole = [&](const std::string& n) {
    if (roleClass.empty()) return false;
    auto it = meta_.find(n);
    return it != meta_.end() && has(it->second.roles, roleClass);
  };
  auto isPin = [&](const std::string& n) { auto it = meta_.find(n); return it != meta_.end() && it->second.pin; };
  std::vector<std::string> ordered;
  for (const auto& n : names) if (isRole(n)) ordered.push_back(n);
  for (const auto& n : names) if (!isRole(n) && isPin(n)) ordered.push_back(n);
  for (const auto& n : names) if (!isRole(n) && !isPin(n)) ordered.push_back(n);
  VisibleTags out;
  for (size_t i = 0; i < ordered.size(); ++i) {
    if (i < max) out.shown.push_back({ordered[i], isRole(ordered[i]), isPin(ordered[i])});
    else out.hidden.push_back(ordered[i]);
  }
  return out;
}

std::string Watchlist::tagTitle(const std::string& name) const {
  auto it = meta_.find(name);
  if (it == meta_.end()) return "";
  const TagMeta& m = it->second;
  std::string s = m.definition;
  auto add = [&](const std::string& b) { if (!s.empty()) s += "\n"; s += b; };
  if (!m.parent.empty()) add("Part of: " + m.parent);
  if (!m.status.empty() && m.status != "settled") add("Tag status: " + m.status);
  return s;
}

std::string Watchlist::derivedText(const WlEntry& e) const {
  auto phrase = [&](const std::string& n) {
    auto it = meta_.find(n);
    std::string p = it != meta_.end() ? trimJs(it->second.phrase) : std::string();
    return p.empty() ? lowerUtf8(n) : p;
  };
  auto parents = parentSet(meta_, entryTagNames(e));
  auto keep = [&](const std::vector<std::string>& v) {
    std::vector<std::string> out;
    for (const auto& n : v) if (!parents.count(n)) out.push_back(phrase(n));
    return out;
  };
  auto join = [](const std::vector<std::string>& v, const char* sep) {
    std::string s; for (size_t i = 0; i < v.size(); ++i) { if (i) s += sep; s += v[i]; } return s;
  };
  std::vector<std::string> parts;
  if (!e.severity.empty()) parts.push_back(lowerUtf8(severityLabel(e.severity)) + " overall");
  auto arch = keep(e.archetypes), pers = keep(e.tags);
  if (!arch.empty()) parts.push_back(join(arch, "; "));
  if (!pers.empty()) parts.push_back(join(pers, "; "));
  if (parts.empty()) return "";
  std::string t = join(parts, " \xC2\xB7 ");
  if (!t.empty() && (unsigned char)t[0] < 0x80) t[0] = (char)toupper((unsigned char)t[0]);
  return t;
}

}
