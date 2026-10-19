#pragma once
// Copy/paste exchange with the Raid Squad Composer: a one-line JSON text that carries who is in which
// subgroup with which role. Both tools stay standalone; the clipboard is the only link.
//
//   {"gw2squad":1,"members":[{"a":"Name.1234","c":"Character","r":"boonheal__quick","s":2,"t":1}, ...]}
//
//   a  account (the composer may only know a react name without ".1234")   c  character / display name (optional)
//   r  role code (same ids as RoleSet)   s  subgroup, 1-based (absent = none)   t  1 = tank
//   p  preferred role code (optional; a side that has no "prefers" simply ignores it)
#include "roles.hpp"
#include "roster.hpp"
#include <string>
#include <vector>

namespace sw {

struct ExchangeMember {
  std::string account, character, role, prefer;
  int subgroup = 0;   // 1-based, 0 = none
  bool tank = false;
};

// Whole squad (everyone, with or without subgroup / role) as one compact line of JSON.
std::string buildExchange(const RoleSet& roles, const Assignments& a, const std::vector<Member>& members);
// Accepts the text with anything around it (code fences, chat noise): the first '{' to the last '}' is used.
bool parseExchange(const std::string& text, std::vector<ExchangeMember>* out, std::string* err = nullptr);

struct ExchangeReport {
  int total = 0;                       // entries in the pasted text
  int replaced = 0;                    // members the squad list held before it was replaced
  int duplicates = 0;                  // entries skipped because the same player was listed twice
  int seated = 0;                      // members that received a subgroup
  int roleSet = 0;                     // members that received a role
  int preferSet = 0;                   // members that received a preferred role
  int tanks = 0;                       // members marked as tank
  std::vector<std::string> unknownRoles;   // role codes this addon does not have (the role was skipped)
};

// The pasted text REPLACES the squad list: everybody in it becomes a member (account when the name is an account,
// otherwise the name is kept as the character), with the pasted subgroup, role, tank mark and preferred role.
// Nothing is matched against the old list, so an empty or stale list does not matter. Roles and tank marks that were
// set before are dropped with the old list; a preferred role is kept for a player who is still in the squad and
// whose paste carries none (not every composer view has "prefers"), and a character name is kept when the paste has none.
ExchangeReport applyExchange(Roster& roster, Assignments& assign, const RoleSet& roles, const std::vector<ExchangeMember>& in);
std::string describeExchange(const ExchangeReport& r);

}
