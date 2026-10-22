#include "leaves.hpp"
#include <algorithm>
#include <map>

namespace sw {

void LeaveTracker::leave(const SnapMember& who, double now) {
  const std::string key = who.m.key();
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.who.m.key() == key; }), pending_.end());
  if (curBurst_ == 0 || now - lastAt_ > settings.burstGap) ++curBurst_;
  lastAt_ = now;
  pending_.push_back({who, now, curBurst_});
}

bool LeaveTracker::cancel(const std::string& key) {
  size_t n = pending_.size();
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.who.m.key() == key; }), pending_.end());
  return pending_.size() != n;
}

size_t LeaveTracker::removeKeys(const std::vector<std::string>& keys) {
  size_t n = pending_.size();
  pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Pending& p) {
    return std::find(keys.begin(), keys.end(), p.who.m.key()) != keys.end();
  }), pending_.end());
  return n - pending_.size();
}

std::vector<LeaveBatch> LeaveTracker::flush(double now, size_t squadSize) {
  std::map<int, std::vector<size_t>> bursts;   // burst id -> indexes into pending_
  for (size_t i = 0; i < pending_.size(); ++i) bursts[pending_[i].burst].push_back(i);
  std::vector<LeaveBatch> out;
  std::vector<bool> done(pending_.size(), false);
  for (const auto& kv : bursts) {
    double first = 1e18, last = -1e18;
    for (size_t i : kv.second) { first = std::min(first, pending_[i].at); last = std::max(last, pending_[i].at); }
    // The burst is reported when its newest leaver has stayed away for the grace period (so everyone who left in the same
    // wave is reported together), or, if leaves keep trickling in, once the oldest one is overdue by burstMaxExtra.
    bool due = now - last >= settings.graceSeconds || now - first >= settings.graceSeconds + settings.burstMaxExtra;
    if (!due) continue;
    LeaveBatch b;
    for (size_t i : kv.second) { b.left.push_back(pending_[i].who); done[i] = true; }
    size_t n = b.left.size();
    b.mass = (int)n >= settings.massMin || (n >= 3 && n * 3 >= squadSize + n);
    out.push_back(std::move(b));
  }
  if (!out.empty()) {
    std::vector<Pending> keep;
    for (size_t i = 0; i < pending_.size(); ++i) if (!done[i]) keep.push_back(pending_[i]);
    pending_ = std::move(keep);
  }
  return out;
}

ChangeEntry entryFromLeaves(const LeaveBatch& b, const std::string& time) {
  ChangeEntry e; e.time = time; e.left = b.left; e.live = true; e.mass = b.mass;
  return e;
}

}
