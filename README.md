# Squad Watch (Nexus addon for Guild Wars 2)

A standalone Nexus (Raidcore) addon. It does not need Unofficial Extras.

- **Squad window**, grouped by subgroup, filled from ArcDPS squad events relayed by Nexus **and/or** a list you paste from the clipboard.
- **Watchlist markers**: BLOCKED / FLAGGED / TRUSTED, plus up to 3 tags per player (role-relevant tags first, then your pinned tags). Hover for the derived summary and notes. Uses the same matching rules as the Raid Squad Composer.
- **Roles**: click a player's role button to assign a role. The **Roles** tab shows each role's seats, who holds it, and what is still open. Assignments are saved to `assignments.json`.
- **Changes**: press **Snapshot**, take another later, and the **Changes** tab lists who joined / left / moved subgroup / switched character, with watchlist markers.
- **Copy squad**: copies `Account.1234, Character` per line (UTF-16, so emoji and non-Latin names survive). Separators, ":" prefix, subgroup column are set in Nexus options.

## Install
1. Get `squad_watch.dll` from the GitHub Actions run (Artifacts) or a tagged Release.
2. Put it in `<Guild Wars 2>\addons\`, then enable it in the Nexus library / addon list.
3. Data folder: `<Guild Wars 2>\addons\SquadWatch\` (the options page shows the exact path). Put in it:
   - `watchlist.json`: export from `gw2-watchlist.html` (the JSON export).
   - `roles.json`: composer -> role editor -> **Export roles for addon**. `samples/roles.json` shows the format.
4. Nexus options -> Keybinds: bind **Squad Watch: toggle / copy / paste / snapshot** (unbound by default so nothing clashes). The quick-access icon also toggles the window.

## Building
Push this folder to a GitHub repo. `.github/workflows/build.yml` builds the 64-bit DLL on `windows-latest`, runs the unit tests, and uploads `squad_watch.dll`. Tag `v0.1.0` to publish a Release.
Local: `cmake -S . -B build -A x64 && cmake --build build --config Release`.

## What has and has not been verified
Verified (Linux, native):
- 64 unit checks of the core (paste parsing, clipboard formatting, roster merge, diff, roles, watchlist).
- Name matching agrees with the composer's JavaScript on 9,508 names (all real names in your watchlist plus fuzzed edge cases), and standing / visible tags / derived text agree on every watchlist entry.
- The Nexus/ImGui glue compiles (syntax-checked) against the real `Nexus.h` v6 and ImGui 1.80 with a stub `windows.h`.

**Not verified: the Windows build and anything in-game.** The glue has never been linked or run. Likely first-run issues: ArcDPS event payload details, Nexus keybind handling, fonts (emoji/CJK names may show as `?` unless Nexus's font has the glyphs), and the quick-access icon.
Not in the addon: icons on the game's own squad panel (not possible from an addon); members on other maps or offline only appear via paste.
Known detail: lowercasing is per character, so a capital Greek sigma at the end of a word is matched as sigma, not final sigma.
