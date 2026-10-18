// Squad Watch - Nexus (Raidcore) addon. Windows-only glue; all logic lives in src/core (tested natively).
#include <windows.h>
#include "Nexus.h"
#include "imgui.h"
#include "icon_png.hpp"
#include "role_icons.hpp"
#include "../core/roles.hpp"
#include "../core/roster.hpp"
#include "../core/text.hpp"
#include "../core/watchlist.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

using namespace sw;
namespace fs = std::filesystem;

namespace {

AddonAPI_t* gApi = nullptr;
HMODULE gModule = nullptr;
constexpr const char* kName = "Squad Watch";
constexpr const char* kWindow = "Squad Watch";
constexpr const char* kKbToggle = "KB_SQUADWATCH_TOGGLE";
constexpr const char* kKbCopy = "KB_SQUADWATCH_COPY";
constexpr const char* kKbUpdate = "KB_SQUADWATCH_UPDATE";
constexpr const char* kQa = "QA_SQUADWATCH";
constexpr const char* kTexN = "TEX_SQUADWATCH_N";
constexpr const char* kTexH = "TEX_SQUADWATCH_H";

// ---- state ----
struct Event { enum Kind { Join, Leave } kind; std::string account, character; int subgroup; bool self; };
std::mutex gMutex;               // guards gEvents (events can arrive on other threads)
std::deque<Event> gEvents;

Roster gRoster;
Watchlist gWatch;
RoleSet gRoles = RoleSet::defaults();
Assignments gAssign;
std::map<std::string, int> gNeed;      // role code -> seats wanted (0 = no target)
std::vector<Change> gChanges;          // what the last "Update from clipboard" changed
std::string gChangeSummary;
bool gUseLive = false;                 // ArcDPS live squad events (off: the clipboard list is the truth)
CopyOptions gCopy;
bool gVisible = false;
bool gShowTags = true, gShowNotes = true;
int gMaxTags = 3;
std::string gStatus;
std::string gWatchStatus;
fs::path gDir;

fs::path P(const char* f) { return gDir / fs::u8path(f); }

std::string readFile(const fs::path& p, bool* ok) {
  std::ifstream f(p, std::ios::binary);
  if (!f) { *ok = false; return ""; }
  std::stringstream ss; ss << f.rdbuf(); *ok = true;
  std::string s = ss.str();
  if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s.erase(0, 3);
  return s;
}
bool writeFile(const fs::path& p, const std::string& s) {
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f << s; return (bool)f;
}

void logI(const std::string& m) { if (gApi && gApi->Log) gApi->Log(LOGL_INFO, kName, m.c_str()); }
void alert(const std::string& m) { if (gApi && gApi->GUI_SendAlert) gApi->GUI_SendAlert(m.c_str()); }

// ---- clipboard (UTF-16, so emoji / non-Latin names survive) ----
bool clipboardWrite(const std::string& utf8) {
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (n <= 0) return false;
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (size_t)n * sizeof(wchar_t));
  if (!h) return false;
  wchar_t* d = (wchar_t*)GlobalLock(h);
  if (!d) { GlobalFree(h); return false; }
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, d, n);
  GlobalUnlock(h);
  if (!OpenClipboard(nullptr)) { GlobalFree(h); return false; }
  EmptyClipboard();
  bool ok = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
  if (!ok) GlobalFree(h);
  CloseClipboard();
  return ok;
}
std::string clipboardRead() {
  std::string out;
  if (!OpenClipboard(nullptr)) return out;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (const wchar_t* w = (const wchar_t*)GlobalLock(h)) {
      int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
      if (n > 1) { out.resize((size_t)n - 1); WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], n, nullptr, nullptr); }
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return out;
}

// ---- persistence ----
void saveAssignments() { if (!writeFile(P("assignments.json"), gAssign.toJson())) logI("could not write assignments.json"); }

void saveSquad() { writeFile(P("squad.json"), gRoster.toJson()); }
void loadSquad() { bool ok; std::string t = readFile(P("squad.json"), &ok); if (ok) gRoster.loadFromJson(t); }

void saveConfig() {
  nlohmann::json j;
  j["colonPrefix"] = gCopy.colonPrefix; j["includeCharacter"] = gCopy.includeCharacter; j["includeSubgroup"] = gCopy.includeSubgroup;
  j["sortBySubgroup"] = gCopy.sortBySubgroup; j["colSep"] = gCopy.colSep; j["rowSep"] = gCopy.rowSep;
  j["useLive"] = gUseLive; j["need"] = gNeed; j["showTags"] = gShowTags; j["showNotes"] = gShowNotes; j["maxTags"] = gMaxTags; j["visible"] = gVisible;
  writeFile(P("config.json"), j.dump(2));
}
void loadConfig() {
  bool ok; std::string t = readFile(P("config.json"), &ok);
  if (!ok) return;
  try {
    auto j = nlohmann::json::parse(t);
    auto b = [&](const char* k, bool& v) { if (j.contains(k) && j[k].is_boolean()) v = j[k].get<bool>(); };
    auto s = [&](const char* k, std::string& v) { if (j.contains(k) && j[k].is_string()) v = j[k].get<std::string>(); };
    b("colonPrefix", gCopy.colonPrefix); b("includeCharacter", gCopy.includeCharacter); b("includeSubgroup", gCopy.includeSubgroup);
    b("sortBySubgroup", gCopy.sortBySubgroup); s("colSep", gCopy.colSep); s("rowSep", gCopy.rowSep);
    b("useLive", gUseLive); b("showTags", gShowTags); b("showNotes", gShowNotes); b("visible", gVisible);
    if (j.contains("need") && j["need"].is_object())
      for (auto it = j["need"].begin(); it != j["need"].end(); ++it) if (it.value().is_number_integer()) gNeed[it.key()] = std::max(0, std::min(50, it.value().get<int>()));
    if (j.contains("maxTags") && j["maxTags"].is_number_integer()) gMaxTags = std::min(6, std::max(0, j["maxTags"].get<int>()));
  } catch (...) { logI("config.json unreadable, using defaults"); }
}

void loadWatchlist() {
  // The watchlist page saves its export as gw2-watchlist-data.json; an older name is still accepted.
  bool ok = false; std::string t, err;
  for (const char* name : {"gw2-watchlist-data.json", "watchlist.json"}) { t = readFile(P(name), &ok); if (ok) break; }
  if (!ok) { gWatchStatus = "gw2-watchlist-data.json not found in the addon folder"; return; }
  if (gWatch.loadFromJson(t, &err)) gWatchStatus = std::to_string(gWatch.playerCount()) + " players loaded";
  else gWatchStatus = err;
}
void loadAssignments() { bool ok; std::string t = readFile(P("assignments.json"), &ok); if (ok) gAssign.loadFromJson(t); }

// ---- actions ----
void drainEvents() {
  std::deque<Event> local;
  { std::lock_guard<std::mutex> lk(gMutex); local.swap(gEvents); }
  if (!gUseLive) return;                       // events are ignored unless enabled in options
  bool changed = false;
  for (const auto& e : local) {
    changed = true;
    if (e.kind == Event::Join) gRoster.join(e.account, e.character, e.subgroup);
    else if (e.self) gRoster.clearLive();
    else gRoster.leave(e.account);
  }
  if (changed) saveSquad();
}

void doCopy() {
  if (gRoster.size() == 0) { gStatus = "Nothing to copy: squad list is empty"; alert(gStatus); return; }
  if (clipboardWrite(formatList(gRoster.members(), gCopy))) gStatus = "Copied " + std::to_string(gRoster.size()) + " members to the clipboard";
  else gStatus = "Could not open the clipboard";
  alert(gStatus);
}

// The clipboard list (e.g. from Squad Attendance) is the truth: accounts missing from it have left the squad.
void doUpdate() {
  auto list = parsePaste(clipboardRead());
  if (list.empty()) { gStatus = "Clipboard has no recognisable names"; alert(gStatus); return; }
  bool first = gRoster.size() == 0;
  gChanges = gRoster.applyUpdate(list);
  gAssign.prune(gRoster.members());
  int j = 0, l = 0, c = 0;
  for (auto& ch : gChanges) { j += ch.kind == Change::Joined; l += ch.kind == Change::Left; c += ch.kind == Change::CharacterChanged; }
  gChangeSummary = first ? "First list: no earlier squad to compare with" : ("+" + std::to_string(j) + " joined, -" + std::to_string(l) + " left, " + std::to_string(c) + " changed character");
  gStatus = "Updated: " + std::to_string(gRoster.size()) + " members" + (first ? "" : " (" + gChangeSummary + ")");
  saveSquad(); saveAssignments();
  alert(gStatus);
}

void removeMember(const Member& m) {
  const std::string who = m.account.empty() ? m.character : m.account;
  gRoster.remove(who); gAssign.set(who, "");
  saveSquad(); saveAssignments();
}
void setSubgroup(const Member& m, int sg) {
  gRoster.setSubgroup(m.account.empty() ? m.character : m.account, sg);
  saveSquad();
}
void assignUnassignedDps() {
  int n = 0;
  for (const auto& m : gRoster.members()) {
    const std::string& who = m.account.empty() ? m.character : m.account;
    if (gAssign.get(who).empty()) { gAssign.set(who, "dps"); ++n; }
  }
  saveAssignments();
  gStatus = "Assigned DPS to " + std::to_string(n) + " players";
}

void kbToggle(const char*, bool rel) { if (!rel) { gVisible = !gVisible; } }
void kbCopy(const char*, bool rel) { if (!rel) doCopy(); }
void kbUpdate(const char*, bool rel) { if (!rel) doUpdate(); }

// ArcDPS squad events relayed by Nexus. May be called off the render thread: only queue here.
struct EvAgentUpdate {
  char account[64]; char character[64];
  uintptr_t id, instanceId;
  uint32_t added, target, Self, prof, elite;
  uint16_t team, subgroup;
};
void queueEvent(Event::Kind k, void* args) {
  if (!args) return;
  const EvAgentUpdate* a = (const EvAgentUpdate*)args;
  Event e; e.kind = k;
  e.account = std::string(a->account, strnlen(a->account, sizeof a->account));
  e.character = std::string(a->character, strnlen(a->character, sizeof a->character));
  e.subgroup = (int)a->subgroup; e.self = a->Self != 0;
  if (e.account.empty()) return;
  std::lock_guard<std::mutex> lk(gMutex);
  gEvents.push_back(std::move(e));
}
void onJoin(void* a) { queueEvent(Event::Join, a); }
void onLeave(void* a) { queueEvent(Event::Leave, a); }

// ---- UI ----
ImVec4 standingColor(Standing s) {
  switch (s) {
    case Standing::Blocked: return ImVec4(0.95f, 0.30f, 0.30f, 1);
    case Standing::Flagged: return ImVec4(0.95f, 0.70f, 0.20f, 1);
    case Standing::Trusted: return ImVec4(0.40f, 0.85f, 0.45f, 1);
    default: return ImVec4(1, 1, 1, 1);
  }
}
const char* standingLabel(Standing s) {
  switch (s) { case Standing::Blocked: return "BLOCKED"; case Standing::Flagged: return "FLAGGED"; case Standing::Trusted: return "TRUSTED"; default: return ""; }
}

// Role icons are registered as textures at load; fetch each frame (the texture may finish loading later).
bool drawRoleIcon(const std::string& icon, float size) {
  if (icon.empty() || !gApi) return false;
  Texture_t* t = gApi->Textures_Get(("TEX_SQUADWATCH_ROLE_" + icon).c_str());
  if (!t || !t->Resource) return false;
  ImGui::Image((ImTextureID)t->Resource, ImVec2(size, size));
  return true;
}

void drawTags(const WlEntry& e, const std::string& roleClass) {
  VisibleTags v = gWatch.pickVisibleTags(e, roleClass, (size_t)gMaxTags);
  for (size_t i = 0; i < v.shown.size(); ++i) {
    const VisibleTag& t = v.shown[i];
    ImGui::SameLine(0, i ? 8.f : 0.f);
    ImGui::TextColored(t.role ? ImVec4(0.55f, 0.80f, 1.f, 1) : (t.pin ? ImVec4(1.f, 0.85f, 0.5f, 1) : ImVec4(0.75f, 0.75f, 0.75f, 1)), "%s", t.name.c_str());
    std::string tip = gWatch.tagTitle(t.name);
    if (!tip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
  }
  if (!v.hidden.empty()) {
    ImGui::SameLine(0, 8.f);
    ImGui::TextDisabled("+%d more", (int)v.hidden.size());
    if (ImGui::IsItemHovered()) { std::string s; for (auto& h : v.hidden) s += (s.empty() ? "" : "\n") + h; ImGui::SetTooltip("%s", s.c_str()); }
  }
}

void drawMemberRow(const Member& m, int idx) {
  const std::string& who = m.account.empty() ? m.character : m.account;
  const WlEntry* e = gWatch.match(m.account, m.character);
  Standing st = Watchlist::standing(e);
  std::string role = gAssign.get(who);
  std::string cls = gRoles.classOf(role);

  ImGui::TableNextRow();
  ImGui::PushID(idx);
  ImGui::TableNextColumn();
  ImGui::TextColored(standingColor(st), "%s", who.c_str());
  if (!m.account.empty() && !m.character.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", m.character.c_str()); }
  if (e && ImGui::IsItemHovered()) {
    std::string tip = gWatch.derivedText(*e);
    if (gShowNotes) for (auto& n : e->notes) tip += (tip.empty() ? "" : "\n- ") + n;
    if (!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
  }
  ImGui::TableNextColumn();
  if (st != Standing::None) ImGui::TextColored(standingColor(st), "%s", standingLabel(st));
  if (e && gShowTags) { if (st != Standing::None) ImGui::SameLine(); drawTags(*e, cls); }

  // subgroup
  ImGui::TableNextColumn();
  std::string sgLabel = m.subgroup >= 0 ? "Sub " + std::to_string(m.subgroup + 1) : std::string("Sub -");
  if (ImGui::SmallButton((sgLabel + "##sg").c_str())) ImGui::OpenPopup("sgpick");
  if (ImGui::BeginPopup("sgpick")) {
    ImGui::TextDisabled("%s: subgroup", who.c_str());
    for (int g = 0; g < 10; ++g) {
      if (g % 5) ImGui::SameLine();
      if (ImGui::Button(std::to_string(g + 1).c_str(), ImVec2(30, 0))) { setSubgroup(m, g); ImGui::CloseCurrentPopup(); }
    }
    if (ImGui::Button("None", ImVec2(60, 0))) { setSubgroup(m, -1); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
  }

  // role
  ImGui::TableNextColumn();
  const RoleDef* rd = gRoles.find(role);
  if (rd && drawRoleIcon(rd->icon, 18.f)) ImGui::SameLine();
  std::string label = role.empty() ? "-" : gRoles.labelOf(role);
  if (ImGui::SmallButton((label + "##role").c_str())) ImGui::OpenPopup("rolepick");
  if (ImGui::BeginPopup("rolepick")) {
    ImGui::TextDisabled("%s", who.c_str());
    if (ImGui::Selectable("(none)", role.empty())) { gAssign.set(who, ""); saveAssignments(); }
    for (const auto& r : gRoles.roles()) {
      if (drawRoleIcon(r.icon, 16.f)) ImGui::SameLine();
      if (ImGui::Selectable(r.label.c_str(), role == r.code)) { gAssign.set(who, r.code); saveAssignments(); }
    }
    ImGui::EndPopup();
  }

  ImGui::TableNextColumn();
  if (ImGui::SmallButton("X##rm")) { removeMember(m); }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove from squad");
  ImGui::PopID();
}

void drawSquadTab() {
  if (ImGui::Button("Copy squad")) doCopy();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies the list to the clipboard using the format set in Nexus options");
  std::vector<Member> list = gRoster.sorted();
  if (list.empty()) { ImGui::TextDisabled("Squad is empty. Copy the list from Squad Attendance, then press Update from clipboard."); return; }
  if (ImGui::BeginTable("squad", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 2.f);
    ImGui::TableSetupColumn("Watchlist", ImGuiTableColumnFlags_WidthStretch, 2.f);
    ImGui::TableSetupColumn("Sub", ImGuiTableColumnFlags_WidthFixed, 55.f);
    ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 150.f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24.f);
    ImGui::TableHeadersRow();
    int lastGroup = -2;
    for (size_t i = 0; i < list.size(); ++i) {
      if (list[i].subgroup != lastGroup) {
        lastGroup = list[i].subgroup;
        int n = 0; for (auto& m : list) if (m.subgroup == lastGroup) ++n;
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableNextColumn();
        if (lastGroup >= 0) ImGui::Text("Subgroup %d  (%d)", lastGroup + 1, n); else ImGui::Text("No subgroup  (%d)", n);
      }
      drawMemberRow(list[i], (int)i);
    }
    ImGui::EndTable();
  }
}

void drawRolesTab() {
  auto board = buildBoard(gRoles, gAssign, gRoster.members(), gNeed);
  if (ImGui::BeginTable("board", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthStretch, 3.f);
    ImGui::TableSetupColumn("Need", ImGuiTableColumnFlags_WidthFixed, 80.f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 70.f);
    ImGui::TableHeadersRow();
    for (const auto& r : board) {
      ImGui::PushID(r.role.code.c_str());
      ImGui::TableNextRow(); ImGui::TableNextColumn();
      if (drawRoleIcon(r.role.icon, 18.f)) ImGui::SameLine();
      ImGui::Text("%s", r.role.label.c_str());
      ImGui::TableNextColumn();
      std::string s; for (auto& h : r.holders) s += (s.empty() ? "" : ", ") + h;
      if (s.empty()) ImGui::TextDisabled("-"); else ImGui::TextWrapped("%s", s.c_str());
      ImGui::TableNextColumn();
      int need = r.need;
      if (ImGui::SmallButton("-") && need > 0) { gNeed[r.role.code] = need - 1; saveConfig(); }
      ImGui::SameLine(); ImGui::Text("%d", need);
      ImGui::SameLine(); if (ImGui::SmallButton("+") && need < 50) { gNeed[r.role.code] = need + 1; saveConfig(); }
      ImGui::TableNextColumn();
      int have = (int)r.holders.size();
      if (r.need == 0) ImGui::TextDisabled("%d", have);
      else if (r.open > 0) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1), "%d/%d OPEN", have, r.need);
      else if (r.over > 0) ImGui::TextColored(ImVec4(0.6f, 0.8f, 1, 1), "%d/%d +%d", have, r.need, r.over);
      else ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1), "%d/%d ok", have, r.need);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  int unassigned = 0; for (auto& m : gRoster.members()) if (gAssign.get(m.account.empty() ? m.character : m.account).empty()) ++unassigned;
  ImGui::Text("%d squad members without a role", unassigned);
  ImGui::SameLine();
  if (ImGui::Button("Assign unassigned as DPS")) assignUnassignedDps();
  ImGui::SameLine();
  if (ImGui::Button("Clear all roles")) { gAssign.clear(); saveAssignments(); }
  ImGui::TextDisabled("Need = how many seats you want filled; 0 means no target.");
}

void drawChangesTab() {
  if (gChangeSummary.empty()) { ImGui::TextDisabled("Nothing yet. Each Update from clipboard shows here who joined or left since the previous list."); return; }
  ImGui::Text("Last update: %s", gChangeSummary.c_str());
  if (gChanges.empty()) return;
  for (const auto& c : gChanges) {
    const Member& m = (c.kind == Change::Left) ? c.before : c.now;
    std::string who = m.account.empty() ? m.character : m.account;
    Standing st = Watchlist::standing(gWatch.match(m.account, m.character));
    switch (c.kind) {
      case Change::Joined: ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1), "+ joined"); break;
      case Change::Left: ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1), "- left"); break;
      case Change::Moved: ImGui::TextColored(ImVec4(0.6f, 0.8f, 1, 1), "~ moved %d -> %d", c.before.subgroup + 1, c.now.subgroup + 1); break;
      case Change::CharacterChanged: ImGui::TextColored(ImVec4(0.6f, 0.8f, 1, 1), "~ character %s -> %s", c.before.character.c_str(), c.now.character.c_str()); break;
    }
    ImGui::SameLine(); ImGui::TextColored(standingColor(st), "%s", who.c_str());
    if (st != Standing::None) { ImGui::SameLine(); ImGui::TextColored(standingColor(st), "[%s]", standingLabel(st)); }
    std::string role = gAssign.get(who);
    if (!role.empty()) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", gRoles.labelOf(role).c_str()); }
  }
}

int countStanding(Standing s) { int n = 0; for (auto& m : gRoster.members()) if (Watchlist::standing(gWatch.match(m.account, m.character)) == s) ++n; return n; }

void renderWindow() {
  drainEvents();
  if (!gVisible) return;
  ImGui::SetNextWindowSize(ImVec2(700, 440), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(kWindow, &gVisible, ImGuiWindowFlags_None)) { ImGui::End(); return; }
  if (ImGui::Button("Update from clipboard")) doUpdate();
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replaces the squad with the list on the clipboard (Account.1234, Character per line).\nAccounts missing from it are treated as having left. Subgroups and roles are kept.");
  ImGui::SameLine(); if (ImGui::Button("Reload watchlist")) { loadWatchlist(); gStatus = "Watchlist: " + gWatchStatus; }
  ImGui::SameLine(); if (ImGui::Button("Clear list")) { gRoster.clear(); gChanges.clear(); gChangeSummary.clear(); saveSquad(); gStatus = "Squad cleared"; }
  ImGui::Text("%d members", (int)gRoster.size());
  int b = countStanding(Standing::Blocked), f = countStanding(Standing::Flagged), t = countStanding(Standing::Trusted);
  if (b) { ImGui::SameLine(); ImGui::TextColored(standingColor(Standing::Blocked), "  %d blocked", b); }
  if (f) { ImGui::SameLine(); ImGui::TextColored(standingColor(Standing::Flagged), "  %d flagged", f); }
  if (t) { ImGui::SameLine(); ImGui::TextColored(standingColor(Standing::Trusted), "  %d trusted", t); }
  if (!gStatus.empty()) { ImGui::SameLine(); ImGui::TextDisabled("  %s", gStatus.c_str()); }
  if (ImGui::BeginTabBar("swtabs")) {
    if (ImGui::BeginTabItem("Squad")) { drawSquadTab(); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Roles")) { drawRolesTab(); ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Changes")) { drawChangesTab(); ImGui::EndTabItem(); }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

void renderOptions() {
  bool dirty = false;
  ImGui::TextDisabled("Squad Watch");
  dirty |= ImGui::Checkbox("Put ':' before account names when copying", &gCopy.colonPrefix);
  dirty |= ImGui::Checkbox("Include character names when copying", &gCopy.includeCharacter);
  dirty |= ImGui::Checkbox("Include subgroup column when copying", &gCopy.includeSubgroup);
  dirty |= ImGui::Checkbox("Sort copied list by subgroup", &gCopy.sortBySubgroup);
  int sep = gCopy.colSep == "\t" ? 1 : (gCopy.colSep == "; " ? 2 : 0);
  const char* seps[] = {"Comma + space", "Tab", "Semicolon"};
  if (ImGui::Combo("Column separator", &sep, seps, 3)) { gCopy.colSep = sep == 1 ? "\t" : (sep == 2 ? "; " : ", "); dirty = true; }
  int row = gCopy.rowSep == "\n" ? 1 : 0;
  const char* rows[] = {"Windows (CRLF)", "Unix (LF)"};
  if (ImGui::Combo("Row separator", &row, rows, 2)) { gCopy.rowSep = row ? "\n" : "\r\n"; dirty = true; }
  dirty |= ImGui::Checkbox("Show watchlist tags", &gShowTags);
  dirty |= ImGui::Checkbox("Show notes in tooltips", &gShowNotes);
  dirty |= ImGui::SliderInt("Tags shown per player", &gMaxTags, 0, 6);
  dirty |= ImGui::Checkbox("Also follow ArcDPS live squad events (experimental)", &gUseLive);
  ImGui::Separator();
  ImGui::TextDisabled("Watchlist: %s", gWatchStatus.c_str());
  ImGui::TextDisabled("Data folder: %s", gDir.u8string().c_str());
  if (ImGui::Button("Reload watchlist")) loadWatchlist();
  if (dirty) saveConfig();
}

// ---- lifecycle ----
void AddonLoad(AddonAPI_t* api) {
  gApi = api;
  ImGui::SetCurrentContext((ImGuiContext*)api->ImguiContext);
  ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))api->ImguiMalloc, (void (*)(void*, void*))api->ImguiFree);

  const char* dir = api->Paths_GetAddonDirectory("SquadWatch");
  gDir = fs::u8path(dir ? dir : ".");
  std::error_code ec; fs::create_directories(gDir, ec);

  loadConfig(); loadWatchlist(); loadAssignments(); loadSquad();

  api->GUI_Register(RT_Render, renderWindow);
  api->GUI_Register(RT_OptionsRender, renderOptions);
  api->GUI_RegisterCloseOnEscape(kWindow, &gVisible);

  // "(null)" is Nexus's own spelling of "unbound": no key is claimed and no bogus "[...]" shows in tooltips.
  api->InputBinds_RegisterWithString(kKbToggle, kbToggle, "(null)");
  api->InputBinds_RegisterWithString(kKbCopy, kbCopy, "(null)");
  api->InputBinds_RegisterWithString(kKbUpdate, kbUpdate, "(null)");

  api->Textures_GetOrCreateFromMemory(kTexN, (void*)kIconNormal, sizeof kIconNormal);
  api->Textures_GetOrCreateFromMemory(kTexH, (void*)kIconHover, sizeof kIconHover);
  api->QuickAccess_Add(kQa, kTexN, kTexH, kKbToggle, "Squad Watch");
  for (const auto& ic : kRoleIcons)
    api->Textures_GetOrCreateFromMemory((std::string("TEX_SQUADWATCH_ROLE_") + ic.id).c_str(), (void*)ic.data, ic.size);

  api->Events_Subscribe("EV_ARCDPS_SQUAD_JOIN", onJoin);
  api->Events_Subscribe("EV_ARCDPS_SQUAD_LEAVE", onLeave);
  api->Events_Subscribe("EV_REPLAY_ARCDPS_SQUAD_JOIN", onJoin);
  logI("loaded");
}

void AddonUnload() {
  if (!gApi) return;
  saveConfig();
  gApi->Events_Unsubscribe("EV_ARCDPS_SQUAD_JOIN", onJoin);
  gApi->Events_Unsubscribe("EV_ARCDPS_SQUAD_LEAVE", onLeave);
  gApi->Events_Unsubscribe("EV_REPLAY_ARCDPS_SQUAD_JOIN", onJoin);
  gApi->QuickAccess_Remove(kQa);
  gApi->InputBinds_Deregister(kKbToggle); gApi->InputBinds_Deregister(kKbCopy); gApi->InputBinds_Deregister(kKbUpdate);
  gApi->GUI_DeregisterCloseOnEscape(kWindow);
  gApi->GUI_Deregister(renderOptions);
  gApi->GUI_Deregister(renderWindow);
  gApi = nullptr;
}

AddonDefinition_t gDef = {
  0x53515744, NEXUS_API_VERSION, kName, {0, 2, 0, 0}, "Squad Watch contributors",
  "Squad list with watchlist markers, subgroups, role assignment and change report. Update the squad from the clipboard.",
  AddonLoad, AddonUnload, AF_None, UP_None, nullptr
};

} // namespace

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() { return &gDef; }

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) { if (reason == DLL_PROCESS_ATTACH) gModule = h; return TRUE; }
