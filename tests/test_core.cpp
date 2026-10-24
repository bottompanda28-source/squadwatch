#include "../src/core/exchange.hpp"
#include "../src/core/history.hpp"
#include "../src/core/leaves.hpp"
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
  CHECK_EQ(rs.classOf("dps"), "dps"); CHECK_EQ(rs.find("dps")->icon, "crossed_swords"); CHECK_EQ(rs.find("decima_kite")->icon, "decima"); CHECK(rs.find("nope") == nullptr);
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
  a.set("Alpha.1111", "boonheal__quick"); a.setTank("Alpha.1111", true); a.set("Bravo.2222", "dps"); a.setPrefer("Bravo.2222", "kite");

  // export: everybody, including people without subgroup / role; "p" only when set
  std::string blob = buildExchange(rs, a, r.members());
  CHECK(blob.find('\n') == std::string::npos);
  CHECK(blob.find("\"gw2squad\":1") != std::string::npos && blob.find("\"p\":\"kite\"") != std::string::npos);
  CHECK(buildExchange(rs, Assignments(), r.members()).find("\"p\"") == std::string::npos);
  std::vector<ExchangeMember> back; std::string err;
  CHECK(parseExchange(blob, &back, &err) && back.size() == 4);
  if (back.size() == 4) {
    CHECK_EQ(back[0].account, "Alpha.1111"); CHECK_EQ(back[0].character, "Alpha Char"); CHECK_EQ(back[0].role, "boonheal__quick");
    CHECK(back[0].subgroup == 2 && back[0].tank);                         // 0-based 1 -> 1-based 2
    CHECK(back[2].subgroup == 0 && back[2].role.empty() && back[2].character.empty());
    CHECK_EQ(back[1].prefer, "kite");
  }

  // text around the blob (fences, chat noise) is tolerated; garbage is refused with a message
  CHECK(parseExchange("here:\n```\n" + blob + "\n```\nthanks", &back, &err) && back.size() == 4);
  CHECK(!parseExchange("Alpha.1111, Alpha Char", &back, &err) && !err.empty());
  CHECK(!parseExchange("{\"foo\":1}", &back, &err));
  CHECK(!parseExchange(blob.substr(0, blob.size() / 2) + "}", &back, &err));
  CHECK(!parseExchange("{\"gw2squad\":1,\"members\":[]}", &back, &err));

  // what the composer writes (accounts already cleaned there; a name without number is kept as a character)
  const char* fromComposer =
    "{\"gw2squad\":1,\"src\":\"composer\",\"members\":["
    "{\"a\":\"Alpha.1111\",\"r\":\"boonheal__alac\",\"s\":3,\"p\":\"kite\"},"
    "{\"a\":\"Bravo\",\"r\":\"boondps__quick\",\"s\":2},"             // no number: character fallback
    "{\"a\":\"neco.9451\",\"c\":\"juno\",\"r\":\"dps\",\"s\":2},"
    "{\"a\":\"Echo.5555\",\"r\":\"made_up_role\",\"s\":3,\"t\":1},"
    "{\"a\":\"Golf.7777\",\"r\":\"mat_tank\"},"                        // no subgroup
    "{\"a\":\"alpha.1111\",\"s\":2}]}";                                // the same player twice
  std::vector<ExchangeMember> in; CHECK(parseExchange(fromComposer, &in, &err)); CHECK(in.size() == 6);
  ExchangeReport rep = applyExchange(r, a, rs, in);      // r still holds the OLD squad: nothing has to match
  CHECK(rep.total == 6 && rep.replaced == 4 && rep.duplicates == 1);
  CHECK(r.size() == 5);                                  // Charlie / Delta are gone, the paste is the squad
  auto find = [&](const char* acct) { for (auto& m : r.members()) if (m.account == acct) return m; return Member(); };
  CHECK(find("Alpha.1111").subgroup == 2 && a.get("Alpha.1111") == "boonheal__alac");
  CHECK(find("Alpha.1111").character == "Alpha Char");   // kept: the paste had none
  CHECK(!a.isTank("Alpha.1111"));                        // old tank mark dropped with the old list
  CHECK(a.getPrefer("Alpha.1111") == "kite");
  CHECK(find("neco.9451").character == "juno" && find("neco.9451").subgroup == 1 && a.get("neco.9451") == "dps");
  CHECK(find("Golf.7777").subgroup == -1 && a.get("Golf.7777") == "mat_tank");
  CHECK(find("Echo.5555").subgroup == 2 && a.get("Echo.5555").empty() && !a.isTank("Echo.5555"));
  CHECK(rep.unknownRoles.size() == 1 && rep.unknownRoles[0] == "made_up_role");
  bool sawBravo = false;
  for (auto& m : r.members()) if (m.character == "Bravo") { sawBravo = true; CHECK(m.account.empty() && m.subgroup == 1 && a.get("Bravo") == "boondps__quick"); }
  CHECK(sawBravo);
  CHECK(a.getPrefer("Bravo.2222").empty());              // left the squad: prefers dropped
  CHECK(!describeExchange(rep).empty());

  // an empty (just cleared) list takes a paste just the same
  Roster r3; Assignments a3;
  rep = applyExchange(r3, a3, rs, in);
  CHECK(rep.replaced == 0 && r3.size() == 5 && a3.get("Alpha.1111") == "boonheal__alac");

  // tank flag: healers only
  Roster r2; Assignments a2; std::vector<ExchangeMember> t2;
  CHECK(parseExchange("{\"gw2squad\":1,\"members\":[{\"a\":\"P.1111\",\"r\":\"boonheal__quick\",\"s\":2,\"t\":1},{\"a\":\"Q.2222\",\"r\":\"dps\",\"s\":3,\"t\":1}]}", &t2, &err));
  rep = applyExchange(r2, a2, rs, t2);
  CHECK(a2.isTank("P.1111") && !a2.isTank("Q.2222") && rep.tanks == 1 && rep.tanksSkipped == 1);
  { std::string d = describeExchange(rep); CHECK(d.find("1 tank") != std::string::npos && d.find("1 tank mark skipped") != std::string::npos); }
  { Roster rn; Assignments an; std::vector<ExchangeMember> tn; CHECK(parseExchange("{\"gw2squad\":1,\"members\":[{\"a\":\"N.1\",\"s\":1}]}", &tn, &err));
    ExchangeReport rr = applyExchange(rn, an, rs, tn); CHECK(rr.tanks == 0 && rr.tanksSkipped == 0 && describeExchange(rr).find("tank") == std::string::npos); }

  // prefers: kept for a player who stays when the paste has none (a composer view without prefers), unknown codes reported
  Roster r4; Assignments a4; r4.mergePaste(parsePaste("P.1111\nQ.2222")); a4.setPrefer("P.1111", "hk"); a4.setPrefer("Q.2222", "hk");
  std::vector<ExchangeMember> t4;
  CHECK(parseExchange("{\"gw2squad\":1,\"members\":[{\"a\":\"P.1111\",\"s\":2},{\"a\":\"Z.3333\",\"p\":\"nope\"}]}", &t4, &err));
  rep = applyExchange(r4, a4, rs, t4);
  CHECK(a4.getPrefer("P.1111") == "hk" && a4.getPrefer("Q.2222").empty() && rep.unknownRoles.size() == 1);
  Assignments ar; CHECK(ar.loadFromJson(a4.toJson()) && ar.getPrefer("P.1111") == "hk");
  ar.clear(); CHECK(ar.getPrefer("P.1111") == "hk");     // Clear all roles keeps prefers
}

void testLfg() {
  RoleSet rs = RoleSet::defaults();
  auto row = [&](const char* code, int need, int have) {
    BoardRow r; r.role = *rs.find(code); r.need = need; r.open = std::max(0, need - have); return r; };
  std::vector<BoardRow> b = { row("boondps__quick", 2, 1), row("dps", 0, 0), row("hk", 1, 1) };
  CHECK_EQ(lfgRolesText(b, 6), "1quickdps, dps");           // user's example
  CHECK_EQ(lfgRolesText(b, 9), "1quickdps");                // 9 + 1 open = 10, no dps filler
  b = { row("boonheal__alac", 2, 0), row("boondps__alac", 1, 0) };
  CHECK_EQ(lfgRolesText(b, 5), "2alacheal, 1alacdps, dps"); // 5+3 < 10
  CHECK_EQ(lfgRolesText(b, 7), "2alacheal, 1alacdps");
  b = { row("dps", 3, 0) };
  CHECK_EQ(lfgRolesText(b, 10), "dps");                      // DPS role open: no quantity
  b = { row("hk", 0, 0) };
  CHECK_EQ(lfgRolesText(b, 10), "");                         // full squad, nothing open
  CHECK_EQ(lfgRolesText(b, 8), "dps");
}


static SnapMember sm(const char* account, const char* role = "") {
  SnapMember s; s.m.account = account; s.role = role; return s;
}
static size_t countBatches(const std::vector<LeaveBatch>& v) { return v.size(); }

void testLeaves() {
  // Roster helpers
  Roster r; r.join(":A.1111", "a", 2); r.join(":B.2222", "b", 0);
  Member gone; CHECK(r.leave(":a.1111", &gone)); CHECK_EQ(gone.account, "A.1111"); CHECK(gone.subgroup == 2); CHECK(r.size() == 1);
  CHECK(!r.leave(":A.1111")); CHECK(r.find("B.2222") != nullptr); CHECK(r.find(":b.2222") != nullptr); CHECK(r.find("A.1111") == nullptr);

  // A single leave is held back until the grace period is over
  LeaveTracker t;
  CHECK(t.settings.graceSeconds == 15.0);
  t.leave(sm("A.1111", "boondps__alac"), 100.0);
  CHECK(countBatches(t.flush(110.0, 9)) == 0);
  CHECK(t.pending() == 1);
  auto b = t.flush(115.0, 9);
  CHECK(b.size() == 1); CHECK(b.size() == 1 && b[0].left.size() == 1 && b[0].left[0].role == "boondps__alac" && !b[0].mass);
  CHECK(t.pending() == 0); CHECK(t.flush(200.0, 9).empty());

  // Coming back within the grace period cancels it silently (any case / colon prefix is normalised by Member::key)
  t.leave(sm("A.1111"), 0.0); t.leave(sm("B.2222"), 1.0);
  Member back; back.account = "a.1111";
  CHECK(t.cancel(back.key())); CHECK(!t.cancel(back.key()));
  auto b2 = t.flush(30.0, 9);
  CHECK(b2.size() == 1 && b2[0].left.size() == 1 && b2[0].left[0].m.account == "B.2222");

  // The same player leaving twice is one pending leave (the later time counts)
  t = LeaveTracker(); t.leave(sm("A.1111"), 0.0); t.leave(sm("A.1111"), 10.0);
  CHECK(t.pending() == 1); CHECK(t.flush(20.0, 9).empty()); CHECK(t.flush(25.0, 9).size() == 1);

  // A whole squad changing instance: one batch, flagged mass, reported once the last leaver has been away for the grace period
  t = LeaveTracker();
  for (int i = 0; i < 8; ++i) { std::string a = "P" + std::to_string(i) + ".100" + std::to_string(i); t.leave(sm(a.c_str()), 50.0 + i * 0.3); }
  CHECK(t.flush(60.0, 2).empty());                       // 8 pending, grace not over for the newest
  auto b3 = t.flush(67.2, 2);
  CHECK(b3.size() == 1 && b3[0].left.size() == 8 && b3[0].mass);
  CHECK(t.pending() == 0);

  // A few swapping characters: 2 of 10 is not a mass leave; 3 of a 6 squad is (a third); 4 of 20 is not
  t = LeaveTracker(); t.leave(sm("A.1111"), 0); t.leave(sm("B.2222"), 1);
  auto b4 = t.flush(20, 8); CHECK(b4.size() == 1 && b4[0].left.size() == 2 && !b4[0].mass);
  t = LeaveTracker(); t.leave(sm("A.1111"), 0); t.leave(sm("B.2222"), 1); t.leave(sm("C.3333"), 2);
  auto b5 = t.flush(30, 3); CHECK(b5.size() == 1 && b5[0].mass);
  t = LeaveTracker(); for (int i = 0; i < 4; ++i) { std::string a = "Q.100" + std::to_string(i); t.leave(sm(a.c_str()), i); }
  auto b6 = t.flush(30, 16); CHECK(b6.size() == 1 && b6[0].left.size() == 4 && !b6[0].mass);

  // Two separate leaves far apart are separate batches
  t = LeaveTracker(); t.leave(sm("A.1111"), 0); t.leave(sm("B.2222"), 30);
  auto b7 = t.flush(31, 9); CHECK(b7.size() == 1 && b7[0].left[0].m.account == "A.1111");
  auto b8 = t.flush(46, 9); CHECK(b8.size() == 1 && b8[0].left[0].m.account == "B.2222");

  // A trickle of leaves (each within the burst gap of the last) is still reported after grace + burstMaxExtra
  t = LeaveTracker();
  for (int i = 0; i < 20; ++i) { std::string a = "T" + std::to_string(i) + ".200" + std::to_string(i % 10); if (i >= 10) a = "T" + std::to_string(i) + ".210" + std::to_string(i % 10); t.leave(sm(a.c_str()), i * 2.0); }
  CHECK(t.flush(24.9, 5).empty());                      // oldest leave at 0, overdue only from 15 + 10
  auto b9 = t.flush(25.0, 5); CHECK(b9.size() == 1 && b9[0].left.size() == 20 && b9[0].mass);

  // Grace 0 reports on the next flush
  t = LeaveTracker(); t.settings.graceSeconds = 0; t.leave(sm("A.1111"), 5.0);
  CHECK(t.flush(5.0, 9).size() == 1);

  // removeKeys: leaves already reported by an update (or whose player is back) are dropped without a report
  t = LeaveTracker(); t.leave(sm("A.1111"), 0); t.leave(sm("B.2222"), 0.5); t.leave(sm("C.3333"), 1);
  Member ka; ka.account = "A.1111"; Member kc; kc.account = "c.3333";
  CHECK(t.removeKeys({ka.key(), kc.key()}) == 2); CHECK(t.pending() == 1);
  auto b10 = t.flush(40, 9); CHECK(b10.size() == 1 && b10[0].left.size() == 1 && b10[0].left[0].m.account == "B.2222");
  t.leave(sm("A.1111"), 0); t.clear(); CHECK(t.pending() == 0);

  // Entry + persistence of the live/mass flags (older changes.json without them still loads)
  LeaveBatch lb; lb.left = {sm("A.1111", "hk"), sm("B.2222")}; lb.mass = true;
  ChangeEntry e = entryFromLeaves(lb, "21:05");
  CHECK(e.live && e.mass && e.joined.empty() && e.left.size() == 2 && e.time == "21:05");
  ChangeLog log; log.push(e); ChangeEntry plain; plain.time = "20:00"; log.push(plain);
  ChangeLog back2; CHECK(back2.loadFromJson(log.toJson()));
  CHECK(back2.entries().size() == 2 && back2.entries()[1].live && back2.entries()[1].mass && !back2.entries()[0].live && !back2.entries()[0].mass);
  ChangeLog old; CHECK(old.loadFromJson("{\"version\":1,\"entries\":[{\"time\":\"1\",\"joined\":[],\"left\":[]}]}")); CHECK(!old.entries()[0].live && !old.entries()[0].mass);
  RoleSet rs = RoleSet::defaults(); CHECK_EQ(leftRolesClipboard(e, rs), "1DPS, 1HK");

  // My account: added when missing (valid accounts only), never duplicated, subgroup left unknown
  Roster me; me.join(":X.1000", "x", 0);
  CHECK(me.addIfMissing(":Self.4321")); CHECK(me.size() == 2); CHECK(me.find("self.4321") != nullptr && me.find("self.4321")->subgroup == -1);
  CHECK(!me.addIfMissing("Self.4321")); CHECK(!me.addIfMissing("not an account")); CHECK(!me.addIfMissing("")); CHECK(me.size() == 2);
  auto wl = withAccount(parsePaste("X.1000, x"), ":Self.4321");
  CHECK(wl.size() == 2 && wl[1].account == "Self.4321");
  CHECK(withAccount(wl, "self.4321").size() == 2); CHECK(withAccount(wl, "nonsense").size() == 2);
  // an update whose paste leaves you out keeps you when you are added to the paste first
  Roster up; up.join("Self.4321", "", -1); up.join("Y.2000", "y", 0);
  up.applyUpdate(withAccount(parsePaste("Y.2000, y"), "Self.4321")); CHECK(up.find("Self.4321") != nullptr);
}

// Simplified subgroups: a player who is not tracked yet starts in the default subgroup (index 0), everyone else keeps theirs.
void testDefaultSubgroup() {
  Roster off;                                    // default: no default subgroup
  CHECK(off.defaultSubgroup() == -1);
  off.applyUpdate(parsePaste("A.1111, a")); CHECK(off.find("A.1111")->subgroup == -1);

  Roster r; r.setDefaultSubgroup(0); CHECK(r.defaultSubgroup() == 0);
  // first list: everybody is new
  r.applyUpdate(parsePaste("A.1111, a\nB.2222, b\nC.3333, c"));
  CHECK(r.find("A.1111")->subgroup == 0 && r.find("B.2222")->subgroup == 0 && r.find("C.3333")->subgroup == 0);
  // later update: only the new player gets the default, a manual move survives, a leaver is gone
  r.setSubgroup("B.2222", 2);
  r.applyUpdate(parsePaste("A.1111, a\nB.2222, b\nD.4444, d"));
  CHECK(r.find("B.2222")->subgroup == 2);                    // manual subgroup kept
  CHECK(r.find("D.4444")->subgroup == 0);                    // new player -> subgroup 1
  CHECK(r.find("C.3333") == nullptr && r.size() == 3);
  // a player already tracked without a subgroup is not moved by an update
  Roster legacy; legacy.applyUpdate(parsePaste("L.5555, l")); CHECK(legacy.find("L.5555")->subgroup == -1);
  legacy.setDefaultSubgroup(0); legacy.applyUpdate(parsePaste("L.5555, l\nM.6666, m"));
  CHECK(legacy.find("L.5555")->subgroup == -1 && legacy.find("M.6666")->subgroup == 0);
  // a paste that already carries a subgroup keeps it
  Roster carry; carry.setDefaultSubgroup(0);
  std::vector<Member> in = parsePaste("P.7777, p\nQ.8888, q"); in[1].subgroup = 3;
  carry.applyUpdate(in); CHECK(carry.find("P.7777")->subgroup == 0 && carry.find("Q.8888")->subgroup == 3);
  // merge / replace paste (composer exchange path), live join and "My account"
  Roster m; m.setDefaultSubgroup(0);
  m.mergePaste(parsePaste("X.1000\nY.2000")); CHECK(m.find("X.1000")->subgroup == 0 && m.find("Y.2000")->subgroup == 0);
  m.replaceWithPaste(parsePaste("Z.3000")); CHECK(m.size() == 1 && m.find("Z.3000")->subgroup == 0);
  m.join("J.9000", "j", -1); CHECK(m.find("J.9000")->subgroup == 0);              // joined without a subgroup
  m.join("K.9001", "k", 4); CHECK(m.find("K.9001")->subgroup == 4);               // ArcDPS reports its own subgroup: kept
  m.join("J.9000", "j", -1); CHECK(m.find("J.9000")->subgroup == 0);              // a repeat join does not reset anything
  m.setSubgroup("J.9000", 1); m.join("J.9000", "j2", -1); CHECK(m.find("J.9000")->subgroup == 1);
  CHECK(m.addIfMissing("Self.4321") && m.find("Self.4321")->subgroup == 0);
  // the default is not part of squad.json: a reloaded squad keeps exactly what was saved
  Roster saved; saved.applyUpdate(parsePaste("S.1000")); Roster loaded; loaded.setDefaultSubgroup(0);
  CHECK(loaded.loadFromJson(saved.toJson())); CHECK(loaded.find("S.1000")->subgroup == -1);
  // composer exchange: players without a seat get the default, seated ones keep theirs
  Roster er; er.setDefaultSubgroup(0); Assignments ea; RoleSet ers = RoleSet::defaults();
  std::vector<ExchangeMember> xin(2); xin[0].account = "E.1111"; xin[0].subgroup = 3; xin[1].account = "F.2222";
  applyExchange(er, ea, ers, xin);
  CHECK(er.find("E.1111")->subgroup == 2 && er.find("F.2222")->subgroup == 0);
  // the default can be switched back
  er.setDefaultSubgroup(-5); CHECK(er.defaultSubgroup() == -1);
}

int main() {
  testText(); testParse(); testFormat(); testRoster(); testDiff(); testWatchlist(); testRoles(); testUpdate(); testNewFeatures(); testUndo(); testExchange(); testLfg(); testLeaves(); testDefaultSubgroup();
  std::printf("%d checks, %d failed\n", g_n, g_fail);
  return g_fail ? 1 : 0;
}
