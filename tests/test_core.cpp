#include "../src/core/exchange.hpp"
#include "../src/core/history.hpp"
#include "../src/core/match.hpp"
#include "../src/core/roles.hpp"
#include "../src/core/undo.hpp"
#include "../src/core/roster.hpp"
#include "../src/core/text.hpp"
#include "../src/core/watchlist.hpp"
#include <cstdio>
#include <string>

static int g_fail = 0, g_n = 0;
#define CHECK(c) do { ++g_n; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { ++g_n; std::string _a = (a), _b = (b); if (_a != _b) { ++g_fail; std::printf("FAIL %s:%d\n  got:      [%s]\n  expected: [%s]\n", __FILE__, __LINE__, _a.c_str(), _b.c_str()); } } while (0)
using namespace sw;

static void testText() {
  CHECK_EQ(lowerUtf8("ÉCOLE.1234"), "école.1234");
  CHECK_EQ(lowerUtf8("juno\xF0\x9F\x8C\xBB\xF0\x90\xA8\x80 \xE1\xA8\x90"), "juno\xF0\x9F\x8C\xBB\xF0\x90\xA8\x80 \xE1\xA8\x90");
  CHECK_EQ(trimJs("  a b \t"), "a b");
  CHECK(isLetterOrNumber(U'a') && isLetterOrNumber(U'9') && isLetterOrNumber(0x6F22) && !isLetterOrNumber(U'.') && !isLetterOrNumber(0x1F33B));
  CHECK_EQ(toUtf8(toU32("\xF0\x9F\x8C\xBB x")), "\xF0\x9F\x8C\xBB x");
  CHECK(splitLines("a\r\nb\nc\rd").size() == 4);
}

static void testParse() {
  auto v = parsePaste("Account Name, Character\r\n:Foo Bar.1234, Fooey\r\n:Solo.9999\r\n\r\nChar Only\r\nTwo.5555\tTabbed Char\t3f2504e0-4f89-11d3-9a0c-0305e82c3301\r\n");
  CHECK(v.size() == 4);
  if (v.size() == 4) {
    CHECK_EQ(v[0].account, "Foo Bar.1234"); CHECK_EQ(v[0].character, "Fooey");
    CHECK_EQ(v[1].account, "Solo.9999");    CHECK_EQ(v[1].character, "");
    CHECK_EQ(v[2].account, "");             CHECK_EQ(v[2].character, "Char Only");
    CHECK_EQ(v[3].account, "Two.5555");     CHECK_EQ(v[3].character, "Tabbed Char");
  }
  auto r = parsePaste("Fooey, Foo.1234");   // reversed column order still finds the account
  CHECK(r.size() == 1 && r[0].account == "Foo.1234" && r[0].character == "Fooey");
  CHECK(parsePaste("Account\nA.1111").size() == 1);
  CHECK(!looksLikeAccount("name.12345")); CHECK(looksLikeAccount("name.1234")); CHECK(!looksLikeAccount(".1234"));
}

static void testFormat() {
  std::vector<Member> ms(3);
  ms[0].account = "B.2222"; ms[0].character = "Beta"; ms[0].subgroup = 1;
  ms[1].account = "A.1111"; ms[1].character = "Alpha"; ms[1].subgroup = 0;
  ms[2].account = "C.3333"; ms[2].subgroup = 0;
  CopyOptions o;
  CHECK_EQ(formatList(ms, o), "A.1111, Alpha\r\nC.3333\r\nB.2222, Beta\r\n");
  o.colonPrefix = true; o.includeCharacter = false; o.rowSep = "\n";
  CHECK_EQ(formatList(ms, o), ":A.1111\n:C.3333\n:B.2222\n");
  o = CopyOptions(); o.includeSubgroup = true; o.colSep = "\t";
  CHECK_EQ(formatList(ms, o), "Sub 1\tA.1111\tAlpha\r\nSub 1\tC.3333\r\nSub 2\tB.2222\tBeta\r\n");
  // emoji survive a round trip through format -> parse
  std::vector<Member> e(1); e[0].account = "Neco.9451"; e[0].character = "juno\xF0\x9F\x8C\xBB\xF0\x90\xA8\x80 \xE1\xA8\x90";
  auto back = parsePaste(formatList(e, CopyOptions()));
  CHECK(back.size() == 1 && back[0].character == e[0].character && back[0].account == "Neco.9451");
}

static void testRoster() {
  Roster r;
  r.join(":Foo.1111", "Fooey", 0); r.join(":Bar.2222", "", 1); r.join(":foo.1111", "", 2);  // update w/o character keeps old one
  CHECK(r.size() == 2);
  CHECK_EQ(r.sorted()[0].account, "Bar.2222"); CHECK_EQ(r.sorted()[1].character, "Fooey"); CHECK(r.sorted()[1].subgroup == 2);
  r.leave(":BAR.2222"); CHECK(r.size() == 1);
  r.mergePaste(parsePaste("Baz.3333, Bazzy\nFoo.1111, Newchar"));
  CHECK(r.size() == 2);
  bool sawNew = false; for (auto& m : r.members()) if (m.account == "Foo.1111" && m.character == "Newchar") sawNew = true;
  CHECK(sawNew);
  r.clearLive(); CHECK(r.size() == 1);   // paste-only member remains
  r.replaceWithPaste(parsePaste("X.1000\nY.2000")); CHECK(r.size() == 2);
}

static void testDiff() {
  Roster r; r.join(":A.1111", "a", 0); r.join(":B.2222", "b", 0); r.join(":C.3333", "c", 1);
  Snapshot s; s.members = r.members();
  r.leave(":B.2222"); r.join(":D.4444", "d", 0); r.join(":C.3333", "c", 2); r.join(":A.1111", "a2", 0);
  auto ch = diffSnapshot(s, r.members());
  int joined = 0, left = 0, moved = 0, charc = 0;
  for (auto& c : ch) { joined += c.kind == Change::Joined; left += c.kind == Change::Left; moved += c.kind == Change::Moved; charc += c.kind == Change::CharacterChanged; }
  CHECK(joined == 1 && left == 1 && moved == 1 && charc == 1);
}

static const char* kWl = R"({"players":[
 {"accounts":["Bad.1111"],"characters":["Badchar"],"sections":["blocklist"],"severity":"very","tags":["Toxic","Sus: leeching"],"archetypes":[],"notes":["n"]},
 {"accounts":["Flag.2222"],"sections":["badplayers"],"severity":"mildly","tags":["Late"],"archetypes":["Bad tank"]},
 {"accounts":["Good.3333"],"sections":["trusted"],"tags":["Reliable"],"archetypes":["Great healer"],"mood":["Chill"]},
 {"accounts":["Both.4444"],"sections":["trusted","badplayers"],"tags":[]}
 ],"tagConfig":{"categories":[{"id":"mood"}],"tagMeta":{
  "Sus: leeching":{"parent":"Sus","phrase":"leeches","roles":["dps"]},
  "Sus":{"phrase":"is sus"},
  "Reliable":{"phrase":"reliable","pin":true},
  "Great healer":{"phrase":"great healer","roles":["heal"]},
  "Late":{"definition":"Shows up late","status":"draft"}}}})";

static void testWatchlist() {
  Watchlist w; std::string err;
  CHECK(w.loadFromJson(kWl, &err)); CHECK(w.playerCount() == 4);
  CHECK(!w.loadFromJson("{nope", &err)); CHECK(!err.empty());
  CHECK(!w.loadFromJson("{\"x\":1}", &err));
  CHECK(w.loadFromJson(kWl, &err));
  CHECK(Watchlist::standing(w.match(":Bad.1111", "")) == Standing::Blocked);
  CHECK(Watchlist::standing(w.match("Nobody.9", "badchar")) == Standing::Blocked);       // character fallback
  CHECK(Watchlist::standing(w.match("flag.2222 (alt)", "")) == Standing::Flagged);       // decorated name
  CHECK(Watchlist::standing(w.match("Good.3333 / Bad.1111", "")) == Standing::Blocked);  // worst wins
  CHECK(Watchlist::standing(w.match("Good.3333", "")) == Standing::Trusted);
  CHECK(Watchlist::standing(w.match("Both.4444", "")) == Standing::Flagged);             // negative beats trusted
  CHECK(w.match("Unknown.0000", "") == nullptr);
  const WlEntry* g = w.match("Good.3333", "");
  auto v = w.pickVisibleTags(*g, "heal");
  CHECK(v.shown.size() == 4 - 1 || v.shown.size() == 3);   // Reliable, Great healer, Chill (3 max)
  CHECK(v.shown[0].name == "Great healer" && v.shown[0].role);
  CHECK(v.shown[1].name == "Reliable" && v.shown[1].pin);
  const WlEntry* b = w.match("Bad.1111", "");
  auto bv = w.pickVisibleTags(*b, "");
  bool hasParent = false; for (auto& t : bv.shown) if (t.name == "Sus") hasParent = true;
  CHECK(!hasParent);                                      // parent hidden when child present
  CHECK_EQ(w.derivedText(*b), "Terrible overall \xC2\xB7 toxic; leeches");
  CHECK_EQ(w.tagTitle("Late"), "Shows up late\nTag status: draft");
}

static void testRoles() {
  RoleSet rs = RoleSet::defaults();
  CHECK(rs.roles().size() == 16);
  CHECK_EQ(rs.classOf("boonheal__quick"), "heal"); CHECK_EQ(rs.labelOf("kite"), "Q1 Kite"); CHECK_EQ(rs.labelOf("decima_kite"), "Decima Kite");
  CHECK_EQ(rs.classOf("dps"), "dps"); CHECK_EQ(rs.find("dps")->icon, "crossed_swords"); CHECK(rs.find("nope") == nullptr);
  Assignments a; a.set(":Foo.1111", "dps"); a.set("bar.2222", "dps"); a.set("baz.3333", "dps"); a.set("Foo.1111", "boonheal__quick"); a.set("qux.4444", "dps"); a.set("qux.4444", "");
  CHECK_EQ(a.get("FOO.1111"), "boonheal__quick");
  std::vector<Member> present(2); present[0].account = "Foo.1111"; present[1].account = "Bar.2222";
  std::map<std::string, int> need{{"boonheal__quick", 1}, {"dps", 3}};
  auto board = buildBoard(rs, a, present, need);
  CHECK(board.size() == 16 && board[0].open == 0 && board[0].holders.size() == 1);
  int dpsRow = -1; for (size_t i = 0; i < board.size(); ++i) if (board[i].role.code == "dps") dpsRow = (int)i;
  CHECK(dpsRow >= 0 && board[dpsRow].holders.size() == 1 && board[dpsRow].open == 2);
  CHECK(board[1].need == 0 && board[1].open == 0);            // no target -> never "open"
  Assignments b; CHECK(b.loadFromJson(a.toJson())); CHECK_EQ(b.get("bar.2222"), "dps"); CHECK(b.raw().size() == a.raw().size());
  a.prune(present); CHECK(a.raw().size() == 2);
}

static void testUpdate() {
  Roster r;
  auto first = r.applyUpdate(parsePaste("A.1111, a\nB.2222, b\nC.3333, c"));
  CHECK(first.empty() && r.size() == 3);                      // first import reports no changes
  r.setSubgroup("B.2222", 1); r.setSubgroup("c.3333", 0);
  auto ch = r.applyUpdate(parsePaste(":A.1111, a-renamed\n:B.2222, b\nD.4444, d\nA.1111, dupe"));
  int joined = 0, left = 0, charc = 0;
  for (auto& c : ch) { joined += c.kind == Change::Joined; left += c.kind == Change::Left; charc += c.kind == Change::CharacterChanged; }
  CHECK(joined == 1 && left == 1 && charc == 1);
  CHECK(r.size() == 3);                                       // C left, D joined, duplicate ignored
  int sub = -9; for (auto& m : r.members()) if (m.account == "B.2222") sub = m.subgroup;
  CHECK(sub == 1);                                            // manual subgroup survives an update
  bool aRenamed = false; for (auto& m : r.members()) if (m.account == "A.1111" && m.character == "a-renamed") aRenamed = true;
  CHECK(aRenamed);
  r.remove("d.4444"); CHECK(r.size() == 2);
  Roster r2; CHECK(r2.loadFromJson(r.toJson())); CHECK(r2.size() == 2);
  bool kept = false; for (auto& m : r2.members()) if (m.account == "B.2222" && m.subgroup == 1) kept = true;
  CHECK(kept); CHECK(!r2.loadFromJson("{oops"));
}

static void testNewFeatures() {
  CHECK_EQ(stripAccountSuffix(":Foo Bar.1234"), "Foo Bar"); CHECK_EQ(stripAccountSuffix("neco.9451 (alt)"), "neco"); CHECK_EQ(stripAccountSuffix("NoDigits"), "NoDigits");
  RoleSet rs = RoleSet::defaults();
  // order requested by the user: HK right after VG Condi DPS, Pusher before G1
  std::vector<std::string> order; for (auto& r : rs.roles()) order.push_back(r.code);
  auto idx = [&](const char* c) { for (size_t i = 0; i < order.size(); ++i) if (order[i] == c) return (int)i; return -1; };
  CHECK(idx("hk") == idx("vg_condi_dps") + 1); CHECK(idx("pusher") + 1 == idx("g1"));

  Assignments a; std::vector<Member> pres(5);
  const char* names[] = {"A.1111", "B.2222", "C.3333", "D.4444", "E.5555"};
  for (int i = 0; i < 5; ++i) pres[i].account = names[i];
  a.set("A.1111", "boonheal__alac"); a.set("B.2222", "boonheal__alac"); a.set("C.3333", "dps"); a.set("D.4444", "pusher");
  CHECK_EQ(copyByRole(rs, a, pres, "heal"), "A, B - Alac Heal");
  CHECK_EQ(copyByRole(rs, a, pres, ""), "A, B - Alac Heal; C - DPS; D - Pusher");
  CHECK_EQ(copyByRole(rs, a, pres, "boondps"), "");
  CHECK_EQ(copyOneRole(rs, a, pres, "pusher"), "D - Pusher");
  a.setTank("a.1111", true); CHECK(a.isTank("A.1111")); CHECK(!a.isTank("B.2222"));
  Assignments b; CHECK(b.loadFromJson(a.toJson())); CHECK(b.isTank(":A.1111"));
  std::vector<Member> two(1); two[0].account = "B.2222"; b.prune(two); CHECK(!b.isTank("A.1111"));

  // history: 2 Alac DPS + 1 unassigned leave -> "2AlacDPS, 1DPS"
  std::vector<SnapMember> snap;
  auto add = [&](const char* acct, const char* role) { SnapMember s; s.m.account = acct; s.role = role; snap.push_back(s); };
  add("Keep.1", "dps"); add("P.1111", "boondps__alac"); add("Q.2222", "boondps__alac"); add("U.3333", "");
  std::vector<Member> after(2); after[0].account = "Keep.1"; after[1].account = "New.9999";
  ChangeEntry e = diffForHistory(snap, after, "12:00");
  CHECK(e.joined.size() == 1 && e.joined[0] == "New.9999" && e.left.size() == 3);
  CHECK_EQ(leftRolesClipboard(e, rs), "2AlacDPS, 1DPS");
  CHECK_EQ(leftRolesDisplay(e, rs), "2 Alac DPS, 1 DPS");
  ChangeLog log; for (int i = 0; i < 13; ++i) { ChangeEntry x; x.time = std::to_string(i); log.push(x); }
  CHECK(log.entries().size() == 10 && log.entries()[0].time == "12");
  ChangeLog log2; CHECK(log2.loadFromJson(log.toJson())); CHECK(log2.entries().size() == 10);
  log.push(e); ChangeLog log3; CHECK(log3.loadFromJson(log.toJson())); CHECK(log3.entries()[0].left.size() == 3 && log3.entries()[0].left[0].role == "boondps__alac");
  log.clear(); CHECK(log.entries().empty());
  std::vector<SnapMember> rt; CHECK(snapshotFromJson(snapshotToJson(snap), &rt)); CHECK(rt.size() == 4 && rt[1].role == "boondps__alac");
}

static void testUndo() {
  UndoStack<int> u(2); int cur = 0, out = -1; std::string lab;
  CHECK(!u.canUndo() && !u.canRedo() && !u.undo(cur, &out));
  u.record(cur, "a"); cur = 1; u.record(cur, "b"); cur = 2; u.record(cur, "c"); cur = 3;   // depth 2: "a" is dropped
  CHECK(u.canUndo() && u.undoLabel() == "c");
  CHECK(u.undo(cur, &out, &lab) && out == 2 && lab == "c"); cur = out;
  CHECK(u.undo(cur, &out, &lab) && out == 1 && lab == "b"); cur = out;
  CHECK(!u.undo(cur, &out));                                  // only two steps
  CHECK(u.canRedo() && u.redoLabel() == "b");
  CHECK(u.redo(cur, &out, &lab) && out == 2); cur = out;
  CHECK(u.redo(cur, &out) && out == 3); cur = out;
  CHECK(!u.canRedo());
  u.undo(cur, &out); cur = out; u.record(cur, "d"); CHECK(!u.canRedo());   // new action clears redo
  u.clear(); CHECK(!u.canUndo());
}

static void testExchange() {
  RoleSet rs = RoleSet::defaults();
  Roster r; Assignments a;
  r.mergePaste(parsePaste("Alpha.1111, Alpha Char\nBravo.2222, Bravo Char\nCharlie.3333\nDelta.4444, Delta Char"));
  r.setSubgroup("Alpha.1111", 1); r.setSubgroup("Bravo.2222", 2);   // Charlie / Delta: no subgroup
  a.set("Alpha.1111", "boonheal__quick"); a.setTank("Alpha.1111", true); a.set("Bravo.2222", "dps");

  // export: everybody, including people without subgroup / role
  std::string blob = buildExchange(rs, a, r.members());
  CHECK(blob.find('\n') == std::string::npos);
  CHECK(blob.find("\"gw2squad\":1") != std::string::npos);
  std::vector<ExchangeMember> back; std::string err;
  CHECK(parseExchange(blob, &back, &err));
  CHECK(back.size() == 4);
  if (back.size() == 4) {
    CHECK_EQ(back[0].account, "Alpha.1111"); CHECK_EQ(back[0].character, "Alpha Char"); CHECK_EQ(back[0].role, "boonheal__quick");
    CHECK(back[0].subgroup == 2 && back[0].tank);                         // 0-based 1 -> 1-based 2
    CHECK(back[2].subgroup == 0 && back[2].role.empty() && back[2].character.empty());
  }

  // text around the blob (fences, chat noise) is tolerated; garbage is refused with a message
  CHECK(parseExchange("here:\n```\n" + blob + "\n```\nthanks", &back, &err) && back.size() == 4);
  CHECK(!parseExchange("Alpha.1111, Alpha Char", &back, &err) && !err.empty());
  CHECK(!parseExchange("{\"foo\":1}", &back, &err));
  CHECK(!parseExchange(blob.substr(0, blob.size() / 2) + "}", &back, &err));
  CHECK(!parseExchange("{\"gw2squad\":1,\"members\":[]}", &back, &err));

  // what the composer writes: react names, with or without the number, subgroup numbers 2/3, an unknown role
  const char* fromComposer =
    "{\"gw2squad\":1,\"src\":\"composer\",\"members\":["
    "{\"a\":\"alpha.1111\",\"r\":\"boonheal__alac\",\"s\":3},"        // same account, other case
    "{\"a\":\"Bravo\",\"r\":\"boondps__quick\",\"s\":2},"             // no number
    "{\"a\":\"Delta Char\",\"r\":\"dps\",\"s\":2},"                   // character name instead of account
    "{\"a\":\"Charlie.9999\",\"r\":\"hk\",\"s\":2},"                  // wrong number: a different account
    "{\"a\":\"Zed.5555\",\"r\":\"made_up_role\",\"s\":3},"            // not in the squad
    "{\"a\":\"Echo\",\"r\":\"made_up_role\",\"s\":3,\"t\":1},"
    "{\"a\":\"Charlie.3333\",\"r\":\"made_up_role\"}]}";           // matched, but this addon has no such role
  std::vector<ExchangeMember> in; CHECK(parseExchange(fromComposer, &in, &err)); CHECK(in.size() == 7);
  ExchangeReport rep = applyExchange(r, a, rs, in);
  CHECK(rep.total == 7 && rep.matched == 4 && rep.added == 0);
  auto find = [&](const char* acct) { for (auto& m : r.members()) if (m.account == acct) return m; return Member(); };
  CHECK(find("Alpha.1111").subgroup == 2 && a.get("Alpha.1111") == "boonheal__alac");
  CHECK(a.isTank("Alpha.1111"));                                          // kept: still a healer and the paste did not clear it
  CHECK(find("Bravo.2222").subgroup == 1 && a.get("Bravo.2222") == "boondps__quick");
  CHECK(find("Delta.4444").subgroup == 1 && a.get("Delta.4444") == "dps"); // matched through the character name
  CHECK(find("Charlie.3333").subgroup == -1 && a.get("Charlie.3333").empty());
  CHECK(rep.notInSquad.size() == 3);                                       // Charlie.9999, Zed.5555, Echo
  CHECK(rep.unknownRoles.size() == 1 && rep.unknownRoles[0] == "made_up_role");
  CHECK(!describeExchange(rep).empty());

  // tank flag: applied to healers only; a non-heal role drops an old tank mark
  Roster r2; Assignments a2;
  r2.mergePaste(parsePaste("P.1111\nQ.2222"));
  std::vector<ExchangeMember> t2;
  CHECK(parseExchange("{\"gw2squad\":1,\"members\":[{\"a\":\"P.1111\",\"r\":\"boonheal__quick\",\"s\":2,\"t\":1},{\"a\":\"Q.2222\",\"r\":\"dps\",\"s\":3,\"t\":1}]}", &t2, &err));
  rep = applyExchange(r2, a2, rs, t2);
  CHECK(a2.isTank("P.1111") && !a2.isTank("Q.2222") && rep.tanks == 1);

  // empty squad list: the pasted squad becomes the list
  Roster r3; Assignments a3;
  rep = applyExchange(r3, a3, rs, in);
  CHECK(r3.size() == 7 && rep.added == 7 && rep.matched == 7 && rep.notInSquad.empty());
  CHECK(a3.get("alpha.1111") == "boonheal__alac");
  bool sawBravo = false; for (auto& m : r3.members()) if (m.character == "Bravo") { sawBravo = true; CHECK(m.account.empty() && m.subgroup == 1); }
  CHECK(sawBravo);

  // two entries never claim the same member
  Roster r4; Assignments a4;
  r4.mergePaste(parsePaste("Same.1111, X"));
  std::vector<ExchangeMember> t4;
  CHECK(parseExchange("{\"gw2squad\":1,\"members\":[{\"a\":\"Same\",\"r\":\"dps\",\"s\":2},{\"a\":\"Same.1111\",\"r\":\"hk\",\"s\":3}]}", &t4, &err));
  rep = applyExchange(r4, a4, rs, t4);
  CHECK(rep.matched == 1 && a4.get("Same.1111") == "hk" && r4.members()[0].subgroup == 2);   // exact account text wins
}

int main() {
  testText(); testParse(); testFormat(); testRoster(); testDiff(); testWatchlist(); testRoles(); testUpdate(); testNewFeatures(); testUndo(); testExchange();
  std::printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
