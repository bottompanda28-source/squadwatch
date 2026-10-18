// Cross-checks the C++ candidateVariants against the composer's real JavaScript on a large corpus.
// usage: node tools/crosscheck.js <composer.html> <watchlist.html> <variants_dump binary> <workdir>
const fs = require('fs'), vm = require('vm'), cp = require('child_process'), path = require('path');
const [comp, wl, bin, work] = process.argv.slice(2);
const src = fs.readFileSync(comp, 'utf8');
function grab(name) {
  // each function in the composer starts at column 0 and ends at the next line that is exactly "}"
  const i = src.indexOf('function ' + name + '(');
  if (i < 0) throw new Error('missing ' + name);
  const oneLine = src.slice(i, src.indexOf('\n', i));
  if (/\}\s*$/.test(oneLine) && oneLine.split('{').length === oneLine.split('}').length) return oneLine;
  const end = src.indexOf('\n}\n', i);
  return src.slice(i, end + 2);
}
const ctx = {};
vm.createContext(ctx);
vm.runInContext(['wlNormKey', 'trimNameDecor', 'wlCandidateVariants'].map(grab).join('\n'), ctx);

const corpus = [];
const wlSrc = fs.readFileSync(wl, 'utf8');
const m = wlSrc.match(/const SEED_DATA = (\{.*?\});\n/s);
if (m) { const d = JSON.parse(m[1]); d.players.forEach(p => { (p.accounts || []).concat(p.characters || []).forEach(x => corpus.push(x)); }); }
const real = corpus.length;
['zenzou.4620 (diff acc + low ap)', 'beef.5493 / beefie.7620', '╪ Sekathi.5627 ╪', ':Foo Bar.1234', '@x.1234', 'a and b', 'Sandra.1234', 'ANDY or Bob.4321', 'x - y - z', 'x – y', 'name.12345', 'name.123', '', ' ', 'a', '😀', '😀😀', 'a😀', 'İstanbul.1234', 'ǅ x.1234', 'Ünï.9999 & Ωmega.1111', '[tag] Foo.1234', '{a}{b}', '((x)', 'x)', 'a,b;c|d+e&f',
  '🌻 juno🌻𐨀 ᨐ', 'neco.9451'].forEach(x => corpus.push(x));
// deterministic fuzz
let seed = 12345; const rnd = n => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) % n;
const bits = ['a', 'Bob', 'Foo Bar.1234', 'x.0001', 'k.12345', ' ', '  ', ' / ', ' and ', ' AND ', ' or ', 'andy', 'or', ' - ', ' – ', ' — ', '(note)', '[n]', '{z}', '(', ')', '╪', '😀', '𐨀', 'İ', 'ß', 'Ǆ', 'É', 'é', ':', '@', ',', ';', '|', '+', '&', '.', '1', '9999', ' ', '　', 'Ж', '漢字', '_', '-'];
for (let i = 0; i < 6000; i++) { let s = ''; const n = 1 + rnd(8); for (let k = 0; k < n; k++) s += bits[rnd(bits.length)]; corpus.push(s); }
const lines = corpus.filter(x => !/[\r\n]/.test(x));
const inFile = path.join(work, 'corpus.txt');
fs.writeFileSync(inFile, lines.join('\n') + '\n');
const out = cp.execFileSync(bin, [inFile], { maxBuffer: 1 << 28 }).toString('utf8').split('\n');
let bad = 0;
lines.forEach((l, i) => {
  const exp = ctx.wlCandidateVariants(l).join('\x1f');
  if (exp !== out[i]) { if (bad++ < 15) console.log('MISMATCH', JSON.stringify(l), '\n  js :', JSON.stringify(exp), '\n  c++:', JSON.stringify(out[i])); }
});
console.log(`corpus ${lines.length} (real names ${real}); mismatches ${bad}`);
process.exit(bad ? 1 : 0);
