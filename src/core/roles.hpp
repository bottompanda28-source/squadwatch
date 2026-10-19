#pragma once
// Role template (exported from the composer) + assignments + role board.
#include "roster.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sw {

struct RoleDef {
  std::string code;       // e.g. "boonheal__quick"
  std::string label;      // e.g. "Quick Heal"
  std::string roleClass;  // heal / boondps / dps / specialty (used to pick role-relevant tags)
  std::string icon;       // icon id (see role_icons.hpp), may be empty
  std::string shortName;  // compact form without spaces, used in the "left roles" clipboard text
};

class RoleSet {
 public:
  static RoleSet defaults();  // every role used by the composer's templates
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
  void clear() { map_.clear(); tanks_.clear(); }   // roles + tank marks only: "prefers" is a standing wish and stays
  void setPrefer(const std::string& account, const std::string& code);  // role the player would like to play; empty clears
  std::string getPrefer(const std::string& account) const;
  bool isTank(const std::string& account) const;
  void setTank(const std::string& account, bool on);
  void prune(const std::vector<Member>& present);  // optional: drop people no longer in squad
  std::string toJson() const;
  bool loadFromJson(const std::string& text, std::string* err = nullptr);
  const std::map<std::string, std::string>& raw() const { return map_; }
 private:
  std::map<std::string, std::string> map_;  // normKey(account) -> role code
  std::set<std::string> tanks_;              // normKey(account) of players marked as tank
  std::map<std::string, std::string> prefers_;  // normKey(account) -> preferred role code (a marker, nothing more)
};

struct BoardRow {
  RoleDef role;
  std::vector<std::string> holders;  // display names of present members holding the role
  int need = 0;                      // seats wanted (set by the user; 0 = no target)
  int open = 0;                      // need - holders, never negative
  int over = 0;                      // holders beyond need (only when need > 0)
};
std::vector<BoardRow> buildBoard(const RoleSet& roles, const Assignments& a, const std::vector<Member>& present,
                                 const std::map<std::string, int>& need);


// Composer-style "copy by role" text: "Name1, Name2 - Role Label; Name3 - Other Role" (account numbers removed).
// roleClass: "" = every role, otherwise heal / boondps / dps / specialty. Only assigned members are included.
std::string copyByRole(const RoleSet& roles, const Assignments& a, const std::vector<Member>& present, const std::string& roleClass);
std::string copyOneRole(const RoleSet& roles, const Assignments& a, const std::vector<Member>& present, const std::string& code);

}
