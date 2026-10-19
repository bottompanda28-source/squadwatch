#pragma once
// Copy/paste exchange with the Raid Squad Composer: a one-line JSON text that carries who is in which
// subgroup with which role. Both tools stay standalone; the clipboard is the only link.
//
//   {"gw2squad":1,"members":[{"a":"Name.1234","c":"Character","r":"boonheal__quick","s":2,"t":1}, ...]}
//
//   a  account (the composer may only know a react name without ".1234")   c  character / display name (optional)
//   r  role code (same ids as RoleSet)   s  subgroup, 1-based (absent = none)   t  1 = tank
#include "roles.hpp"
#include "roster.hpp"
#include <string>
#include <vector>

namespace sw {

struct ExchangeMember {
  std::string account, character, role;
  int subgroup = 0;   // 1-based, 0 = none
  bool tank = false;
};

// Whole squad (everyone, with or without subgroup / role) as one compact line of JSON.
std::string buildExchange(const RoleSet& roles, const Assignments& a, const std::vector<Member>& members);
// Accepts the text with anything around it (code fences, chat noise): the first '{' to the last '}' is used.
bool parseExchange(const std::string& text, std::vector<ExchangeMember>* out, std::string* err = nullptr);

struct ExchangeReport {
  int total = 0;                       // entries in the pasted text
  int matched = 0;                     // entries that found a squad member
  int added = 0;                       // entries added because the squad list was empty
  int seated = 0;                      // members that received a subgroup
  int roleSet = 0;                     // members that received a role
  int tanks = 0;                       // members marked as tank
  std::vector<std::string> unknownRoles;   // role codes this addon does not have (the role was skipped)
  std::vector<std::string> notInSquad;     // entries that matched nobody (squad list not empty, so not added)
};

// Applies the pasted entries to the squad. Matching is by account (case-insensitive, number optional) and falls back
// to the character name. Members that are not in the pasted text are left untouched. When the squad list is empty
// everybody in the pasted text is added (the clipboard list from Squad Attendance can still replace it later).
ExchangeReport applyExchange(Roster& roster, Assignments& assign, const RoleSet& roles, const std::vector<ExchangeMember>& in);
std::string describeExchange(const ExchangeReport& r);

}
