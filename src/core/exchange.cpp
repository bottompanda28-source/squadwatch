#include "exchange.hpp"
#include "match.hpp"
#include "text.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <nlohmann/json.hpp>

namespace sw {
using nlohmann::json;

std::string buildExchange(const RoleSet& roles, const Assignments& a, const std::vector<Member>& members) {
  (void)roles;
  json arr = json::array();
  for (const auto& m : members) {
    const std::string& who = m.account.empty() ? m.character : m.account;
    json e = json::object();
    e["a"] = who;
    if (!m.account.empty() && !m.character.empty()) e["c"] = m.character;
    std::string role = a.get(who);
    if (!role.empty()) e["r"] = role;
    std::string pref = a.getPrefer(who);
    if (!pref.empty()) e["p"] = pref;
    if (m.subgroup >= 0) e["s"] = m.subgroup + 1;
    if (a.isTank(who)) e["t"] = 1;
    arr.push_back(e);
  }
  json root = json::object();
  root["gw2squad"] = 1;
  root["members"] = arr;
  return root.dump(-1, ' ', false, json::error_handler_t::replace);
}

bool parseExchange(const std::string& text, std::vector<ExchangeMember>* out, std::string* err) {
  out->clear();
  size_t b = text.find('{'), e = text.rfind('}');
  if (b == std::string::npos || e == std::string::npos || e < b) { if (err) *err = "Clipboard does not hold a squad copy from the composer"; return false; }
  json root;
  try { root = json::parse(text.substr(b, e - b + 1)); }
  catch (const std::exception&) { if (err) *err = "Clipboard text is not a complete squad copy (it may have been cut off)"; return false; }
  if (!root.is_object() || !root.contains("gw2squad") || !root.contains("members") || !root["members"].is_array()) {
    if (err) *err = "Clipboard does not hold a squad copy from the composer";
    return false;
  }
  auto str = [](const json& o, const char* k) { return (o.contains(k) && o[k].is_string()) ? o[k].get<std::string>() : std::string(); };
  for (const auto& j : root["members"]) {
    if (!j.is_object()) continue;
    ExchangeMember m;
    m.account = stripAccountPrefix(str(j, "a"));
    m.character = trimJs(str(j, "c"));
    m.role = str(j, "r");
    m.prefer = str(j, "p");
    if (j.contains("s") && j["s"].is_number_integer()) m.subgroup = j["s"].get<int>();
    if (j.contains("t")) m.tank = (j["t"].is_boolean() && j["t"].get<bool>()) || (j["t"].is_number() && j["t"].get<double>() != 0);
    if (m.account.empty() && m.character.empty()) continue;
    out->push_back(m);
  }
  if (out->empty()) { if (err) *err = "The squad copy has no players in it"; return false; }
  return true;
}

namespace {
std::string joinNames(const std::vector<std::string>& v, size_t maxShown) {
  std::string s;
  for (size_t i = 0; i < v.size() && i < maxShown; ++i) s += (i ? ", " : "") + v[i];
  if (v.size() > maxShown) s += " (+" + std::to_string(v.size() - maxShown) + " more)";
  return s;
}
}  // namespace

ExchangeReport applyExchange(Roster& roster, Assignments& assign, const RoleSet& roles, const std::vector<ExchangeMember>& in) {
  ExchangeReport rep;
  rep.total = (int)in.size();
  rep.replaced = (int)roster.size();

  std::map<std::string, std::string> oldCharacter;   // so a paste without character names does not lose them
  for (const auto& m : roster.members()) if (!m.account.empty() && !m.character.empty()) oldCharacter[m.key()] = m.character;

  struct Entry { Member m; const ExchangeMember* e; };
  std::vector<Entry> fresh;
  std::set<std::string> seen;
  for (const auto& e : in) {
    Member m;
    if (looksLikeAccount(e.account)) { m.account = e.account; m.character = e.character; }
    else if (looksLikeAccount(e.character)) { m.account = e.character; m.character = e.account; }   // columns swapped
    else m.character = e.account.empty() ? e.character : e.account;                                  // no account known: name fallback
    if (m.key().empty()) continue;
    if (!seen.insert(m.key()).second) { ++rep.duplicates; continue; }
    if (m.character.empty()) { auto it = oldCharacter.find(m.key()); if (it != oldCharacter.end()) m.character = it->second; }
    fresh.push_back({m, &e});
  }

  std::vector<Member> list;
  for (const auto& f : fresh) list.push_back(f.m);
  roster.replaceWithPaste(list);
  assign.clear();                      // roles and tank marks of the old list
  assign.prune(roster.members());      // prefers of players who are no longer in the squad

  for (const auto& f : fresh) {
    const std::string who = f.m.account.empty() ? f.m.character : f.m.account;
    const ExchangeMember& e = *f.e;
    if (e.subgroup >= 1 && e.subgroup <= 10) { roster.setSubgroup(who, e.subgroup - 1); ++rep.seated; }
    if (!e.role.empty()) {
      if (roles.find(e.role)) { assign.set(who, e.role); ++rep.roleSet; }
      else if (std::find(rep.unknownRoles.begin(), rep.unknownRoles.end(), e.role) == rep.unknownRoles.end()) rep.unknownRoles.push_back(e.role);
    }
    if (!e.prefer.empty()) {
      if (roles.find(e.prefer)) { assign.setPrefer(who, e.prefer); ++rep.preferSet; }
      else if (std::find(rep.unknownRoles.begin(), rep.unknownRoles.end(), e.prefer) == rep.unknownRoles.end()) rep.unknownRoles.push_back(e.prefer);
    }
    if (e.tank) {
      if (roles.classOf(assign.get(who)) == "heal") { assign.setTank(who, true); ++rep.tanks; }
      else ++rep.tanksSkipped;          // the addon only holds a tank mark for healers
    }
  }
  return rep;
}

std::string describeExchange(const ExchangeReport& r) {
  std::string s = "Pasted from composer: squad replaced (" + std::to_string(r.replaced) + " -> " + std::to_string(r.total - r.duplicates) + " players); "
                + std::to_string(r.seated) + " seated, " + std::to_string(r.roleSet) + " roles";
  if (r.preferSet) s += ", " + std::to_string(r.preferSet) + " prefers";
  if (r.tanks) s += ", " + std::to_string(r.tanks) + " tank" + (r.tanks == 1 ? "" : "s");
  if (r.tanksSkipped) s += "; " + std::to_string(r.tanksSkipped) + " tank mark" + (r.tanksSkipped == 1 ? "" : "s") + " skipped (here only healers can be the tank)";
  if (!r.unknownRoles.empty()) s += "; role not in this addon: " + joinNames(r.unknownRoles, 4);
  if (r.duplicates) s += "; " + std::to_string(r.duplicates) + " duplicate(s) skipped";
  return s;
}

}
