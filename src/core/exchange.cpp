#include "exchange.hpp"
#include "match.hpp"
#include "text.hpp"
#include <algorithm>
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
    if (j.contains("s") && j["s"].is_number_integer()) m.subgroup = j["s"].get<int>();
    if (j.contains("t")) m.tank = (j["t"].is_boolean() && j["t"].get<bool>()) || (j["t"].is_number() && j["t"].get<double>() != 0);
    if (m.account.empty() && m.character.empty()) continue;
    out->push_back(m);
  }
  if (out->empty()) { if (err) *err = "The squad copy has no players in it"; return false; }
  return true;
}

namespace {

std::string fullKey(const std::string& s) { return normKey(stripAccountPrefix(s)); }
std::string baseKey(const std::string& s) { return normKey(stripAccountSuffix(s)); }
// "Name.1234" -> "1234"; empty when the text does not end in .NN to .NNNNNN
std::string numberOf(const std::string& s) {
  std::string t = trimJs(stripAccountPrefix(s));
  size_t dot = t.rfind('.');
  if (dot == std::string::npos) return "";
  std::string d = t.substr(dot + 1);
  if (d.size() < 2 || d.size() > 6) return "";
  for (char c : d) if (c < '0' || c > '9') return "";
  return d;
}
bool sameName(const std::string& x, const std::string& y) {
  if (x.empty() || y.empty()) return false;
  std::string bx = baseKey(x), by = baseKey(y);
  if (bx.empty() || bx != by) return false;
  std::string nx = numberOf(x), ny = numberOf(y);
  return nx.empty() || ny.empty() || nx == ny;   // two different account numbers are two different accounts
}
// 3 = same account text, 2 = same account name, 1 = matched through a character name, 0 = no match
int score(const ExchangeMember& e, const Member& m) {
  // The account is the truth: two known, different account numbers are two different people, whatever the characters say.
  std::string en = numberOf(e.account), mn = numberOf(m.account);
  if (!en.empty() && !mn.empty() && en != mn) return 0;
  if (!e.account.empty() && !m.account.empty()) {
    if (fullKey(e.account) == fullKey(m.account)) return 3;
    if (sameName(e.account, m.account)) return 2;
  }
  const std::string ex[2] = {e.account, e.character};
  const std::string mm[2] = {m.account, m.character};
  for (const auto& x : ex) for (const auto& y : mm) if (sameName(x, y)) return 1;
  return 0;
}

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

  if (roster.size() == 0) {   // nothing to match against yet: the pasted squad becomes the list
    std::vector<Member> fresh;
    for (const auto& e : in) {
      Member m;
      if (looksLikeAccount(e.account)) { m.account = e.account; m.character = e.character; }
      else m.character = e.account.empty() ? e.character : e.account;
      fresh.push_back(m);
    }
    roster.mergePaste(fresh);
    rep.added = (int)roster.size();
  }

  const std::vector<Member>& members = roster.members();
  std::vector<int> pick(in.size(), -1);
  std::vector<bool> used(members.size(), false);
  for (int want = 3; want >= 1; --want)
    for (size_t i = 0; i < in.size(); ++i) {
      if (pick[i] >= 0) continue;
      for (size_t j = 0; j < members.size(); ++j)
        if (!used[j] && score(in[i], members[j]) == want) { pick[i] = (int)j; used[j] = true; break; }
    }

  // Copy the targets first: setSubgroup/assign never reorder the roster, but keep this independent of that.
  struct Target { std::string who; const ExchangeMember* e; };
  std::vector<Target> targets;
  for (size_t i = 0; i < in.size(); ++i) {
    if (pick[i] < 0) { rep.notInSquad.push_back(in[i].account.empty() ? in[i].character : in[i].account); continue; }
    const Member& m = members[(size_t)pick[i]];
    targets.push_back({m.account.empty() ? m.character : m.account, &in[i]});
  }
  rep.matched = (int)targets.size();

  for (const auto& t : targets) {
    const ExchangeMember& e = *t.e;
    if (e.subgroup >= 1 && e.subgroup <= 10) { roster.setSubgroup(t.who, e.subgroup - 1); ++rep.seated; }
    if (!e.role.empty()) {
      if (roles.find(e.role)) {
        assign.set(t.who, e.role); ++rep.roleSet;
        if (roles.classOf(e.role) != "heal") assign.setTank(t.who, false);
      } else if (std::find(rep.unknownRoles.begin(), rep.unknownRoles.end(), e.role) == rep.unknownRoles.end()) {
        rep.unknownRoles.push_back(e.role);
      }
    }
    if (e.tank && roles.classOf(assign.get(t.who)) == "heal") { assign.setTank(t.who, true); ++rep.tanks; }
  }
  return rep;
}

std::string describeExchange(const ExchangeReport& r) {
  std::string s = "Pasted from composer: " + std::to_string(r.matched) + " of " + std::to_string(r.total) + " matched";
  if (r.added) s += " (squad list was empty, " + std::to_string(r.added) + " added)";
  s += "; " + std::to_string(r.seated) + " seated, " + std::to_string(r.roleSet) + " roles";
  if (!r.unknownRoles.empty()) s += "; role not in this addon: " + joinNames(r.unknownRoles, 4);
  if (!r.notInSquad.empty()) s += "; not in the squad list: " + joinNames(r.notInSquad, 4);
  return s;
}

}
