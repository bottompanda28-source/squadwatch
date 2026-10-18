#pragma once
// Role template (exported from the composer) + assignments + role board.
#include "roster.hpp"
#include <map>
#include <string>
#include <vector>

namespace sw {

struct RoleDef {
  std::string code;       // e.g. "boonheal__quick"
  std::string label;      // e.g. "Quick Heal"
  std::string roleClass;  // heal / boondps / dps / specialty / "" (used to pick role-relevant tags)
  int count = 1;          // how many seats
  bool mandatory = false;
};

class RoleSet {
 public:
  bool loadFromJson(const std::string& text, std::string* err = nullptr);
  const std::vector<RoleDef>& roles() const { return roles_; }
  const RoleDef* find(const std::string& code) const;
  std::string classOf(const std::string& code) const;
  std::string labelOf(const std::string& code) const;
 private:
  std::vector<RoleDef> roles_;
};

class Assignments {
 public:
  void set(const std::string& account, const std::string& code);  // empty code clears
  std::string get(const std::string& account) const;
  void clear() { map_.clear(); }
  void prune(const std::vector<Member>& present);  // optional: drop people no longer in squad
  std::string toJson() const;
  bool loadFromJson(const std::string& text, std::string* err = nullptr);
  const std::map<std::string, std::string>& raw() const { return map_; }
 private:
  std::map<std::string, std::string> map_;  // normKey(account) -> role code
};

struct BoardRow {
  RoleDef role;
  std::vector<std::string> holders;  // display names of present members holding the role
  int open = 0;                      // seats still missing (count - holders), never negative
  int over = 0;                      // holders beyond the requested count
};
std::vector<BoardRow> buildBoard(const RoleSet& roles, const Assignments& a, const std::vector<Member>& present);

}
