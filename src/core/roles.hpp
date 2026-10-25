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

  // ---- editing (the Roles tab): add / edit / remove, saved as roles.json ----
  // The composer's role id is the label lower-cased, "(notes)" removed, every run of other characters turned into "_"
  // (Kite G2 -> kite_g2); a new role gets the same id so the Squad Watch exchange lines up. The id never changes afterwards.
  static std::string slug(const std::string& label);
  static std::string defaultShortName(const std::string& label);   // the label without spaces
  static bool validClass(const std::string& c);                    // heal / boondps / dps / specialty
  static bool isProtected(const std::string& code) { return code == "dps"; }   // "dps" also means "no role" in the change log, LFG text and "Set unassigned as DPS"
  std::string uniqueCode(const std::string& label) const;          // slug, then slug_2, slug_3 ... when taken
  // def.code empty = derive it from the label. False (and *err) for an empty / duplicate name, an unknown class or a taken code.
  bool add(RoleDef def, std::string* err = nullptr);
  // Replaces everything but the code. Same checks as add.
  bool update(const std::string& code, const RoleDef& def, std::string* err = nullptr);
  bool remove(const std::string& code);                             // false for unknown roles and for the protected "dps"
  std::string toJson() const;
  bool loadFromJson(const std::string& text, std::string* err = nullptr);   // replaces the list; keeps the old one on error
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
  // A role was removed: everybody who held it becomes unassigned (their tank mark goes too: it needs a healer role),
  // and nobody prefers it any more. Returns how many players lost the role.
  int dropRole(const std::string& code);
  int dropMissingRoles(const RoleSet& roles);      // dropRole for every assigned / preferred code the set does not have
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
// LFG text for the roles still open: "1quickdps, 2alacheal, dps". Every role with need > holders gets "<missing><shortname>"
// (lower case); pure DPS never gets a quantity and is appended when fewer than `target` people (roster + open seats of the
// other roles) would be in the squad, or when the DPS role itself has open seats. Empty string = nothing to look for.
std::string lfgRolesText(const std::vector<BoardRow>& board, int rosterSize, int target = 10);
std::string copyOneRole(const RoleSet& roles, const Assignments& a, const std::vector<Member>& present, const std::string& code);

}
