#pragma once
// Live leaves (experimental ArcDPS follow mode): a leave is not reported at once. It waits out a grace period, so a player
// who changes character or instance and comes straight back never shows up in the Changes tab. Leaves that happen close
// together (a whole squad changing instance) are reported as one entry, flagged "mass". Pure logic, time is passed in.
#include "history.hpp"
#include <string>
#include <vector>

namespace sw {

struct LeaveSettings {
  double graceSeconds = 15.0;   // how long someone must stay away before the leave is reported
  double burstGap = 3.0;        // leaves closer together than this belong to one burst (reported together)
  double burstMaxExtra = 10.0;  // a burst is reported at the latest this long after its grace period would have ended
  int massMin = 5;              // this many leavers in one burst = a mass leave (also: 3+ and a third of the squad)
};

struct LeaveBatch {
  std::vector<SnapMember> left;  // who stayed away, with the role they held when they left
  bool mass = false;
};

class LeaveTracker {
 public:
  LeaveSettings settings;

  void leave(const SnapMember& who, double now);             // a live leave was seen (replaces a pending one for the same key)
  bool cancel(const std::string& key);                       // the player came back: forget the leave silently (key = Member::key())
  size_t removeKeys(const std::vector<std::string>& keys);   // forget leaves that were reported or undone another way
  // Batches whose grace period is over. squadSize = players currently in the roster (used for the "a third of the squad" rule).
  std::vector<LeaveBatch> flush(double now, size_t squadSize);
  void clear() { pending_.clear(); }
  size_t pending() const { return pending_.size(); }

 private:
  struct Pending { SnapMember who; double at; int burst; };
  std::vector<Pending> pending_;
  int curBurst_ = 0;
  double lastAt_ = -1e9;
};

// A Changes tab entry for a flushed batch (live = true; joined stays empty).
ChangeEntry entryFromLeaves(const LeaveBatch& b, const std::string& time);

}
