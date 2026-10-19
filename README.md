# Squad Watch (Nexus addon for Guild Wars 2)

A standalone Nexus (Raidcore) addon. It does not need Unofficial Extras.

- **Update from clipboard**: copy the squad list with Squad Attendance (or anything that gives `Account.1234, Character` per line), press *Update from clipboard*. The pasted list is the truth: accounts missing from it are treated as having left; a changed character name is fine. Subgroups and roles you set are kept.
- **Squad tab**: players grouped by subgroup. Click *Sub* to move someone to a subgroup (1-10), click the role button to assign a role (with icon), `X` removes one player. Each player shows BLOCKED / FLAGGED / TRUSTED and up to 3 tags (role-relevant first, then your pinned tags); hover for the derived summary and notes. Same matching rules as the Raid Squad Composer. *Copy squad* copies the list back out.
- **Copying**: click a player's name to copy it without the number suffix (`Name.1234` -> `Name`). In the Squad tab, *Copy by role* (DPS / Healers / BoonDPS / Specialty / All roles) copies the composer-style text `Name1, Name2 - Role; Name3 - Role`. A subgroup picker next to those buttons (All subs / Sub N) limits the copy, because the in-game chat holds only 199 characters; the status line warns when a copy is longer. Clicking a role row in the Roles tab copies just that role.
- **Undo / Redo** (top bar): steps back through the last 2 of *Update from clipboard*, *Clear list* and *Clear all roles* (squad, roles, change log). Not kept across restarts.
- **Tank**: a *Tank* toggle appears next to a player's role when it is Quick or Alac Heal; it only marks that healer as the tank (shown as "(tank)" in the Roles tab).
- **Roles tab**: all roles built in (Quick/Alac Heal and DPS, DPS, VG Condi DPS, HK, Pusher, G1, Kite G2, Q1 Kite, Lamp, Mat Tank, Q2 Kite, Ranged DPS, Decima Kite). Set how many seats you want per role with `-`/`+` to see OPEN roles at a glance. *Assign unassigned as DPS* fills everyone without a role.
- **Changes tab**: the last 10 updates that added or removed people (*Clear change log* empties it). Each entry lists who joined, who left and which role each leaver held, plus a "Roles left" line. Leavers are compared against the squad as it was when you last assigned a role (or last updated), so a player you removed with `X` is still reported. Click an entry to copy the roles that left, e.g. `2AlacDPS, 1DPS` (unassigned leavers count as DPS).
- The squad, subgroups and roles are saved and survive restarts. Optional (off by default, Nexus options): also follow ArcDPS live squad events.

## Install
1. Get `squad_watch.dll` from the GitHub Actions run (Artifacts) or a tagged Release.
2. Put it in `<Guild Wars 2>\addons\`, then enable it in the Nexus library / addon list.
3. Put `gw2-watchlist-data.json` (the JSON export from `gw2-watchlist.html`, saved under that name by default) in `<Guild Wars 2>\addons\SquadWatch\` (the options page shows the exact path). Press *Reload watchlist* after replacing it.
4. Nexus options -> Keybinds: bind **Squad Watch: toggle / copy / update** if you want hotkeys (unbound by default). The quick-access icon toggles the window.

## Building
Push this folder to a GitHub repo. `.github/workflows/build.yml` builds the 64-bit DLL on `windows-latest`, runs the unit tests, and uploads `squad_watch.dll`. Tag `v0.1.0` to publish a Release.
Local: `cmake -S . -B build -A x64 && cmake --build build --config Release`.

## What has and has not been verified
Verified (Linux, native):
- 114 unit checks of the core (paste parsing, clipboard formatting, roster update/remove, roles, watchlist).
- Name matching agrees with the composer's JavaScript on 9,508 names (all real names in your watchlist plus fuzzed edge cases), and standing / visible tags / derived text agree on every watchlist entry.
- The Nexus/ImGui glue compiles (syntax-checked) against the real `Nexus.h` v6 and ImGui 1.80 with a stub `windows.h`.

**Not verified: the Windows build and anything in-game.** The glue has never been linked or run. Likely first-run issues: ArcDPS event payload details, Nexus keybind handling, fonts (emoji/CJK names may show as `?` unless Nexus's font has the glyphs), and the quick-access icon.
Not in the addon: icons on the game's own squad panel (not possible from an addon).
Known detail: lowercasing is per character, so a capital Greek sigma at the end of a word is matched as sigma, not final sigma.
