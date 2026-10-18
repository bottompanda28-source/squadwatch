#include "roles.hpp"
#include <algorithm>
#include "text.hpp"
#include <nlohmann/json.hpp>

namespace sw {
using nlohmann::json;

RoleSet RoleSet::defaults() {
  RoleSet s;
  s.roles_ = {
    {"boonheal__quick", "Quick Heal", "heal", "quickheal"},
    {"boonheal__alac", "Alac Heal", "heal", "alacheal"},
    {"boondps__quick", "Quick DPS", "boondps", "quickdps"},
    {"boondps__alac", "Alac DPS", "boondps", "alacdps"},
    {"dps", "DPS", "dps", "crossed_swords"},
    {"vg_condi_dps", "VG Condi DPS", "specialty", "fire"},
    {"g1", "G1", "specialty", "green_circle"},
    {"kite_g2", "Kite G2", "specialty", "1hc1dhuum"},
    {"pusher", "Pusher", "specialty", "rightwards_pushing_hand"},
    {"qtp_pylon_kiter", "QTP Pylon Kiter", "specialty", "1ka1qadim"},
    {"hk", "HK", "specialty", "hand_splayed"},
    {"kite", "Q1 Kite", "specialty", "1mg1qadim"},
    {"lamp", "Lamp", "specialty", "diya_lamp"},
    {"mat_tank", "Mat Tank", "specialty", "dragon"},
    {"ranged_dps", "Ranged DPS", "specialty", "bow_and_arrow"},
    {"decima_kite", "Decima Kite", "specialty", ""},
  };
  return s;
}

const RoleDef* RoleSet::find(const std::string& code) const {
  for (const auto& r : roles_) if (r.code == code) return &r;
  return nullptr;
}
std::string RoleSet::classOf(const std::string& code) const { const RoleDef* r = find(code); return r ? r->roleClass : ""; }
std::string RoleSet::labelOf(const std::string& code) const { const RoleDef* r = find(code); return r ? r->label : code; }

void Assignments::set(const std::string& account, const std::string& code) {
  std::string k = normKey(stripAccountPrefix(account));
  if (k.empty()) return;
  if (code.empty()) map_.erase(k); else map_[k] = code;
}
std::string Assignments::get(const std::string& account) const {
  auto it = map_.find(normKey(stripAccountPrefix(account)));
  return it == map_.end() ? "" : it->second;
}
void Assignments::prune(const std::vector<Member>& present) {
  for (auto it = map_.begin(); it != map_.end();) {
    bool here = false;
    for (const auto& m : present) if (m.key() == it->first) { here = true; break; }
    it = here ? std::next(it) : map_.erase(it);
  }
}
std::string Assignments::toJson() const {
  json j; j["version"] = 1; j["assignments"] = json::object();
  for (const auto& kv : map_) j["assignments"][kv.first] = kv.second;
  return j.dump(2);
}
bool Assignments::loadFromJson(const std::string& text, std::string* err) {
  json root;
  try { root = json::parse(text); }
  catch (const std::exception& ex) { if (err) *err = std::string("Not valid JSON: ") + ex.what(); return false; }
  if (!root.is_object() || !root.contains("assignments") || !root["assignments"].is_object()) { if (err) *err = "No \"assignments\" object."; return false; }
  map_.clear();
  for (auto it = root["assignments"].begin(); it != root["assignments"].end(); ++it)
    if (it.value().is_string()) map_[normKey(it.key())] = it.value().get<std::string>();
  return true;
}

std::vector<BoardRow> buildBoard(const RoleSet& roles, const Assignments& a, const std::vector<Member>& present,
                                 const std::map<std::string, int>& need) {
  std::vector<BoardRow> rows;
  for (const auto& r : roles.roles()) {
    BoardRow row; row.role = r;
    for (const auto& m : present) {
      const std::string& who = m.account.empty() ? m.character : m.account;
      if (a.get(who) == r.code) row.holders.push_back(who);
    }
    auto it = need.find(r.code);
    row.need = it == need.end() ? 0 : std::max(0, it->second);
    int have = (int)row.holders.size();
    row.open = std::max(0, row.need - have);
    row.over = row.need > 0 ? std::max(0, have - row.need) : 0;
    rows.push_back(row);
  }
  return rows;
}

}
