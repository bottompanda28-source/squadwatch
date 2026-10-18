#pragma once
// Squad roster: live ArcDPS events + pasted lists, merged by account name. Also: clipboard text, snapshots/diff.
#include <map>
#include <string>
#include <vector>

namespace sw {

struct Member {
  std::string account;    // "Name.1234", no leading ':'
  std::string character;  // may be empty (offline / invisible / paste without character)
  int subgroup = -1;      // 0-based; -1 unknown (pasted)
  bool live = false;      // seen through ArcDPS events this session
  std::string key() const;  // normalised identity: account if present, else character
};

struct CopyOptions {
  bool colonPrefix = false;        // ":Name.1234" like ArcDPS reports it
  bool includeCharacter = true;
  bool includeSubgroup = false;    // adds a leading "Sub N" column
  bool sortBySubgroup = true;
  std::string colSep = ", ";
  std::string rowSep = "\r\n";
};

std::string stripAccountPrefix(const std::string& s);
bool looksLikeAccount(const std::string& s);  // contains Name.1234
bool looksLikeUuid(const std::string& s);

std::vector<Member> parsePaste(const std::string& text);
std::string formatList(const std::vector<Member>& members, const CopyOptions& opt);

struct Snapshot { std::vector<Member> members; std::string label; };
struct Change { enum Kind { Joined, Left, Moved, CharacterChanged } kind; Member now, before; };
std::vector<Change> diffSnapshot(const Snapshot& before, const std::vector<Member>& now);

class Roster {
 public:
  void join(const std::string& account, const std::string& character, int subgroup);  // ArcDPS add/update
  void leave(const std::string& account);
  void clearLive();                                    // e.g. on squad disband
  void mergePaste(const std::vector<Member>& pasted);  // adds/updates, never removes
  void replaceWithPaste(const std::vector<Member>& pasted);
  void clear() { members_.clear(); }
  void remove(const std::string& account);             // by account (or character if no account)
  void setSubgroup(const std::string& account, int subgroup);
  // "Update from clipboard": the pasted list is the truth. Members whose account is absent have left;
  // character changes are allowed. Subgroups already assigned are kept. Returns what changed
  // (empty and no changes reported when the roster was empty before, i.e. first import).
  std::vector<Change> applyUpdate(const std::vector<Member>& pasted);
  std::string toJson() const;
  bool loadFromJson(const std::string& text);
  const std::vector<Member>& members() const { return members_; }
  std::vector<Member> sorted() const;  // by subgroup then account, unknown subgroup last
  size_t size() const { return members_.size(); }
 private:
  int indexOf(const std::string& key) const;
  std::vector<Member> members_;
};


}
