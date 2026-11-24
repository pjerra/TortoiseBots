// Paste into the DevTools console of any https://www.aotc.gg/forever/... page (a real browser: the site's
// bot check blocks plain HTTP clients, and this script must not be used to get around it).
// It opens each spec page in a hidden same-origin frame, reads it and POSTs one JSON file per page to
// forever/tools/aotc/recv.py on 127.0.0.1:8799. Keep the tab in front: Chrome slows background tabs.
// About an hour in all: classes, census, stats and Legacy Points pages, then talents (27 specs x 7 level groups), stat priority, gear and enchants.
(() => {
const SPECS = {paladin:['retribution','protection','holy'], druid:['feral','restoration','balance'], warrior:['protection','fury','arms'],
  shaman:['enhancement','restoration','elemental'], hunter:['beast-mastery','marksmanship','survival'], priest:['shadow','holy','discipline'],
  rogue:['combat','subtlety','assassination'], mage:['frost','arcane','fire'], warlock:['affliction','demonology','destruction']};
const sleep = ms => new Promise(r => setTimeout(r, ms));
const post = (n, o) => fetch('http://127.0.0.1:8799/' + n, {method: 'POST', body: JSON.stringify(o)});
async function frame(path) {
  const f = document.createElement('iframe');
  f.style.cssText = 'width:1200px;height:900px;position:fixed;left:-3000px;top:0';
  document.body.appendChild(f);
  await new Promise(r => { f.onload = r; f.src = path; });
  await sleep(3000);
  return f;
}
// Talent grid: 4 columns, empty cells kept, so the cell index gives row and column.
function grabTalents(doc) {
  const grids = [...doc.querySelectorAll('div.grid.grid-cols-4')].filter(g => g.querySelector('a[data-wowhead]'));
  const avgs = [...doc.querySelectorAll('[aria-label^="Average "]')].map(e => e.getAttribute('aria-label'));
  const trees = grids.map((g, ti) => {
    const tal = [];
    [...g.children].forEach((c, i) => {
      const a = c.querySelector('a[data-wowhead]'); if (!a) return;
      const l = a.getAttribute('aria-label');
      const m = l.match(/^(.*?): ([\d.]+)%: ([\d,]+) of ([\d,]+) characters with enough points(?:, usual rank (\d+) of (\d+))?/);
      const sp = (a.getAttribute('href') || '').match(/spell[=\/](\d+)/);
      const mx = c.textContent.match(/(\d+)\/(\d+)/);
      // rare labels ("<0.1%", "no character ...") stay raw in the name; convert.py parses them
      tal.push([m ? m[1] : l, sp ? +sp[1] : 0, Math.floor(i / 4), i % 4, m ? +m[2] : null, m ? +m[3].replace(/,/g, '') : null,
        m ? +m[4].replace(/,/g, '') : null, m && m[5] ? +m[5] : 0, m && m[6] ? +m[6] : (mx ? +mx[2] : null)]);
    });
    return {avg: avgs[ti] || null, t: tal};
  });
  const splits = [...doc.querySelectorAll('button')].map(b => (b.getAttribute('aria-label') || b.textContent).trim())
    .map(t => t.match(/^(\d+\/\d+\/\d+): ([\d.]+)%, ([\d,]+) of/)).filter(Boolean).map(m => [m[1], +m[2], +m[3].replace(/,/g, '')]);
  return {trees, splits};
}
async function talents(cls, spec) {
  const f = await frame('/forever/talents/' + cls + '/' + spec);
  const doc = f.contentDocument;
  const out = {url: '/forever/talents/' + cls + '/' + spec, cls, spec, cols: 'name,spell,row,col,pct,chars,eligible,usualRank,maxRank', brackets: {}};
  for (const b of doc.querySelectorAll('[role=group][aria-label="Talent level"] button')) {
    b.click(); await sleep(2000); out.brackets[b.textContent.trim()] = grabTalents(doc);
  }
  await post('talents_' + cls + '_' + spec, out); f.remove();
}
// Gear and enchant lists: rows "<name>: <pct>%, <n> of <N> characters" under one H2 per slot.
function listRows(doc) {
  const re = /^(.*): (<?[\d.]+)%, ([\d,]+) of ([\d,]+) characters$/; const out = []; let slot = '';
  const w = doc.createTreeWalker(doc.querySelector('article'), NodeFilter.SHOW_ELEMENT); let n;
  while ((n = w.nextNode())) {
    if (/^H[23]$/.test(n.tagName)) slot = n.textContent.trim();
    else if (n.tagName === 'SPAN' && n.classList.contains('sr-only')) {
      const m = n.textContent.trim().match(re); if (!m) continue;
      const li = n.closest('li') || n.parentElement; const a = li && li.querySelector('a[data-wowhead]');
      const d = a ? (a.getAttribute('data-wowhead') || '') + ' ' + (a.getAttribute('href') || '') : '';
      const id = (d.match(/(item|ench|spell)[=\/]\d+/) || [''])[0].replace('/', '=');
      out.push([slot, m[1], id, m[2].startsWith('<') ? 0.05 : +m[2], +m[3].replace(/,/g, ''), +m[4].replace(/,/g, '')]);
    }
  }
  return out;
}
async function listPage(cls, spec, kind) {
  const f = await frame('/forever/' + cls + '/' + spec + '/' + kind); const doc = f.contentDocument;
  const out = {cls, spec, kind, pops: {}};
  for (const b of [...doc.querySelectorAll('button')].filter(b => /^(Best geared|All level 60)/.test(b.textContent.trim()))) {
    b.click(); await sleep(2000);
    for (let k = 0; k < 3; k++) {
      const more = [...doc.querySelectorAll('button')].filter(x => /^Show more$/.test(x.textContent.trim()));
      if (!more.length) break; more.forEach(x => x.click()); await sleep(1200);
    }
    out.pops[b.textContent.trim().startsWith('Best') ? 'best_geared' : 'all_60'] = listRows(doc);
  }
  await post(kind + '_' + cls + '_' + spec, out); f.remove();
}
async function overview(cls, spec) {
  const f = await frame('/forever/' + cls + '/' + spec);
  const t = f.contentDocument.querySelector('article').innerText;
  const parts = ((t.split('Stat priority')[1] || '').split('Gear')[0]).split('\n').map(s => s.trim()).filter(s => s && s !== '›');
  const stats = []; for (let i = 0; i + 1 < parts.length; i += 2) stats.push([parts[i], +parts[i + 1]]);
  await post('overview_' + cls + '_' + spec, {cls, spec, stats}); f.remove();
}
// Spec share of each class's level 60s, from the classes page.
async function population() {
  const f = await frame('/forever/classes');
  const lines = f.contentDocument.querySelector('article').innerText.split('\n').map(s => s.trim()).filter(Boolean);
  const rows = [];
  for (let i = 0; i < lines.length; i++) {
    const m = lines[i].match(/^([\d,]+) at 60$/); if (!m) continue;
    const cls = lines[i - 1].toLowerCase();
    for (let k = i + 1; k + 1 < lines.length && /^\d+%$/.test(lines[k + 1]); k += 2)
      rows.push([cls, lines[k].toLowerCase().replace(/ /g, '-'), +lines[k + 1].replace('%', ''), +m[1].replace(/,/g, '')]);
  }
  await post('population', {rows}); f.remove();
}
// Census: the page text plus the level chart (one bar per level 10-60, scaled from the stated level 10 count).
async function census() {
  const f = await frame('/forever/population/live'); const doc = f.contentDocument;
  const text = doc.querySelector('article').innerText;
  const h = [...doc.querySelectorAll('h2,h3')].find(x => /Level distribution/.test(x.textContent));
  let sec = h; for (let i = 0; i < 4 && !sec.querySelector('svg'); i++) sec = sec.parentElement;
  const hs = [...sec.querySelector('svg').querySelectorAll('rect')].map(r => +r.getAttribute('height'));
  const peak = +text.match(/Level 10 \(the peak\): ([\d,]+)/)[1].replace(/,/g, '');
  await post('census', {levels: hs.map((x, i) => [10 + i, Math.round(x * peak / hs[0])]), text}); f.remove();
}
async function textPage(path, name) {
  const f = await frame(path);
  await post(name, {path, text: f.contentDocument.querySelector('article,main').innerText}); f.remove();
}
window.__aotcLog = [];
(async () => {
  await population();
  await census();
  await textPage('/forever/stats', 'page_stats');
  await textPage('/forever/legacy-points', 'page_legacy_points');
  for (const [c, ss] of Object.entries(SPECS)) for (const s of ss)
    for (const job of [() => talents(c, s), () => overview(c, s), () => listPage(c, s, 'gear'), () => listPage(c, s, 'enchants')]) {
      try { await job(); window.__aotcLog.push('ok ' + c + ' ' + s); } catch (e) { window.__aotcLog.push('ERR ' + c + ' ' + s + ' ' + e); }
      await sleep(1000);
    }
  window.__aotcLog.push('DONE');
})();
})();
