// usage: node crosscheck_match.js composer.html watchlist.html match_dump workdir
const fs = require('fs'), vm = require('vm'), cp = require('child_process'), path = require('path');
const [comp, wlHtml, bin, work] = process.argv.slice(2);
const src = fs.readFileSync(comp, 'utf8');
function grab(name) {
  const i = src.indexOf('function ' + name + '(');
  if (i < 0) throw new Error('missing ' + name);
  const oneLine = src.slice(i, src.indexOf('\n', i));
  if (oneLine.split('{').length === oneLine.split('}').length && /\}\s*$/.test(oneLine)) return oneLine;
  return src.slice(i, src.indexOf('\n}\n', i) + 2);
}
const m = fs.readFileSync(wlHtml, 'utf8').match(/const SEED_DATA = (\{.*?\});\n/s);
const data = JSON.parse(m[1]);
// give the sample some role/pin metadata variety if the seed has none
const meta = (data.tagConfig && data.tagConfig.tagMeta) || {};
fs.writeFileSync(path.join(work, 'wl_export.json'), JSON.stringify(data));
const ctx = { DEFAULT_WL_SEVERITY_LABELS: { very: 'Terrible', really: 'Awful', pretty: 'Bad', mildly: 'Mediocre' } };
vm.createContext(ctx);
vm.runInContext('let watchlistData=null, watchlistAccountIndex={}, watchlistCharacterIndex={};\n' +
  ['wlNormKey', 'rebuildWatchlistIndex', 'wlSeverityLabel', 'trimNameDecor', 'wlCandidateVariants', 'watchlistMatch', 'watchlistIsBlocked', 'watchlistIsFlagged', 'watchlistIsTrusted', 'wlTagMeta', 'wlEntryTagNames', 'wlPickVisibleTags', 'wlDerivedText'].map(grab).join('\n'), ctx);
ctx.__data = data;
vm.runInContext('watchlistData = __data; rebuildWatchlistIndex();', ctx);

const names = [];
data.players.forEach(p => (p.accounts || []).concat(p.characters || []).forEach(x => names.push(x)));
['nobody.0000', 'beef.5493 / ' + (data.players[0].accounts[0] || 'x'), '(x) ' + (data.players[1].accounts[0] || 'y') + ' and zzz.1234'].forEach(x => names.push(x));
fs.writeFileSync(path.join(work, 'names.txt'), names.join('\n') + '\n');
const out = cp.execFileSync(bin, [path.join(work, 'wl_export.json'), path.join(work, 'names.txt')], { maxBuffer: 1 << 28 }).toString('utf8').split('\n');
const tagFmt = r => r.shown.map(t => t.name + (t.role ? '*' : '') + (t.pin ? '^' : '')).join(',');
let bad = 0, hit = 0;
names.forEach((n, i) => {
  const e = ctx.watchlistMatch(n);
  let exp = 'none';
  if (e) {
    hit++;
    const st = ctx.watchlistIsBlocked(e) ? 'blocked' : ctx.watchlistIsFlagged(e) ? 'flagged' : ctx.watchlistIsTrusted(e) ? 'trusted' : 'none';
    const h = ctx.wlPickVisibleTags(e, 'heal', 3), nn = ctx.wlPickVisibleTags(e, null, 3);
    const id = (e.accounts || []).length ? e.accounts[0] : 'c:' + ((e.characters || [])[0] || '');
    exp = [st, id, tagFmt(h), h.hidden.join(','), tagFmt(nn), ctx.wlDerivedText(e)].join('|');
  }
  if (exp !== out[i]) { if (bad++ < 10) console.log('MISMATCH', JSON.stringify(n), '\n js :', exp, '\n c++:', out[i]); }
});
console.log(`names ${names.length}, matched ${hit}, tagMeta entries ${Object.keys(meta).length}, mismatches ${bad}`);
process.exit(bad ? 1 : 0);
