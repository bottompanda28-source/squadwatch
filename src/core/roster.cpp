#include "roster.hpp"
#include "text.hpp"
#include <algorithm>

namespace sw {

std::string Member::key() const { return normKey(account.empty() ? character : account); }

std::string stripAccountPrefix(const std::string& s) {
  std::string t = trimJs(s);
  size_t i = 0;
  while (i < t.size() && t[i] == ':') ++i;
  return t.substr(i);
}

bool looksLikeAccount(const std::string& s) {
  // a letter/number, then '.', then exactly four digits (not followed by a fifth)
  std::u32string u = toU32(s);
  auto dig = [&](size_t i) { return i < u.size() && u[i] >= '0' && u[i] <= '9'; };
  for (size_t j = 1; j < u.size(); ++j) {
    if (u[j] != '.' || !isLetterOrNumber(u[j - 1])) continue;
    if (dig(j + 1) && dig(j + 2) && dig(j + 3) && dig(j + 4) && !dig(j + 5)) return true;
  }
  return false;
}

bool looksLikeUuid(const std::string& s) {
  std::string t = trimJs(s);
  if (t.size() != 36) return false;
  for (size_t i = 0; i < 36; ++i) {
    char c = t[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; }
    else if (!isxdigit((unsigned char)c)) return false;
  }
  return true;
}

std::vector<Member> parsePaste(const std::string& text) {
  std::vector<Member> out;
  bool firstLine = true;
  for (const std::string& rawLine : splitLines(text)) {
    std::string line = trimJs(rawLine);
    if (line.empty()) continue;
    // split on tab / comma / semicolon / pipe
    std::vector<std::string> fields;
    std::string cur;
    for (char c : line) {
      if (c == '\t' || c == ',' || c == ';' || c == '|') { fields.push_back(cur); cur.clear(); }
      else cur.push_back(c);
    }
    fields.push_back(cur);
    std::vector<std::string> clean;
    for (auto& f : fields) { std::string t = trimJs(f); if (!t.empty() && !looksLikeUuid(t)) clean.push_back(t); }
    if (clean.empty()) continue;
    bool anyAccount = false;
    for (auto& f : clean) if (looksLikeAccount(f)) { anyAccount = true; break; }
    if (firstLine) {
      firstLine = false;
      std::string low = lowerUtf8(clean[0]);
      if (!anyAccount && (low == "account" || low == "account name" || low == "accountname" || low == "name" || low == "member")) continue;
    }
    Member m;
    int acctField = -1;
    for (size_t i = 0; i < clean.size(); ++i) if (looksLikeAccount(clean[i])) { acctField = (int)i; break; }
    if (acctField >= 0) {
      m.account = stripAccountPrefix(clean[acctField]);
      for (size_t i = 0; i < clean.size(); ++i) if ((int)i != acctField) { m.character = clean[i]; break; }
    } else {
      m.character = clean[0];
    }
    out.push_back(m);
  }
  return out;
}

std::string formatList(const std::vector<Member>& members, const CopyOptions& opt) {
  std::vector<Member> list = members;
  if (opt.sortBySubgroup)
    std::stable_sort(list.begin(), list.end(), [](const Member& a, const Member& b) {
      int ga = a.subgroup < 0 ? 1000 : a.subgroup, gb = b.subgroup < 0 ? 1000 : b.subgroup;
      return ga < gb;
    });
  std::string out;
  for (const auto& m : list) {
    std::string row;
    if (opt.includeSubgroup) row += (m.subgroup >= 0 ? "Sub " + std::to_string(m.subgroup + 1) : std::string("Sub ?")) + opt.colSep;
    std::string acct = m.account.empty() ? m.character : m.account;
    row += (opt.colonPrefix && !m.account.empty() ? ":" : "") + acct;
    if (opt.includeCharacter && !m.account.empty() && !m.character.empty()) row += opt.colSep + m.character;
    out += row + opt.rowSep;
  }
  return out;
}

int Roster::indexOf(const std::string& key) const {
  for (size_t i = 0; i < members_.size(); ++i) if (members_[i].key() == key) return (int)i;
  return -1;
}

void Roster::join(const std::string& account, const std::string& character, int subgroup) {
  Member m; m.account = stripAccountPrefix(account); m.character = character; m.subgroup = subgroup; m.live = true;
  if (m.account.empty()) return;
  int i = indexOf(m.key());
  if (i < 0) { members_.push_back(m); return; }
  Member& e = members_[i];
  e.account = m.account; e.live = true;
  if (!character.empty()) e.character = character;   // keep the last known character when the update has none
  if (subgroup >= 0) e.subgroup = subgroup;
}

void Roster::leave(const std::string& account) {
  int i = indexOf(normKey(stripAccountPrefix(account)));
  if (i >= 0) members_.erase(members_.begin() + i);
}

void Roster::clearLive() {
  members_.erase(std::remove_if(members_.begin(), members_.end(), [](const Member& m) { return m.live; }), members_.end());
}

void Roster::mergePaste(const std::vector<Member>& pasted) {
  for (const auto& p : pasted) {
    int i = indexOf(p.key());
    if (i < 0) { Member m = p; m.live = false; members_.push_back(m); continue; }
    Member& e = members_[i];
    if (!p.account.empty()) e.account = p.account;
    if (!p.character.empty()) e.character = p.character;
  }
}

void Roster::replaceWithPaste(const std::vector<Member>& pasted) {
  members_.clear();
  mergePaste(pasted);
}

std::vector<Member> Roster::sorted() const {
  std::vector<Member> v = members_;
  std::stable_sort(v.begin(), v.end(), [](const Member& a, const Member& b) {
    int ga = a.subgroup < 0 ? 1000 : a.subgroup, gb = b.subgroup < 0 ? 1000 : b.subgroup;
    if (ga != gb) return ga < gb;
    return a.key() < b.key();
  });
  return v;
}

std::vector<Change> diffSnapshot(const Snapshot& before, const std::vector<Member>& now) {
  std::vector<Change> out;
  std::map<std::string, const Member*> prev, cur;
  for (const auto& m : before.members) prev[m.key()] = &m;
  for (const auto& m : now) cur[m.key()] = &m;
  for (const auto& m : now) {
    auto it = prev.find(m.key());
    if (it == prev.end()) { out.push_back({Change::Joined, m, Member()}); continue; }
    const Member& b = *it->second;
    if (b.subgroup >= 0 && m.subgroup >= 0 && b.subgroup != m.subgroup) out.push_back({Change::Moved, m, b});
    else if (!b.character.empty() && !m.character.empty() && normKey(b.character) != normKey(m.character)) out.push_back({Change::CharacterChanged, m, b});
  }
  for (const auto& m : before.members) if (!cur.count(m.key())) out.push_back({Change::Left, Member(), m});
  return out;
}

}
