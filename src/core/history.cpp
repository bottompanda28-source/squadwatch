#include "history.hpp"
#include <nlohmann/json.hpp>
#include <map>

namespace sw {
using nlohmann::json;

ChangeEntry diffForHistory(const std::vector<SnapMember>& before, const std::vector<Member>& after, const std::string& time) {
  ChangeEntry e; e.time = time;
  std::map<std::string, bool> prev, cur;
  for (const auto& s : before) prev[s.m.key()] = true;
  for (const auto& m : after) cur[m.key()] = true;
  for (const auto& m : after) if (!prev.count(m.key())) e.joined.push_back(m.account.empty() ? m.character : m.account);
  for (const auto& s : before) if (!cur.count(s.m.key())) e.left.push_back(s);
  return e;
}

static std::string rolesText(const ChangeEntry& e, const RoleSet& roles, bool compact) {
  std::map<std::string, int> counts;
  for (const auto& s : e.left) counts[s.role.empty() || !roles.find(s.role) ? "dps" : s.role]++;
  std::string out;
  for (const auto& r : roles.roles()) {
    auto it = counts.find(r.code);
    if (it == counts.end()) continue;
    out += (out.empty() ? "" : ", ") + std::to_string(it->second) + (compact ? "" : " ") + (compact ? r.shortName : r.label);
  }
  return out;
}
std::string leftRolesDisplay(const ChangeEntry& e, const RoleSet& roles) { return rolesText(e, roles, false); }
std::string leftRolesClipboard(const ChangeEntry& e, const RoleSet& roles) { return rolesText(e, roles, true); }

void ChangeLog::push(const ChangeEntry& e) {
  entries_.insert(entries_.begin(), e);
  if (entries_.size() > kMax) entries_.resize(kMax);
}

static json memberJson(const SnapMember& s) {
  return {{"account", s.m.account}, {"character", s.m.character}, {"subgroup", s.m.subgroup}, {"role", s.role}};
}
static bool memberFrom(const json& j, SnapMember* s) {
  if (!j.is_object()) return false;
  auto str = [&](const char* k) { return (j.contains(k) && j[k].is_string()) ? j[k].get<std::string>() : std::string(); };
  s->m.account = str("account"); s->m.character = str("character"); s->role = str("role");
  s->m.subgroup = (j.contains("subgroup") && j["subgroup"].is_number_integer()) ? j["subgroup"].get<int>() : -1;
  return !s->m.key().empty();
}

std::string ChangeLog::toJson() const {
  json arr = json::array();
  for (const auto& e : entries_) {
    json left = json::array();
    for (const auto& s : e.left) left.push_back(memberJson(s));
    json je = {{"time", e.time}, {"joined", e.joined}, {"left", left}};
    if (e.live) je["live"] = true;
    if (e.mass) je["mass"] = true;
    arr.push_back(je);
  }
  return json{{"version", 1}, {"entries", arr}}.dump(2);
}

bool ChangeLog::loadFromJson(const std::string& text) {
  try {
    json j = json::parse(text);
    if (!j.is_object() || !j.contains("entries") || !j["entries"].is_array()) return false;
    std::vector<ChangeEntry> v;
    for (const auto& je : j["entries"]) {
      if (!je.is_object()) continue;
      ChangeEntry e;
      if (je.contains("time") && je["time"].is_string()) e.time = je["time"].get<std::string>();
      if (je.contains("joined") && je["joined"].is_array()) for (const auto& n : je["joined"]) if (n.is_string()) e.joined.push_back(n.get<std::string>());
      if (je.contains("left") && je["left"].is_array()) for (const auto& l : je["left"]) { SnapMember s; if (memberFrom(l, &s)) e.left.push_back(s); }
      if (je.contains("live") && je["live"].is_boolean()) e.live = je["live"].get<bool>();
      if (je.contains("mass") && je["mass"].is_boolean()) e.mass = je["mass"].get<bool>();
      v.push_back(e);
      if (v.size() >= kMax) break;
    }
    entries_ = v;
    return true;
  } catch (...) { return false; }
}

std::string snapshotToJson(const std::vector<SnapMember>& s) {
  json arr = json::array();
  for (const auto& m : s) arr.push_back(memberJson(m));
  return json{{"version", 1}, {"members", arr}}.dump(2);
}
bool snapshotFromJson(const std::string& text, std::vector<SnapMember>* out) {
  try {
    json j = json::parse(text);
    if (!j.is_object() || !j.contains("members") || !j["members"].is_array()) return false;
    std::vector<SnapMember> v;
    for (const auto& e : j["members"]) { SnapMember s; if (memberFrom(e, &s)) v.push_back(s); }
    *out = v;
    return true;
  } catch (...) { return false; }
}

}
