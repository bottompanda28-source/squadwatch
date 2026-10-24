#include "roles.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include "match.hpp"
#include "text.hpp"
#include <nlohmann/json.hpp>

namespace sw {
using nlohmann::json;

RoleSet RoleSet::defaults() {
  RoleSet s;
  s.roles_ = {
    {"boonheal__quick", "Quick Heal", "heal", "quickheal", "QuickHeal"},
    {"boonheal__alac", "Alac Heal", "heal", "alacheal", "AlacHeal"},
    {"boondps__quick", "Quick DPS", "boondps", "quickdps", "QuickDPS"},
    {"boondps__alac", "Alac DPS", "boondps", "alacdps", "AlacDPS"},
    {"dps", "DPS", "dps", "crossed_swords", "DPS"},
    {"vg_condi_dps", "VG Condi DPS", "specialty", "fire", "VGCondiDPS"},
    {"hk", "HK", "specialty", "hand_splayed", "HK"},
    {"pusher", "Pusher", "specialty", "rightwards_pushing_hand", "Pusher"},
    {"g1", "G1", "specialty", "green_circle", "G1"},
    {"kite_g2", "Kite G2", "specialty", "1hc1dhuum", "KiteG2"},
    {"kite", "Q1 Kite", "specialty", "1mg1qadim", "Q1Kite"},
    {"lamp", "Lamp", "specialty", "diya_lamp", "Lamp"},
    {"mat_tank", "Mat Tank", "specialty", "dragon", "MatTank"},
    {"qtp_pylon_kiter", "Q2 Kite", "specialty", "1ka1qadim", "Q2kite"},
    {"ranged_dps", "Ranged DPS", "specialty", "bow_and_arrow", "RangedDPS"},
    {"decima_kite", "Decima Kite", "specialty", "decima", "DecimaKite"},
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
void Assignments::setPrefer(const std::string& account, const std::string& code) {
  std::string k = normKey(stripAccountPrefix(account));
  if (k.empty()) return;
  if (code.empty()) prefers_.erase(k); else prefers_[k] = code;
}
std::string Assignments::getPrefer(const std::string& account) const {
  auto it = prefers_.find(normKey(stripAccountPrefix(account)));
  return it == prefers_.end() ? "" : it->second;
}
bool Assignments::isTank(const std::string& account) const { return tanks_.count(normKey(stripAccountPrefix(account))) > 0; }
void Assignments::setTank(const std::string& account, bool on) {
  std::string k = normKey(stripAccountPrefix(account));
  if (k.empty()) return;
  if (on) tanks_.insert(k); else tanks_.erase(k);
}

void Assignments::prune(const std::vector<Member>& present) {
  for (auto it = map_.begin(); it != map_.end();) {
    bool here = false;
    for (const auto& m : present) if (m.key() == it->first) { here = true; break; }
    it = here ? std::next(it) : map_.erase(it);
  }
  for (auto it = tanks_.begin(); it != tanks_.end();) {
    bool here = false;
    for (const auto& m : present) if (m.key() == *it) { here = true; break; }
    it = here ? std::next(it) : tanks_.erase(it);
  }
  for (auto it = prefers_.begin(); it != prefers_.end();) {
    bool here = false;
    for (const auto& m : present) if (m.key() == it->first) { here = true; break; }
    it = here ? std::next(it) : prefers_.erase(it);
  }
}
std::string Assignments::toJson() const {
  json j; j["version"] = 1; j["assignments"] = json::object(); j["tanks"] = json::array(); j["prefers"] = json::object();
  for (const auto& kv : map_) j["assignments"][kv.first] = kv.second;
  for (const auto& t : tanks_) j["tanks"].push_back(t);
  for (const auto& kv : prefers_) j["prefers"][kv.first] = kv.second;
  return j.dump(2);
}
bool Assignments::loadFromJson(const std::string& text, std::string* err) {
  json root;
  try { root = json::parse(text); }
  catch (const std::exception& ex) { if (err) *err = std::string("Not valid JSON: ") + ex.what(); return false; }
  if (!root.is_object() || !root.contains("assignments") || !root["assignments"].is_object()) { if (err) *err = "No \"assignments\" object."; return false; }
  map_.clear(); tanks_.clear(); prefers_.clear();
  for (auto it = root["assignments"].begin(); it != root["assignments"].end(); ++it)
    if (it.value().is_string()) map_[normKey(it.key())] = it.value().get<std::string>();
  if (root.contains("tanks") && root["tanks"].is_array())
    for (const auto& t : root["tanks"]) if (t.is_string()) tanks_.insert(normKey(t.get<std::string>()));
  if (root.contains("prefers") && root["prefers"].is_object())
    for (auto it = root["prefers"].begin(); it != root["prefers"].end(); ++it)
      if (it.value().is_string()) prefers_[normKey(it.key())] = it.value().get<std::string>();
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


std::string lfgRolesText(const std::vector<BoardRow>& board, int rosterSize, int target) {
  std::string out;
  int claimed = 0;
  bool dps = false;
  for (const auto& row : board) {
    if (row.open <= 0) continue;
    claimed += row.open;
    if (row.role.code == "dps") { dps = true; continue; }
    std::string tok = row.role.shortName;
    for (auto& c : tok) c = (char)std::tolower((unsigned char)c);
    out += (out.empty() ? "" : ", ") + std::to_string(row.open) + tok;
  }
  if (!dps && rosterSize + claimed < target) dps = true;
  if (dps) out += (out.empty() ? "" : ", ") + std::string("dps");
  return out;
}

static std::string groupText(const RoleSet& roles, const Assignments& a, const std::vector<Member>& present, const std::function<bool(const RoleDef&)>& pick) {
  std::string out;
  for (const auto& r : roles.roles()) {
    if (!pick(r)) continue;
    std::string names; int n = 0;
    for (const auto& m : present) {
      const std::string& who = m.account.empty() ? m.character : m.account;
      if (a.get(who) != r.code) continue;
      names += (n++ ? ", " : "") + stripAccountSuffix(who);
    }
    if (!n) continue;
    out += (out.empty() ? "" : "; ") + names + " - " + r.label;
  }
  return out;
}
std::string copyByRole(const RoleSet& roles, const Assignments& a, const std::vector<Member>& present, const std::string& roleClass) {
  return groupText(roles, a, present, [&](const RoleDef& r) { return roleClass.empty() || r.roleClass == roleClass; });
}
std::string copyOneRole(const RoleSet& roles, const Assignments& a, const std::vector<Member>& present, const std::string& code) {
  return groupText(roles, a, present, [&](const RoleDef& r) { return r.code == code; });
}

}
