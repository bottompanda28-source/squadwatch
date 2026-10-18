#include "roles.hpp"
#include <algorithm>
#include "text.hpp"
#include <nlohmann/json.hpp>

namespace sw {
using nlohmann::json;

bool RoleSet::loadFromJson(const std::string& text, std::string* err) {
  json root;
  try { root = json::parse(text); }
  catch (const std::exception& ex) { if (err) *err = std::string("Not valid JSON: ") + ex.what(); return false; }
  const json* arr = nullptr;
  if (root.is_array()) arr = &root;
  else if (root.is_object() && root.contains("roles") && root["roles"].is_array()) arr = &root["roles"];
  if (!arr) { if (err) *err = "No \"roles\" list found."; return false; }
  std::vector<RoleDef> out;
  for (const auto& r : *arr) {
    if (!r.is_object() || !r.contains("code") || !r["code"].is_string()) continue;
    RoleDef d;
    d.code = r["code"].get<std::string>();
    d.label = (r.contains("label") && r["label"].is_string()) ? r["label"].get<std::string>() : d.code;
    d.roleClass = (r.contains("class") && r["class"].is_string()) ? r["class"].get<std::string>() : "";
    if (r.contains("count") && r["count"].is_number_integer()) d.count = std::max(0, r["count"].get<int>());
    d.mandatory = r.contains("mandatory") && r["mandatory"].is_boolean() && r["mandatory"].get<bool>();
    out.push_back(d);
  }
  roles_ = out;
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

std::vector<BoardRow> buildBoard(const RoleSet& roles, const Assignments& a, const std::vector<Member>& present) {
  std::vector<BoardRow> rows;
  for (const auto& r : roles.roles()) {
    BoardRow row; row.role = r;
    for (const auto& m : present) {
      if (a.get(m.account.empty() ? m.character : m.account) == r.code)
        row.holders.push_back(m.account.empty() ? m.character : m.account);
    }
    int have = (int)row.holders.size();
    row.open = std::max(0, r.count - have);
    row.over = std::max(0, have - r.count);
    rows.push_back(row);
  }
  return rows;
}

}
