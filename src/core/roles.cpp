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

std::string RoleSet::slug(const std::string& label) {
  // mirrors the composer's slugify(): lower case, drop \(...\) (non-greedy), runs of anything but a-z0-9 become "_", trim "_"
  std::string s;
  for (unsigned char c : label) s += (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
  std::string t;
  for (size_t i = 0; i < s.size();) {
    if (s[i] == '(') { size_t j = s.find(')', i + 1); if (j != std::string::npos) { i = j + 1; continue; } }
    t += s[i++];
  }
  std::string out; bool sep = false;
  for (unsigned char c : t) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { if (sep && !out.empty()) out += '_'; sep = false; out += (char)c; }
    else sep = true;
  }
  return out.empty() ? "role" : out;
}
std::string RoleSet::defaultShortName(const std::string& label) {
  std::string out;
  for (char c : trimJs(label)) if (c != ' ' && c != '\t') out += c;
  return out;
}
bool RoleSet::validClass(const std::string& c) { return c == "heal" || c == "boondps" || c == "dps" || c == "specialty"; }
std::string RoleSet::uniqueCode(const std::string& label) const {
  std::string base = slug(label), id = base;
  for (int n = 2; find(id); ++n) id = base + "_" + std::to_string(n);
  return id;
}

static bool checkDef(const RoleSet& rs, RoleDef& d, const std::string& self, std::string* err) {
  auto fail = [&](const char* m) { if (err) *err = m; return false; };
  d.label = trimJs(d.label);
  if (d.label.empty()) return fail("The role needs a name.");
  if (d.label.size() > 60) return fail("The name is too long (60 characters at most).");
  if (!RoleSet::validClass(d.roleClass)) return fail("Pick what kind of role it is.");
  const std::string want = lowerUtf8(d.label);
  for (const auto& r : rs.roles()) if (r.code != self && lowerUtf8(r.label) == want) return fail("Another role already has that name.");
  d.shortName = RoleSet::defaultShortName(d.shortName.empty() ? d.label : d.shortName);
  if (d.shortName.empty()) d.shortName = RoleSet::defaultShortName(d.label);
  return true;
}

bool RoleSet::add(RoleDef def, std::string* err) {
  if (!checkDef(*this, def, "", err)) return false;
  if (def.code.empty()) def.code = uniqueCode(def.label);
  else if (find(def.code)) { if (err) *err = "That role id is already used."; return false; }
  roles_.push_back(def);
  return true;
}
bool RoleSet::update(const std::string& code, const RoleDef& def, std::string* err) {
  for (auto& r : roles_) {
    if (r.code != code) continue;
    RoleDef d = def; d.code = code;
    if (!checkDef(*this, d, code, err)) return false;
    r = d;
    return true;
  }
  if (err) *err = "That role does not exist any more.";
  return false;
}
bool RoleSet::remove(const std::string& code) {
  if (isProtected(code)) return false;
  for (auto it = roles_.begin(); it != roles_.end(); ++it) if (it->code == code) { roles_.erase(it); return true; }
  return false;
}
std::string RoleSet::toJson() const {
  json j; j["version"] = 1; j["roles"] = json::array();
  for (const auto& r : roles_) j["roles"].push_back({{"code", r.code}, {"label", r.label}, {"class", r.roleClass}, {"icon", r.icon}, {"short", r.shortName}});
  return j.dump(2);
}
bool RoleSet::loadFromJson(const std::string& text, std::string* err) {
  json root;
  try { root = json::parse(text); }
  catch (const std::exception& ex) { if (err) *err = std::string("Not valid JSON: ") + ex.what(); return false; }
  if (!root.is_object() || !root.contains("roles") || !root["roles"].is_array()) { if (err) *err = "No \"roles\" list."; return false; }
  RoleSet next;
  auto str = [](const json& o, const char* k) { return (o.contains(k) && o[k].is_string()) ? o[k].get<std::string>() : std::string(); };
  for (const auto& e : root["roles"]) {
    if (!e.is_object()) continue;
    RoleDef d; d.code = str(e, "code"); d.label = str(e, "label"); d.roleClass = str(e, "class"); d.icon = str(e, "icon"); d.shortName = str(e, "short");
    if (d.code.empty() || next.find(d.code)) continue;
    if (!validClass(d.roleClass)) d.roleClass = "specialty";
    d.label = trimJs(d.label); if (d.label.empty()) d.label = d.code;
    if (d.shortName.empty()) d.shortName = defaultShortName(d.label);
    next.roles_.push_back(d);
  }
  if (next.roles_.empty()) { if (err) *err = "The roles list is empty."; return false; }
  if (!next.find("dps")) next.roles_.push_back(*defaults().find("dps"));   // "dps" is needed (it also means "no role")
  roles_ = next.roles_;
  return true;
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

int Assignments::dropRole(const std::string& code) {
  int n = 0;
  for (auto it = map_.begin(); it != map_.end();) {
    if (it->second == code) { tanks_.erase(it->first); it = map_.erase(it); ++n; } else ++it;
  }
  for (auto it = prefers_.begin(); it != prefers_.end();) it = (it->second == code) ? prefers_.erase(it) : std::next(it);
  return n;
}
int Assignments::dropMissingRoles(const RoleSet& roles) {
  std::set<std::string> gone;
  for (const auto& kv : map_) if (!roles.find(kv.second)) gone.insert(kv.second);
  for (const auto& kv : prefers_) if (!roles.find(kv.second)) gone.insert(kv.second);
  int n = 0;
  for (const auto& c : gone) n += dropRole(c);
  return n;
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
