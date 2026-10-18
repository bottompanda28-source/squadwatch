#pragma once
// Watchlist model + lookup. Port of the composer's watchlistMatch / wlPickVisibleTags.
#include <map>
#include <string>
#include <vector>

namespace sw {

struct TagMeta {
  std::string definition, phrase, status, parent;
  bool pin = false;
  std::vector<std::string> roles;  // role classes: heal, boondps, dps, specialty
};

struct WlEntry {
  std::vector<std::string> accounts, characters, sections, tags, archetypes, notes;
  std::vector<std::string> extraTags;  // values of custom categories
  std::string severity;                // very / really / pretty / mildly / ""
};

struct VisibleTag { std::string name; bool role = false, pin = false; };
struct VisibleTags { std::vector<VisibleTag> shown; std::vector<std::string> hidden; };

enum class Standing { None, Trusted, Flagged, Blocked };

class Watchlist {
 public:
  // Accepts the watchlist's JSON export ({players, guilds, tagConfig}). Returns false + error on failure.
  bool loadFromJson(const std::string& text, std::string* err = nullptr);
  size_t playerCount() const { return players_.size(); }
  const std::map<std::string, TagMeta>& tagMeta() const { return meta_; }

  // Worst-severity match over the account index then the character index; null when none.
  const WlEntry* match(const std::vector<std::string>& candidates) const;
  const WlEntry* match(const std::string& account, const std::string& character) const;

  static bool isBlocked(const WlEntry* e);
  static bool isFlagged(const WlEntry* e);
  static bool isTrusted(const WlEntry* e);
  static Standing standing(const WlEntry* e);
  static std::string severityLabel(const std::string& code);

  std::vector<std::string> entryTagNames(const WlEntry& e) const;
  VisibleTags pickVisibleTags(const WlEntry& e, const std::string& roleClass, size_t max = 3) const;
  std::string tagTitle(const std::string& name) const;
  std::string derivedText(const WlEntry& e) const;

 private:
  std::vector<WlEntry> players_;
  std::map<std::string, size_t> acctIdx_, charIdx_;
  std::map<std::string, TagMeta> meta_;
  std::vector<std::string> categoryIds_;
};

}
