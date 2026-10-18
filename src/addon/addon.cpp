// Squad Watch - Nexus (Raidcore) addon. Windows-only glue; all logic lives in src/core (tested natively).
#include <windows.h>
#include "Nexus.h"
#include "imgui.h"
#include "icon_png.hpp"
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
constexpr const char* kKbPaste = "KB_SQUADWATCH_PASTE";
constexpr const char* kKbSnap = "KB_SQUADWATCH_SNAPSHOT";
constexpr const char* kQa = "QA_SQUADWATCH";
constexpr const char* kTexN = "TEX_SQUADWATCH_N";
constexpr const char* kTexH = "TEX_SQUADWATCH_H";

// ---- state ----
struct Event { enum Kind { Join, Leave } kind; std::string account, character; int subgroup; bool self; };
std::mutex gMutex;               // guards gEvents (events can arrive on other threads)
std::deque<Event> gEvents;

Roster gRoster;
Watchlist gWatch;
RoleSet gRoles;
Assignments gAssign;
Snapshot gSnap;
bool gHaveSnap = false;
CopyOptions gCopy;
bool gVisible = false;
bool gShowTags = true, gShowNotes = true;
int gMaxTags = 3;
std::string gStatus;
std::string gWatchStatus, gRolesStatus;
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

void saveConfig() {
  nlohmann::json j;
  j["colonPrefix"] = gCopy.colonPrefix; j["includeCharacter"] = gCopy.includeCharacter; j["includeSubgroup"] = gCopy.includeSubgroup;
  j["sortBySubgroup"] = gCopy.sortBySubgroup; j["colSep"] = gCopy.colSep; j["rowSep"] = gCopy.rowSep;
  j["showTags"] = gShowTags; j["showNotes"] = gShowNotes; j["maxTags"] = gMaxTags; j["visible"] = gVisible;
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
    b("showTags", gShowTags); b("showNotes", gShowNotes); b("visible", gVisible);
    if (j.contains("maxTags") && j["maxTags"].is_number_integer()) gMaxTags = std::min(6, std::max(0, j["maxTags"].get<int>()));
  } catch (...) { logI("config.json unreadable, using defaults"); }
}

void loadWatchlist() {
  bool ok; std::string t = readFile(P("watchlist.json"), &ok), err;
  if (!ok) { gWatchStatus = "watchlist.json not found in the addon folder"; return; }
  if (gWatch.loadFromJson(t, &err)) gWatchStatus = std::to_string(gWatch.playerCount()) + " players loaded";
  else gWatchStatus = err;
}
void loadRoles() {
  bool ok; std::string t = readFile(P("roles.json"), &ok), err;
  if (!ok) { gRolesStatus = "roles.json not found in the addon folder"; return; }
  if (gRoles.loadFromJson(t, &err)) gRolesStatus = std::to_string(gRoles.roles().size()) + " roles loaded";
  else gRolesStatus = err;
}
void loadAssignments() { bool ok; std::string t = readFile(P("assignments.json"), &ok); if (ok) gAssign.loadFromJson(t); }

// ---- actions ----
void drainEvents() {
  std::deque<Event> local;
  { std::lock_guard<std::mutex> lk(gMutex); local.swap(gEvents); }
  for (const auto& e : local) {
    if (e.kind == Event::Join) gRoster.join(e.account, e.character, e.subgroup);
    else if (e.self) gRoster.clearLive();
    else gRoster.leave(e.account);
  }
}

void doCopy() {
  drainEvents();
  if (gRoster.size() == 0) { gStatus = "Nothing to copy: squad list is empty"; alert(gStatus); return; }
  if (clipboardWrite(formatList(gRoster.members(), gCopy))) gStatus = "Copied " + std::to_string(gRoster.size()) + " members to the clipboard";
  else gStatus = "Could not open the clipboard";
  alert(gStatus);
}
void doPaste(bool replace) {
  auto list = parsePaste(clipboardRead());
  if (list.empty()) { gStatus = "Clipboard has no recognisable names"; alert(gStatus); return; }
  if (replace) gRoster.replaceWithPaste(list); else gRoster.mergePaste(list);
  gStatus = "Imported " + std::to_string(list.size()) + " names from the clipboard";
  alert(gStatus);
}
void doSnapshot() {
  drainEvents();
  gSnap.members = gRoster.members(); gSnap.label = "snapshot"; gHaveSnap = true;
  gStatus = "Snapshot taken (" + std::to_string(gSnap.members.size()) + " members)";
  alert(gStatus);
}

void kbToggle(const char*, bool rel) { if (!rel) { gVisible = !gVisible; } }
void kbCopy(const char*, bool rel) { if (!rel) doCopy(); }
void kbPaste(const char*, bool rel) { if (!rel) doPaste(false); }
void kbSnap(const char*, bool rel) { if (!rel) doSnapshot(); }

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
  ImGui::TableNextColumn();
  ImGui::PushID(idx);
  ImGui::TextColored(standingColor(st), "%s", who.c_str());
  if (!m.account.empty() && !m.character.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", m.character.c_str()); }
  if (e && ImGui::IsItemHovered()) {
    std::string tip = gWatch.derivedText(*e);
    if (gShowNotes) for (auto& n : e->notes) tip += (tip.empty() ? "" : "\n- ") + n;
    if (!tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
  }
  ImGui::TableNextColumn();
  if (st != Standing::None) ImGui::TextColored(standingColor(st), "%s", standingLabel(st));
  if (e && gShowTags) {
    if (st != Standing::None) ImGui::SameLine();
    drawTags(*e, cls);
  }
  ImGui::TableNextColumn();
  std::string label = role.empty() ? "-" : gRoles.labelOf(role);
  if (ImGui::SmallButton((label + "##role").c_str())) ImGui::OpenPopup("rolepick");
  if (ImGui::BeginPopup("rolepick")) {
    ImGui::TextDisabled("%s", who.c_str());
    if (ImGui::Selectable("(none)", role.empty())) { gAssign.set(who, ""); saveAssignments(); }
    for (const auto& r : gRoles.roles())
      if (ImGui::Selectable(r.label.c_str(), role == r.code)) { gAssign.set(who, r.code); saveAssignments(); }
    if (gRoles.roles().empty()) ImGui::TextDisabled("No roles loaded (roles.json)");
    ImGui::EndPopup();
  }
  ImGui::PopID();
}

void drawSquadTab() {
  std::vector<Member> list = gRoster.sorted();
  if (list.empty()) { ImGui::TextDisabled("Squad is empty. Join a squad with ArcDPS running, or use Paste import."); return; }
  if (ImGui::BeginTable("squad", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 2.f);
    ImGui::TableSetupColumn("Watchlist", ImGuiTableColumnFlags_WidthStretch, 2.f);
    ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 110.f);
    ImGui::TableHeadersRow();
    int lastGroup = -2;
    for (size_t i = 0; i < list.size(); ++i) {
      if (list[i].subgroup != lastGroup) {
        lastGroup = list[i].subgroup;
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableNextColumn();
        if (lastGroup >= 0) ImGui::Text("Subgroup %d", lastGroup + 1); else ImGui::Text("Subgroup unknown");
      }
      drawMemberRow(list[i], (int)i);
    }
    ImGui::EndTable();
  }
}

void drawRolesTab() {
  if (gRoles.roles().empty()) { ImGui::TextDisabled("No roles loaded. Put roles.json (exported from the composer) in the addon folder, then press Reload."); return; }
  auto board = buildBoard(gRoles, gAssign, gRoster.members());
  if (ImGui::BeginTable("board", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Role"); ImGui::TableSetupColumn("Filled"); ImGui::TableSetupColumn("Players");
    ImGui::TableHeadersRow();
    for (const auto& r : board) {
      ImGui::TableNextRow(); ImGui::TableNextColumn();
      ImGui::Text("%s%s", r.role.label.c_str(), r.role.mandatory ? " *" : "");
      ImGui::TableNextColumn();
      int have = (int)r.holders.size();
      ImVec4 c = r.open > 0 ? (r.role.mandatory ? ImVec4(0.95f, 0.35f, 0.35f, 1) : ImVec4(0.95f, 0.75f, 0.3f, 1)) : (r.over ? ImVec4(0.6f, 0.8f, 1, 1) : ImVec4(0.45f, 0.85f, 0.5f, 1));
      ImGui::TextColored(c, "%d / %d%s", have, r.role.count, r.open > 0 ? "  OPEN" : "");
      ImGui::TableNextColumn();
      std::string s; for (auto& h : r.holders) s += (s.empty() ? "" : ", ") + h;
      ImGui::TextWrapped("%s", s.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::TextDisabled("* = mandatory");
  int unassigned = 0; for (auto& m : gRoster.members()) if (gAssign.get(m.account.empty() ? m.character : m.account).empty()) ++unassigned;
  ImGui::Text("%d squad members without a role", unassigned);
  if (ImGui::SmallButton("Clear all assignments")) { gAssign.clear(); saveAssignments(); }
}

void drawChangesTab() {
  if (!gHaveSnap) { ImGui::TextDisabled("No snapshot yet. Press \"Snapshot\" now; take another later to see who joined or left."); return; }
  auto ch = diffSnapshot(gSnap, gRoster.members());
  ImGui::Text("Compared with snapshot of %d members", (int)gSnap.members.size());
  if (ch.empty()) { ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1), "No changes"); return; }
  for (const auto& c : ch) {
    const Member& m = (c.kind == Change::Left) ? c.before : c.now;
    std::string who = m.account.empty() ? m.character : m.account;
    const WlEntry* e = gWatch.match(m.account, m.character);
    Standing st = Watchlist::standing(e);
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
  ImGui::SetNextWindowSize(ImVec2(640, 420), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(kWindow, &gVisible, ImGuiWindowFlags_None)) { ImGui::End(); return; }
  if (ImGui::Button("Copy squad")) doCopy();
  ImGui::SameLine(); if (ImGui::Button("Paste import")) doPaste(false);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reads names from the clipboard (Account.1234, Character per line) and adds them to the list");
  ImGui::SameLine(); if (ImGui::Button("Snapshot")) doSnapshot();
  ImGui::SameLine(); if (ImGui::Button("Reload files")) { loadWatchlist(); loadRoles(); loadAssignments(); gStatus = "Reloaded"; }
  ImGui::SameLine(); if (ImGui::Button("Clear list")) { gRoster.clear(); gStatus = "Roster cleared"; }
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
  ImGui::Separator();
  ImGui::TextDisabled("Watchlist: %s", gWatchStatus.c_str());
  ImGui::TextDisabled("Roles: %s", gRolesStatus.c_str());
  ImGui::TextDisabled("Data folder: %s", gDir.u8string().c_str());
  if (ImGui::Button("Reload files")) { loadWatchlist(); loadRoles(); loadAssignments(); }
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

  loadConfig(); loadWatchlist(); loadRoles(); loadAssignments();

  api->GUI_Register(RT_Render, renderWindow);
  api->GUI_Register(RT_OptionsRender, renderOptions);
  api->GUI_RegisterCloseOnEscape(kWindow, &gVisible);

  // Unbound by default so nothing clashes; bind them under Nexus > Options > Keybinds.
  Keybind_t none{0, false, false, false};
  api->InputBinds_RegisterWithStruct(kKbToggle, kbToggle, none);
  api->InputBinds_RegisterWithStruct(kKbCopy, kbCopy, none);
  api->InputBinds_RegisterWithStruct(kKbPaste, kbPaste, none);
  api->InputBinds_RegisterWithStruct(kKbSnap, kbSnap, none);

  api->Textures_GetOrCreateFromMemory(kTexN, (void*)kIconNormal, sizeof kIconNormal);
  api->Textures_GetOrCreateFromMemory(kTexH, (void*)kIconHover, sizeof kIconHover);
  api->QuickAccess_Add(kQa, kTexN, kTexH, kKbToggle, "Squad Watch");

  api->Events_Subscribe("EV_ARCDPS_SQUAD_JOIN", onJoin);
  api->Events_Subscribe("EV_ARCDPS_SQUAD_LEAVE", onLeave);
  api->Events_Subscribe("EV_REPLAY_ARCDPS_SQUAD_JOIN", onJoin);  // catch up on members who joined before we loaded
  logI("loaded");
}

void AddonUnload() {
  if (!gApi) return;
  saveConfig();
  gApi->Events_Unsubscribe("EV_ARCDPS_SQUAD_JOIN", onJoin);
  gApi->Events_Unsubscribe("EV_ARCDPS_SQUAD_LEAVE", onLeave);
  gApi->Events_Unsubscribe("EV_REPLAY_ARCDPS_SQUAD_JOIN", onJoin);
  gApi->QuickAccess_Remove(kQa);
  gApi->InputBinds_Deregister(kKbToggle); gApi->InputBinds_Deregister(kKbCopy);
  gApi->InputBinds_Deregister(kKbPaste); gApi->InputBinds_Deregister(kKbSnap);
  gApi->GUI_DeregisterCloseOnEscape(kWindow);
  gApi->GUI_Deregister(renderOptions);
  gApi->GUI_Deregister(renderWindow);
  gApi = nullptr;
}

AddonDefinition_t gDef = {
  0x53515744, NEXUS_API_VERSION, kName, {0, 1, 0, 0}, "Squad Watch contributors",
  "Squad list with watchlist markers, role assignment, change report and copy-to-clipboard. Works with ArcDPS squad events or pasted lists.",
  AddonLoad, AddonUnload, AF_None, UP_None, nullptr
};

} // namespace

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef() { return &gDef; }

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) { if (reason == DLL_PROCESS_ATTACH) gModule = h; return TRUE; }
