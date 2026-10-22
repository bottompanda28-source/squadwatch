#pragma once
// Change history between squad updates: who joined, and which roles left (for the "what do I need to refill" copy).
#include "roles.hpp"
#include "roster.hpp"
#include <string>
#include <vector>

namespace sw {

struct SnapMember { Member m; std::string role; };

struct ChangeEntry {
  std::string time;                 // "HH:MM", informational
  std::vector<std::string> joined;  // display names
  std::vector<SnapMember> left;     // who left, with the role they held (empty role = unassigned)
  bool live = false;                // came from ArcDPS live leaves (after the grace period), not from an update
  bool mass = false;                // many players left at once (instance change): shown as one summary line
};

// before = snapshot (roster + roles as of the last role assignment / update), after = the new roster.
ChangeEntry diffForHistory(const std::vector<SnapMember>& before, const std::vector<Member>& after, const std::string& time);

// Role counts of everyone who left. Unassigned players count as DPS. Role order follows the RoleSet.
// display: "2 Alac DPS, 1 DPS"   clipboard: "2AlacDPS, 1DPS"
std::string leftRolesDisplay(const ChangeEntry& e, const RoleSet& roles);
std::string leftRolesClipboard(const ChangeEntry& e, const RoleSet& roles);

class ChangeLog {
 public:
  static constexpr size_t kMax = 10;
  void push(const ChangeEntry& e);     // newest first, capped at kMax
  void clear() { entries_.clear(); }
  const std::vector<ChangeEntry>& entries() const { return entries_; }
  std::string toJson() const;
  bool loadFromJson(const std::string& text);
 private:
  std::vector<ChangeEntry> entries_;
};

std::string snapshotToJson(const std::vector<SnapMember>& s);
bool snapshotFromJson(const std::string& text, std::vector<SnapMember>* out);

}
