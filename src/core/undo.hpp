#pragma once
// Tiny undo/redo history (default depth 2) over copyable state snapshots.
#include <deque>
#include <string>
#include <utility>

namespace sw {

template <class T>
class UndoStack {
 public:
  explicit UndoStack(size_t maxDepth = 2) : max_(maxDepth) {}

  // Call BEFORE a destructive action, passing the state as it is now. Clears the redo history.
  void record(T before, std::string label) {
    undo_.push_back({std::move(before), std::move(label)});
    while (undo_.size() > max_) undo_.pop_front();
    redo_.clear();
  }
  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  std::string undoLabel() const { return undo_.empty() ? "" : undo_.back().label; }
  std::string redoLabel() const { return redo_.empty() ? "" : redo_.back().label; }

  // current = the state right now (kept so the step can be redone). Returns the state to restore.
  bool undo(const T& current, T* restore, std::string* label = nullptr) {
    if (undo_.empty()) return false;
    Item it = std::move(undo_.back()); undo_.pop_back();
    redo_.push_back({current, it.label});
    while (redo_.size() > max_) redo_.pop_front();
    if (label) *label = it.label;
    *restore = std::move(it.state);
    return true;
  }
  bool redo(const T& current, T* restore, std::string* label = nullptr) {
    if (redo_.empty()) return false;
    Item it = std::move(redo_.back()); redo_.pop_back();
    undo_.push_back({current, it.label});
    while (undo_.size() > max_) undo_.pop_front();
    if (label) *label = it.label;
    *restore = std::move(it.state);
    return true;
  }
  void clear() { undo_.clear(); redo_.clear(); }

 private:
  struct Item { T state; std::string label; };
  std::deque<Item> undo_, redo_;
  size_t max_;
};

}
