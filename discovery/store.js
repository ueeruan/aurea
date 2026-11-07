// Loja da Comunidade (0.4.6): plugins, efeitos e presets da comunidade.
// O criador envia o pacote e as fotos (POST /store/files), depois cria o item
// (POST /store) com nome, descrição e link de tutorial. Plugin (.aureaplugin)
// entra PENDENTE até um admin aprovar: ele pode rodar script e pedir rede.
// Preset (.json) entra aprovado. Quem baixa instala pelo fluxo normal do app
// (o consentimento de permissões do plugin continua no aparelho).
import { lerJson, json, dentroDoLimite } from './contas.js';
import { avatarMime } from './community.js';

const MAX_PLUGIN = 20 * 1024 * 1024;
const MAX_PRESET = 1024 * 1024;
const MAX_PHOTO = 3 * 1024 * 1024;
const MAX_PHOTOS = 4;
const MAX_MANIFEST = 256 * 1024;
const PAGE = 30;
const HIDE_AFTER = 3;
const UUID = /^[0-9a-f-]{36}$/;
const PLUGIN_ID = /^[a-z0-9._-]{3,64}$/;
const VERSION = /^(\d{1,6})\.(\d{1,6})\.(\d{1,6})$/;
const PRESET_KINDS = new Set(['effects', 'text', 'animation', 'caption', 'curve']);
const PLUGIN_KINDS = new Set(['shader', 'shader3d', 'stack', 'panel']);
const problem = (error, status = 400) => json({ error }, status);
const stmt = (env, sql, ...params) => env.AUREA_DB.prepare(sql).bind(...params);
const text = (v, max) => typeof v === 'string' && [...v.trim()].length <= max ? v.trim() : null;

// --- ZIP: só o necessário para ler o plugin.json (sem extrair nada) ---------
const u16 = (d, o) => d[o] | (d[o + 1] << 8);
const u32 = (d, o) => (d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)) >>> 0;
async function inflateRaw(bytes, limit) {
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
  const reader = stream.getReader(), parts = []; let total = 0;
  for (;;) {
    const { done, value } = await reader.read(); if (done) break;
    total += value.byteLength;
    if (total > limit) { await reader.cancel(); throw new Error('manifest_too_large'); }
    parts.push(value);
  }
  const out = new Uint8Array(total); let o = 0;
  for (const p of parts) { out.set(p, o); o += p.byteLength; }
  return out;
}
/** Lê o plugin.json de um .aureaplugin. Recusa caminhos perigosos (zip slip). */
export async function readPluginManifest(data) {
  if (data.length < 22 || u32(data, 0) !== 0x04034b50) throw new Error('invalid_plugin');
  let eocd = -1;
  for (let i = data.length - 22; i >= Math.max(0, data.length - 65557); --i) if (u32(data, i) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) throw new Error('invalid_plugin');
  const count = u16(data, eocd + 10), size = u32(data, eocd + 12), start = u32(data, eocd + 16);
  if (count === 0 || count > 512 || start + size > eocd) throw new Error('invalid_plugin');
  let p = start, manifest = null; const names = [];
  for (let n = 0; n < count; ++n) {
    if (p + 46 > data.length || u32(data, p) !== 0x02014b50) throw new Error('invalid_plugin');
    const method = u16(data, p + 10), comp = u32(data, p + 20), raw = u32(data, p + 24);
    const nameLen = u16(data, p + 28), extra = u16(data, p + 30), comment = u16(data, p + 32), local = u32(data, p + 42);
    const name = new TextDecoder('utf-8', { fatal: true }).decode(data.subarray(p + 46, p + 46 + nameLen));
    if (!name || name.startsWith('/') || name.includes('\\') || name.split('/').includes('..') || /^[a-z]:/i.test(name)) throw new Error('invalid_plugin');
    names.push(name);
    if (name === 'plugin.json') manifest = { method, comp, raw, local };
    p += 46 + nameLen + extra + comment;
  }
  if (!manifest || manifest.raw > MAX_MANIFEST) throw new Error('invalid_plugin');
  const l = manifest.local;
  if (l + 30 > data.length || u32(data, l) !== 0x04034b50) throw new Error('invalid_plugin');
  const begin = l + 30 + u16(data, l + 26) + u16(data, l + 28);
  if (begin + manifest.comp > data.length) throw new Error('invalid_plugin');
  const body = data.subarray(begin, begin + manifest.comp);
  const bytes = manifest.method === 0 ? body : manifest.method === 8 ? await inflateRaw(body, MAX_MANIFEST) : null;
  if (!bytes) throw new Error('invalid_plugin');
  return { manifest: JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes)), names };
}
const localized = (v) => typeof v === 'string' ? v : v && typeof v === 'object' ? (v['pt-BR'] ?? v.pt ?? v.en ?? v['*'] ?? Object.values(v)[0]) : null;
/** O que a loja mostra do plugin.json — as mesmas regras do PluginManifest.cpp. */
export function pluginMeta(manifest, names) {
  const m = manifest;
  if (!m || typeof m !== 'object' || ![1, 2].includes(m.aureaPlugin)) throw new Error('invalid_plugin');
  if (typeof m.id !== 'string' || !PLUGIN_ID.test(m.id)) throw new Error('invalid_plugin');
  if (typeof m.version !== 'string' || !VERSION.test(m.version)) throw new Error('invalid_plugin');
  if (!PLUGIN_KINDS.has(m.kind) || (m.kind === 'panel' && m.aureaPlugin < 2)) throw new Error('invalid_plugin');
  const name = localized(m.name);
  if (typeof name !== 'string' || !name.trim() || [...name].length > 64) throw new Error('invalid_plugin');
  const description = localized(m.description);
  const perms = m.permissions && typeof m.permissions === 'object' ? m.permissions : {};
  const network = Array.isArray(perms.network) ? perms.network.filter(d => typeof d === 'string').slice(0, 16).map(d => d.slice(0, 120)) : [];
  return {
    format: 'plugin', type: m.kind === 'panel' ? 'plugin' : 'effect', pluginId: m.id, version: m.version,
    name: name.trim(), description: typeof description === 'string' ? description.slice(0, 512) : '',
    permissions: { network, clipboard: perms.clipboard === true, files: perms.files === true },
    hasScript: typeof m.script === 'string' || names.some(n => n.toLowerCase().endsWith('.js')),
  };
}
export function presetMeta(value) {
  if (!value || Array.isArray(value) || value.aurea_preset !== 1 || !PRESET_KINDS.has(value.kind)) throw new Error('invalid_preset');
  const name = typeof value.name === 'string' ? value.name.trim().slice(0, 64) : '';
  return { format: 'preset', type: value.kind === 'effects' ? 'effect' : 'preset', presetKind: value.kind, name,
    description: '', version: '', permissions: { network: [], clipboard: false, files: false }, hasScript: false };
}
const compareVersion = (a, b) => {
  const x = VERSION.exec(a)?.slice(1).map(Number) ?? [0, 0, 0], y = VERSION.exec(b)?.slice(1).map(Number) ?? [0, 0, 0];
  for (let i = 0; i < 3; ++i) if (x[i] !== y[i]) return x[i] - y[i];
  return 0;
};
function tutorial(v) {
  if (v === undefined || v === null || v === '') return '';
  if (typeof v !== 'string' || v.length > 300) return null;
  try { const u = new URL(v.trim()); return u.protocol === 'https:' && !u.username && !u.password ? u.toString() : null; }
  catch { return null; }
}

async function readBody(req, limit) {
  const length = Number(req.headers.get('content-length'));
  if (!Number.isSafeInteger(length) || length <= 0 || length > limit || !req.body) return null;
  const reader = req.body.getReader(), chunks = []; let bytes = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read(); if (done) break;
      bytes += value.byteLength; if (bytes > limit) { await reader.cancel(); return null; }
      chunks.push(value);
    }
  } finally { reader.releaseLock(); }
  if (bytes !== length) return null;
  const data = new Uint8Array(bytes); let o = 0;
  for (const c of chunks) { data.set(c, o); o += c.byteLength; }
  return data;
}

async function uploadFile(req, env, session, url) {
  if (!env.COMMUNITY_FILES) return problem('storage_unavailable', 503);
  const role = url.searchParams.get('role');
  const name = text(url.searchParams.get('name'), 100)?.replace(/[\x00-\x1f/\\]/g, '_');
  if (!['package', 'photo'].includes(role) || !name) return problem('invalid_file');
  if (!(await dentroDoLimite(env, 'store-upload', session.uid, { max: 40, janela: 3600 }))) return problem('rate_limited', 429);
  const lower = name.toLowerCase();
  const plugin = role === 'package' && lower.endsWith('.aureaplugin');
  if (role === 'package' && !plugin && !lower.endsWith('.json')) return problem('invalid_package');
  const data = await readBody(req, role === 'photo' ? MAX_PHOTO : plugin ? MAX_PLUGIN : MAX_PRESET);
  if (!data) return problem('file_too_large', 413);
  let mime, meta = {};
  try {
    if (role === 'photo') {
      mime = avatarMime(data, 4096);
      if (!mime) return problem('invalid_image');
    } else if (plugin) {
      const { manifest, names } = await readPluginManifest(data);
      meta = pluginMeta(manifest, names);
      mime = 'application/zip';
    } else {
      meta = presetMeta(JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(data)));
      mime = 'application/json';
    }
  } catch { return problem(plugin ? 'invalid_plugin' : role === 'photo' ? 'invalid_image' : 'invalid_preset'); }
  const id = crypto.randomUUID();
  await env.COMMUNITY_FILES.put(id, data, { httpMetadata: { contentType: mime } });
  try {
    await stmt(env, 'INSERT INTO community_store_files(id,uid,role,name,mime,bytes,meta,created_at) VALUES(?,?,?,?,?,?,?,?)',
      id, session.uid, role, name, mime, data.length, JSON.stringify(meta), Date.now()).run();
  } catch (e) { await env.COMMUNITY_FILES.delete(id); throw e; }
  return json({ id, role, name, bytes: data.length, meta: role === 'package' ? meta : undefined }, 201);
}

const SELECT = `SELECT i.*, p.username, p.name AS author_name, p.avatar, p.verification, f.bytes AS package_bytes,
  (SELECT COUNT(*) FROM community_store_likes WHERE item=i.id) AS likes,
  EXISTS(SELECT 1 FROM community_store_likes WHERE item=i.id AND uid=?) AS liked
  FROM community_store_items i JOIN community_profiles p ON p.uid=i.uid JOIN community_store_files f ON f.id=i.package`;
function item(row, viewer, admin) {
  const mine = row.uid === viewer;
  const permissions = (() => { try { return JSON.parse(row.permissions); } catch { return {}; } })();
  const photos = (() => { try { return JSON.parse(row.photos); } catch { return []; } })();
  return {
    id: row.id, type: row.type, format: row.format, name: row.name, description: row.description,
    tutorialUrl: row.tutorial_url, photos: photos.length, pluginId: row.plugin_id ?? null, version: row.version,
    presetKind: row.preset_kind ?? null, permissions: { network: permissions.network ?? [], clipboard: !!permissions.clipboard, files: !!permissions.files },
    hasScript: !!row.has_script, downloads: row.downloads, likes: row.likes, liked: !!row.liked, bytes: row.package_bytes,
    status: row.status, ...(mine || admin ? { reviewNote: row.review_note } : {}), mine,
    author: { id: row.uid, username: row.username ?? '', name: row.author_name, avatar: row.avatar ?? null, verification: row.verification },
    createdAt: row.created_at, updatedAt: row.updated_at,
  };
}
async function visible(env, id, viewer, admin) {
  const row = await stmt(env, `${SELECT} WHERE i.id=?`, viewer, id).first();
  if (!row) return null;
  if (row.status === 'approved' || row.uid === viewer || admin) return row;
  return null;
}
const notBlocked = `NOT EXISTS(SELECT 1 FROM community_blocks b WHERE (b.blocker=? AND b.blocked=i.uid) OR (b.blocker=i.uid AND b.blocked=?))`;

async function list(env, url, viewer, admin) {
  const type = url.searchParams.get('type');
  const sort = url.searchParams.get('sort') === 'popular' ? 'popular' : 'new';
  const mine = url.searchParams.get('mine') === '1';
  const pending = url.searchParams.get('status') === 'pending';
  if (pending && !admin) return problem('forbidden', 403);
  const offset = Math.min(10000, Math.max(0, Number.parseInt(url.searchParams.get('cursor') ?? '0', 10) || 0));
  const conditions = [], params = [viewer];
  if (mine) { conditions.push('i.uid=?'); params.push(viewer); }
  else if (pending) conditions.push("i.status='pending'");
  else {
    conditions.push("i.status='approved'", notBlocked,
      `(i.uid=? OR (SELECT COUNT(*) FROM community_reports r WHERE r.target_kind='store' AND r.target_id=i.id AND r.status='open') < ${HIDE_AFTER})`);
    params.push(viewer, viewer, viewer);
  }
  if (['plugin', 'effect', 'preset'].includes(type)) { conditions.push('i.type=?'); params.push(type); }
  const q = (url.searchParams.get('q') ?? '').trim().toLowerCase().slice(0, 40);
  if (q) {
    conditions.push("(LOWER(i.name) LIKE ? ESCAPE '\\' OR LOWER(i.description) LIKE ? ESCAPE '\\' OR LOWER(p.username) LIKE ? ESCAPE '\\')");
    const like = '%' + q.replace(/[\\%_]/g, c => '\\' + c) + '%';
    params.push(like, like, like);
  }
  const order = sort === 'popular' && !pending ? 'i.downloads DESC, i.created_at DESC, i.id DESC' : 'i.created_at DESC, i.id DESC';
  const rows = await stmt(env, `${SELECT} WHERE ${conditions.join(' AND ')} ORDER BY ${order} LIMIT ${PAGE} OFFSET ?`, ...params, offset).all();
  const items = rows.results.map(r => item(r, viewer, admin));
  return json({ items, cursor: items.length === PAGE ? String(offset + PAGE) : null });
}

async function ownedFiles(env, uid, pkg, photos) {
  const meta = await stmt(env, "SELECT * FROM community_store_files WHERE id=? AND uid=? AND role='package'", pkg, uid).first();
  if (!meta) return { error: 'invalid_package' };
  for (const photo of photos) {
    if (!(await stmt(env, "SELECT id FROM community_store_files WHERE id=? AND uid=? AND role='photo'", photo, uid).first())) return { error: 'invalid_photo' };
  }
  try { return { file: meta, meta: JSON.parse(meta.meta) }; } catch { return { error: 'invalid_package' }; }
}
function readFields(v, partial) {
  const name = v.name === undefined && partial ? undefined : text(v.name, 64);
  const description = v.description === undefined && partial ? undefined : text(v.description ?? '', 2000);
  const tutorialUrl = v.tutorialUrl === undefined && partial ? undefined : tutorial(v.tutorialUrl);
  const photos = v.photos === undefined ? (partial ? undefined : [])
    : Array.isArray(v.photos) && v.photos.length <= MAX_PHOTOS && v.photos.every(p => typeof p === 'string' && UUID.test(p)) && new Set(v.photos).size === v.photos.length ? v.photos : null;
  if (name === null || (name !== undefined && !name) || description === null || tutorialUrl === null || photos === null) return null;
  return { name, description, tutorialUrl, photos };
}
async function pluginIdTaken(env, pluginId, uid, except) {
  return !!(await stmt(env, "SELECT id FROM community_store_items WHERE plugin_id=? AND uid<>? AND status<>'rejected' AND id<>?",
    pluginId, uid, except ?? '').first());
}

async function create(req, env, session, admin) {
  const { valor: v, erro } = await lerJson(req, 16000); if (erro) return problem(erro);
  const fields = readFields(v ?? {}, false); if (!fields) return problem('invalid_item');
  const author = await stmt(env, 'SELECT username FROM community_profiles WHERE uid=?', session.uid).first();
  if (!author?.username) return problem('profile_required');
  if (typeof v.package !== 'string' || !UUID.test(v.package)) return problem('invalid_package');
  if (await stmt(env, 'SELECT id FROM community_store_items WHERE package=?', v.package).first()) return problem('invalid_package');
  const owned = await ownedFiles(env, session.uid, v.package, fields.photos); if (owned.error) return problem(owned.error);
  const meta = owned.meta;
  if (meta.format === 'plugin' && await pluginIdTaken(env, meta.pluginId, session.uid)) return problem('plugin_id_taken', 409);
  if (!(await dentroDoLimite(env, 'store-create', session.uid, { max: 10, janela: 3600 }))) return problem('rate_limited', 429);
  const id = crypto.randomUUID(), now = Date.now();
  // Plugin espera revisão (script, rede, arquivos); preset é dado puro.
  const status = meta.format === 'plugin' && !admin ? 'pending' : 'approved';
  await stmt(env, `INSERT INTO community_store_items(id,uid,type,format,name,description,tutorial_url,package,photos,plugin_id,version,
    preset_kind,permissions,has_script,status,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)`,
    id, session.uid, meta.type, meta.format, fields.name, fields.description, fields.tutorialUrl, v.package, JSON.stringify(fields.photos),
    meta.pluginId ?? null, meta.version ?? '', meta.presetKind ?? null, JSON.stringify(meta.permissions ?? {}), meta.hasScript ? 1 : 0,
    status, now, now).run();
  return json({ item: item(await stmt(env, `${SELECT} WHERE i.id=?`, session.uid, id).first(), session.uid, admin) }, 201);
}

async function update(req, env, session, admin, row) {
  if (row.uid !== session.uid) return problem('forbidden', 403);
  const { valor: v, erro } = await lerJson(req, 16000); if (erro) return problem(erro);
  const fields = readFields(v ?? {}, true); if (!fields) return problem('invalid_item');
  let photos = fields.photos, status = row.status, pkg = row.package, meta = null;
  if (v.package !== undefined && v.package !== row.package) {
    if (typeof v.package !== 'string' || !UUID.test(v.package)) return problem('invalid_package');
    if (await stmt(env, 'SELECT id FROM community_store_items WHERE package=?', v.package).first()) return problem('invalid_package');
    const owned = await ownedFiles(env, session.uid, v.package, []); if (owned.error) return problem(owned.error);
    meta = owned.meta;
    // Nova versão do MESMO item: mesmo formato, mesmo plugin, versão maior
    // (o app recusa instalar uma versão mais velha por cima da nova).
    if (meta.format !== row.format) return problem('invalid_package');
    if (meta.format === 'plugin' && (meta.pluginId !== row.plugin_id || compareVersion(meta.version, row.version) <= 0)) return problem('version_not_newer', 409);
    pkg = v.package;
    if (meta.format === 'plugin' && !admin) status = 'pending';
  }
  if (photos) { const owned = await ownedFiles(env, session.uid, pkg, photos); if (owned.error) return problem(owned.error); }
  await stmt(env, `UPDATE community_store_items SET name=COALESCE(?,name), description=COALESCE(?,description), tutorial_url=COALESCE(?,tutorial_url),
    photos=COALESCE(?,photos), package=?, type=?, version=?, preset_kind=?, permissions=?, has_script=?, status=?, review_note=?, updated_at=? WHERE id=?`,
    fields.name ?? null, fields.description ?? null, fields.tutorialUrl ?? null, photos ? JSON.stringify(photos) : null, pkg,
    meta?.type ?? row.type, meta?.version ?? row.version, meta?.presetKind ?? row.preset_kind,
    meta ? JSON.stringify(meta.permissions ?? {}) : row.permissions, meta ? (meta.hasScript ? 1 : 0) : row.has_script,
    status, status === row.status ? row.review_note : '', Date.now(), row.id).run();
  return json({ item: item(await stmt(env, `${SELECT} WHERE i.id=?`, session.uid, row.id).first(), session.uid, admin) });
}

async function serve(req, env, fileId, inline) {
  const file = await stmt(env, 'SELECT * FROM community_store_files WHERE id=?', fileId).first();
  if (!file || !env.COMMUNITY_FILES) return problem('not_found', 404);
  const object = await env.COMMUNITY_FILES.get(fileId);
  if (!object) return problem('not_found', 404);
  return new Response(req.method === 'HEAD' ? null : object.body, { headers: {
    'content-type': file.mime, 'content-length': String(object.size),
    'content-disposition': `${inline ? 'inline' : 'attachment'}; filename*=UTF-8''${encodeURIComponent(file.name)}`,
    'cache-control': 'private, max-age=300', 'x-content-type-options': 'nosniff',
    'content-security-policy': "default-src 'none'; sandbox",
  } });
}

/** /api/community/store… (sessão e admin já resolvidos pelo community.js). */
export async function storeRoute(req, env, url, path, session, admin) {
  // O app abre a Loja só quando o servidor guarda arquivos (bucket R2): sem ele o
  // botão continua "Em breve", sem precisar de versão nova do app para ligar.
  if (path === '/api/community/store/config' && req.method === 'GET') return json({ uploads: !!env.COMMUNITY_FILES });
  if (path === '/api/community/store/files' && req.method === 'POST') return await uploadFile(req, env, session, url);
  if (path === '/api/community/store' && req.method === 'GET') return await list(env, url, session.uid, admin);
  if (path === '/api/community/store' && req.method === 'POST') return await create(req, env, session, admin);
  const m = /^\/api\/community\/store\/([0-9a-f-]{36})(?:\/(like|package|review|photos\/([0-3])))?$/.exec(path);
  if (!m) return null;
  const [, id, action, photoIndex] = m;
  const row = await visible(env, id, session.uid, admin);
  if (!row) return problem('not_found', 404);
  if (!action && req.method === 'GET') return json({ item: item(row, session.uid, admin) });
  if (!action && req.method === 'PUT') return await update(req, env, session, admin, row);
  if (!action && req.method === 'DELETE') {
    if (row.uid !== session.uid && !admin) return problem('forbidden', 403);
    await env.AUREA_DB.batch([
      stmt(env, 'DELETE FROM community_store_items WHERE id=?', id),
      stmt(env, "UPDATE community_reports SET status='removed' WHERE target_kind='store' AND target_id=?", id),
    ]);
    return json({ ok: true });
  }
  if (action === 'like' && ['PUT', 'DELETE'].includes(req.method)) {
    if (row.status !== 'approved') return problem('not_found', 404);
    await stmt(env, req.method === 'PUT' ? 'INSERT OR IGNORE INTO community_store_likes(item,uid) VALUES(?,?)' : 'DELETE FROM community_store_likes WHERE item=? AND uid=?', id, session.uid).run();
    const count = await stmt(env, 'SELECT COUNT(*) AS likes FROM community_store_likes WHERE item=?', id).first();
    return json({ likes: count.likes, liked: req.method === 'PUT' });
  }
  if (action === 'review' && req.method === 'PUT') {
    if (!admin) return problem('forbidden', 403);
    const { valor: v, erro } = await lerJson(req, 2048);
    if (erro || !['approved', 'rejected'].includes(v?.status)) return problem('invalid_status');
    const note = typeof v.note === 'string' ? [...v.note.trim()].slice(0, 500).join('') : '';
    await stmt(env, 'UPDATE community_store_items SET status=?, review_note=?, updated_at=? WHERE id=?', v.status, note, Date.now(), id).run();
    return json({ item: item(await stmt(env, `${SELECT} WHERE i.id=?`, session.uid, id).first(), session.uid, admin) });
  }
  if (action === 'package' && ['GET', 'HEAD'].includes(req.method)) {
    if (req.method === 'GET' && row.status === 'approved' && row.uid !== session.uid
        && await dentroDoLimite(env, 'store-download', `${session.uid}:${id}`, { max: 1, janela: 86400 }))
      await stmt(env, 'UPDATE community_store_items SET downloads=downloads+1 WHERE id=?', id).run();
    return await serve(req, env, row.package, false);
  }
  if (photoIndex !== undefined && ['GET', 'HEAD'].includes(req.method)) {
    const photos = (() => { try { return JSON.parse(row.photos); } catch { return []; } })();
    const photo = photos[Number(photoIndex)];
    if (!photo) return problem('not_found', 404);
    return await serve(req, env, photo, true);
  }
  return problem('not_found', 404);
}
