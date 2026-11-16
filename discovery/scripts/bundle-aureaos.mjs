// bundle-aureaos.mjs — empacota o AureaOS (o método de motion do dono) para o AureaMCP.
// Mesma ideia do bundle-brain.mjs do TwoMCP (MotionCloud/tools/twomcp): copia o que o agente precisa ler,
// gera um índice pesquisável e, com --upload, sobe tudo para o R2 do Worker. Ver docs/aureamcp/DESIGN.md §2.4.
//
//   node scripts/bundle-aureaos.mjs                       (só monta em discovery/.aureaos-bundle/<versão>/)
//   node scripts/bundle-aureaos.mjs --upload              (monta e sobe: R2 aureamcp/brain/<versão>/ + KV aureamcp:brain:current)
//   opções: --src=C:/Users/Ruan/AureaOS  --skills=C:/Users/Ruan/.claude/skills  --out=.aureaos-bundle  --bucket=aurea-community-files
//
// Rode de novo sempre que o AureaOS mudar (referência nova, regra nova, efeito aprovado). Versões antigas ficam no R2:
// voltar = `npx wrangler kv key put --binding=AUREA_KV aureamcp:brain:current <versão antiga> --remote`.
// Subir é publicar: só com o OK do dono (regra da casa para o Worker).
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

export const FILE_MAX = 1024 * 1024;        // por arquivo (o Worker lê inteiro e pagina na resposta)
export const BUNDLE_MAX = 24 * 1024 * 1024; // pacote inteiro
export const PREFIX = 'aureamcp/brain/';

const here = path.dirname(fileURLToPath(import.meta.url));
const opt = Object.fromEntries(process.argv.slice(2).map((a) => { const i = a.indexOf('='); return i < 0 ? [a.replace(/^--/, ''), '1'] : [a.slice(2, i), a.slice(i + 1)]; }));

/** Texto em UTF-8; arquivo antigo em latin-1 (alguns meta.json) é convertido. */
function readText(file) {
  const buf = fs.readFileSync(file);
  try { return new TextDecoder('utf-8', { fatal: true }).decode(buf); } catch { return buf.toString('latin1'); }
}

const docstring = (src) => { const m = /^(?:#![^\n]*\n)?\s*(?:"""([\s\S]*?)"""|'''([\s\S]*?)''')/.exec(src); return m ? (m[1] ?? m[2]).trim() : ''; };

/** Monta o pacote em memória: [{path, text}] + índice. Puro (sem rede): os testes usam com uma pasta falsa. */
export function buildBundle({ src, skills }) {
  const files = [];
  const skipped = [];
  const add = (dst, file, transform) => {
    if (!fs.existsSync(file)) return false;
    let text = readText(file);
    if (transform) text = transform(text);
    if (Buffer.byteLength(text) > FILE_MAX) { skipped.push({ path: dst, bytes: Buffer.byteLength(text) }); return false; }
    files.push({ path: dst, text });
    return true;
  };
  const S = (p) => path.join(src, p);

  // 1) regras e as duas skills (método e diretor)
  add('rules/CLAUDE.md', S('CLAUDE.md'));
  add('skills/metodo.md', path.join(skills, 'metodo-aureaos', 'SKILL.md'));
  add('skills/diretor.md', path.join(skills, 'diretor-aureaos', 'SKILL.md'));
  // 2) catálogo e índices medidos
  for (const f of ['CATALOGO.md', 'EASINGS_VERTICAIS.md', 'EASINGS_HORIZONTAIS.md', 'TEXTO_TRANSICOES_VERTICAIS.md']) add('referencias/' + f, S('referencias/' + f));
  add('referencias/COMO_CATALOGAR.md', S('referencias/_lib/COMO_CATALOGAR.md'));
  // 3) biblioteca de efeitos aprovados + o exemplo aprovado (ref02 cenas 1–4)
  add('engine/fx.js', S('engine/fx.js'));
  add('engine/aurea.js', S('engine/aurea.js'));
  for (const f of ['scenes.js', 'speed.json', 'cam.json']) add('exemplo/ref02/' + f, S('projects/ref02/' + f));
  // 4) ferramentas locais: só a documentação (rodam no PC com o AureaOS, não no Worker)
  const toolsDir = S('tools');
  if (fs.existsSync(toolsDir)) {
    const lines = ['# Ferramentas locais do AureaOS', '', 'Rodam no PC com a pasta do AureaOS (Python/Node). Os resultados delas para as referências já estão no catálogo.', ''];
    for (const f of fs.readdirSync(toolsDir).filter((x) => /\.(py|mjs|sh)$/.test(x)).sort()) {
      const doc = f.endsWith('.py') ? docstring(readText(path.join(toolsDir, f))) : '';
      lines.push(`## tools/${f}`, '', doc || '(sem descrição)', '');
    }
    files.push({ path: 'tools/README.md', text: lines.join('\n') });
  }
  // 5) referências: cada uma com as partes que existirem
  const lib = S('referencias/_lib');
  const refs = [];
  const catalog = fs.existsSync(S('referencias/CATALOGO.md')) ? readText(S('referencias/CATALOGO.md')) : '';
  if (fs.existsSync(lib)) {
    for (const slug of fs.readdirSync(lib).filter((d) => /^R\d+_/.test(d) && fs.statSync(path.join(lib, d)).isDirectory()).sort()) {
      const d = path.join(lib, slug);
      const parts = [];
      for (const [part, file] of [['cenas', 'CENAS.md'], ['deep', 'deep.md'], ['text', 'text.md'], ['trans', 'trans.md'], ['meta', 'meta.json'], ['tags', 'tags.json']]) {
        if (add(`ref/${slug}/${part}.${file.endsWith('.md') ? 'md' : 'json'}`, path.join(d, file))) parts.push(part);
      }
      let meta = null, tags = [];
      try { meta = JSON.parse(readText(path.join(d, 'meta.json'))); } catch { /* imagem estática ou sem meta */ }
      try { tags = JSON.parse(readText(path.join(d, 'tags.json'))); } catch { tags = []; }
      let foot = null;
      try {
        const dj = JSON.parse(readText(path.join(d, 'deep.json')));
        const tot = dj.shots.reduce((a, s) => a + s.frames, 0) || 1;
        foot = dj.shots.filter((s) => String(s.fundo ?? '').includes('filmagem')).reduce((a, s) => a + s.frames, 0) / tot;
      } catch { foot = null; }
      let cuts = Array.isArray(meta?.cuts) ? meta.cuts.length : 0;
      try {
        const tj = JSON.parse(readText(path.join(d, 'trans.json')));
        cuts = tj.transicoes.filter((t) => !/^(NÃO é corte|troca LOCAL|mudança DENTRO)/.test(t.tipos?.[0] ?? '')).length;
      } catch { /* sem trans.json: cortes do meta */ }
      const count = new Map();
      for (const c of tags) for (const g of c.tags ?? []) count.set(g, (count.get(g) ?? 0) + 1);
      const ranked = [...count.entries()].sort((a, b) => b[1] - a[1]).map(([k]) => k);
      const row = catalog.split('\n').find((l) => l.startsWith(`| [${slug}]`));
      const resumo = row ? row.split('|').slice(4, 5).join('').trim() : '';
      const [w, h] = Array.isArray(meta?.size) ? meta.size : [0, 0];
      refs.push({
        slug,
        formato: !meta ? 'imagem' : h > w ? 'vertical' : 'horizontal',
        size: [w, h], fps: meta?.fps ?? 0, dur: meta?.dur_s ?? 0,
        cps: meta?.dur_s ? Math.round((cuts / meta.dur_s) * 100) / 100 : 0,
        foot: foot == null ? null : Math.round(foot * 100) / 100,
        resumo,
        estilo: ranked.filter((k) => k.startsWith('estilo:')).slice(0, 3),
        uso: ranked.filter((k) => k.startsWith('uso:')).slice(0, 3),
        tec: ranked.filter((k) => !k.startsWith('estilo:') && !k.startsWith('uso:')).slice(0, 6),
        parts,
        cenas: tags.map((c) => ({ cena: c.cena, a: c.a, b: c.b, tags: c.tags ?? [], uso: c.uso ?? [], nota: String(c.nota ?? '').slice(0, 400) })),
      });
    }
  }
  return { files, refs, skipped };
}

export function versionNow(d = new Date()) {
  const p = (x) => String(x).padStart(2, '0');
  return `${d.getUTCFullYear()}${p(d.getUTCMonth() + 1)}${p(d.getUTCDate())}${p(d.getUTCHours())}${p(d.getUTCMinutes())}`;
}

/** Grava o pacote na pasta de saída e devolve o manifesto. */
export function writeBundle(bundle, outDir, version) {
  const dir = path.join(outDir, version);
  fs.rmSync(dir, { recursive: true, force: true });
  const manifest = { version, built: new Date().toISOString(), files: [], skipped: bundle.skipped };
  let total = 0;
  for (const f of bundle.files) {
    const bytes = Buffer.from(f.text, 'utf8');
    total += bytes.length;
    const dst = path.join(dir, f.path);
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    fs.writeFileSync(dst, bytes);
    manifest.files.push({ path: f.path, bytes: bytes.length, sha256: crypto.createHash('sha256').update(bytes).digest('hex') });
  }
  const index = { version, refs: bundle.refs };
  const indexBytes = Buffer.from(JSON.stringify(index));
  total += indexBytes.length;
  if (total > BUNDLE_MAX) throw new Error(`pacote grande demais: ${(total / 1048576).toFixed(1)} MB > ${BUNDLE_MAX / 1048576} MB`);
  fs.writeFileSync(path.join(dir, 'index.json'), indexBytes);
  manifest.total = total;
  fs.writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify(manifest, null, 1));
  return { dir, manifest };
}

function upload(dir, manifest, bucket) {
  const wrangler = (args) => spawnSync(process.platform === 'win32' ? 'npx.cmd' : 'npx', ['wrangler', ...args], { cwd: path.resolve(here, '..'), stdio: 'inherit', shell: process.platform === 'win32' });
  // Sem espaço: no Windows o spawn com shell quebra o argumento no espaço.
  const type = (p) => (p.endsWith('.md') ? 'text/markdown;charset=utf-8' : p.endsWith('.json') ? 'application/json' : 'text/javascript;charset=utf-8');
  // Arquivos primeiro; manifest e índice por último; o ponteiro do KV só no fim (nunca aponta para pacote pela metade).
  for (const f of [...manifest.files.map((x) => x.path), 'index.json', 'manifest.json']) {
    const r = wrangler(['r2', 'object', 'put', `${bucket}/${PREFIX}${manifest.version}/${f}`, `--file=${path.join(dir, f)}`, `--content-type=${type(f)}`, '--remote']);
    if (r.status !== 0) throw new Error('upload falhou em ' + f);
  }
  const r = wrangler(['kv', 'key', 'put', '--binding=AUREA_KV', 'aureamcp:brain:current', manifest.version, '--remote']);
  if (r.status !== 0) throw new Error('ponteiro do KV não atualizou');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const src = path.resolve(opt.src || 'C:/Users/Ruan/AureaOS');
  const skills = path.resolve(opt.skills || 'C:/Users/Ruan/.claude/skills');
  if (!fs.existsSync(path.join(src, 'CLAUDE.md'))) { console.error('AureaOS não encontrado em ' + src + ' (passe --src=)'); process.exit(2); }
  const bundle = buildBundle({ src, skills });
  const version = versionNow();
  const { dir, manifest } = writeBundle(bundle, path.resolve(here, '..', opt.out || '.aureaos-bundle'), version);
  console.log(`AureaOS → ${dir}\n  ${manifest.files.length} arquivos · ${bundle.refs.length} referências · ${bundle.refs.reduce((a, r) => a + r.cenas.length, 0)} cenas · ${(manifest.total / 1048576).toFixed(1)} MB`);
  for (const s of bundle.skipped) console.log(`  ! fora (maior que ${FILE_MAX / 1048576} MB): ${s.path} (${(s.bytes / 1048576).toFixed(1)} MB)`);
  if (opt.upload) { upload(dir, manifest, opt.bucket || 'aurea-community-files'); console.log(`✓ no ar: aureamcp:brain:current = ${version}`); }
  else console.log('  (só local; --upload sobe para o R2 e troca o ponteiro, com o OK do dono)');
}
