// Social profiles use the existing account session. Never trust a client email or badge.
import { lerSessao, lerJson, json, dentroDoLimite } from './contas.js';
import { enviarEmail } from './email.js';
import { storeRoute } from './store.js';
import { removeMuralPost } from './mural.js';
import { deleteCommunityMedia, readCommunityMedia, uploadCommunityMedia } from './community_media.js';

const BADGES = new Set(['', 'blue', 'green', 'gold']);
const MAX_FILE = 50 * 1024 * 1024;
const problem = (error, status = 400) => json({ error }, status);
const stmt = (env, sql, ...params) => env.AUREA_DB.prepare(sql).bind(...params);
const text = (v, max) => typeof v === 'string' && [...v.trim()].length <= max ? v.trim() : null;
function cursor(value) {
  const m = /^(\d{1,16}):([0-9a-f-]{36})$/.exec(value ?? '');
  return m ? [Number(m[1]), m[2]] : null;
}
const itemCursor = (item) => `${item.createdAt}:${item.id}`;
const nextCursor = (items) => items.length === 30 ? itemCursor(items.at(-1)) : null;
const POST_CATEGORIES = new Set(['text', 'showcase', 'project', 'preset', 'announcement']);
const POST_SORTS = new Set(['newest', 'oldest', 'popular']);
// Moderação (Google Play: conteúdo gerado pelo usuário e IA generativa).
// Denunciado por HIDE_AFTER pessoas diferentes, o conteúdo sai do feed de todos
// (menos do autor) até o admin decidir; bloqueio esconde nos dois sentidos.
const HIDE_AFTER = 3;
const REPORT_KINDS = new Set(['post', 'comment', 'user', 'preset', 'ai_video', 'store', 'mural']);
const REPORT_REASONS = new Set(['spam', 'nudity', 'violence', 'hate', 'harassment', 'illegal', 'copyright', 'other']);
const UUID = /^[0-9a-f-]{36}$/;
const notBlocked = (column) => `NOT EXISTS(SELECT 1 FROM community_blocks b WHERE (b.blocker=? AND b.blocked=${column}) OR (b.blocker=${column} AND b.blocked=?))`;
const notHidden = (kind, column, owner) => `(${owner}=? OR (SELECT COUNT(*) FROM community_reports r WHERE r.target_kind='${kind}' AND r.target_id=${column} AND r.status='open') < ${HIDE_AFTER})`;
// Bound decoded avatar dimensions too: a tiny compressed file can expand to a huge bitmap.
export function avatarMime(data, maxSide = 2048) {
  const dimensions = (width, height) => width > 0 && height > 0 && width <= maxSide && height <= maxSide;
  if (data.length >= 33 && [137,80,78,71,13,10,26,10].every((v, i) => data[i] === v)) {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    return view.getUint32(8) === 13 && view.getUint32(12) === 0x49484452 && dimensions(view.getUint32(16), view.getUint32(20)) ? 'image/png' : null;
  }
  if (data.length < 4 || data[0] !== 255 || data[1] !== 216) return null;
  let offset = 2;
  while (offset < data.length) {
    if (data[offset++] !== 255) return null;
    while (data[offset] === 255) ++offset;
    const marker = data[offset++];
    if (marker === 0xda || marker === 0xd9 || offset + 2 > data.length) return null;
    if (marker === 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
    const size = data[offset] * 256 + data[offset + 1];
    if (size < 2 || offset + size > data.length) return null;
    if ([0xc0, 0xc1, 0xc2].includes(marker)) {
      if (size < 8) return null;
      const height = data[offset + 3] * 256 + data[offset + 4], width = data[offset + 5] * 256 + data[offset + 6];
      return dimensions(width, height) ? 'image/jpeg' : null;
    }
    offset += size;
  }
  return null;
}
function author(row) {
  return { id: row.uid, username: row.username ?? '', name: row.name, bio: row.bio ?? '',
    avatar: row.avatar ?? null, verification: row.verification };
}
async function ensureProfile(env, uid) {
  await stmt(env, `INSERT OR IGNORE INTO community_profiles(uid, created_at, updated_at) VALUES(?, ?, ?)`, uid, Date.now(), Date.now()).run();
}
async function profile(env, uid, viewer) {
  const row = await stmt(env, `SELECT p.*,
    (SELECT COUNT(*) FROM community_follows WHERE followed=p.uid) AS followers,
    (SELECT COUNT(*) FROM community_follows WHERE follower=p.uid) AS following,
    (SELECT COUNT(*) FROM community_posts WHERE uid=p.uid) AS posts,
    EXISTS(SELECT 1 FROM community_follows WHERE follower=? AND followed=p.uid) AS followed,
    EXISTS(SELECT 1 FROM community_blocks WHERE blocker=? AND blocked=p.uid) AS blocked
    FROM community_profiles p WHERE p.uid=?`, viewer, viewer, uid).first();
  return row && { ...author(row), followers: row.followers, following: row.following, posts: row.posts, followed: !!row.followed,
    blocked: !!row.blocked };
}
const postSelect = `SELECT p.*, a.username, a.name, a.avatar, a.verification,
    f.kind AS asset_kind, f.name AS asset_name, f.bytes AS asset_bytes,
    m.kind AS media_kind, m.name AS media_name, m.bytes AS media_bytes,
    (SELECT COUNT(*) FROM community_likes WHERE post=p.id) AS likes,
    (SELECT COUNT(*) FROM community_comments WHERE post=p.id) AS comments,
    EXISTS(SELECT 1 FROM community_likes WHERE post=p.id AND uid=?) AS liked
    FROM community_posts p JOIN community_profiles a ON a.uid=p.uid
    LEFT JOIN community_assets f ON f.id=p.asset
    LEFT JOIN community_media m ON m.id=p.media`;
const postJson = r => ({ id: r.id, body: r.body, category: r.category, createdAt: r.created_at, author: author(r),
  likes: r.likes, comments: r.comments, liked: !!r.liked,
  asset: r.asset ? { id: r.asset, kind: r.asset_kind, name: r.asset_name, bytes: r.asset_bytes } : null,
  media: r.media ? { id: r.media, kind: r.media_kind, name: r.media_name, bytes: r.media_bytes } : null });

async function postPage(env, url, viewer) {
  const sort = url.searchParams.get('sort') ?? 'newest';
  const category = url.searchParams.get('category');
  if (!POST_SORTS.has(sort) || (category !== null && !POST_CATEGORIES.has(category))) return problem('invalid_feed_filter');
  const page = url.searchParams.get('cursor');
  const uid = url.searchParams.get('user');
  const following = url.searchParams.get('following') === '1';
  const popular = sort === 'popular', ascending = sort === 'oldest';
  const order = ascending ? 'ASC' : 'DESC', compare = ascending ? '>' : '<';
  const conditions = [notBlocked('p.uid'), notHidden('post', 'p.id', 'p.uid')], params = [viewer, viewer, viewer, viewer];
  if (category !== null) { conditions.push('p.category=?'); params.push(category); }
  if (page && popular) {
    const split = page.indexOf(':');
    const score = split > 0 ? page.slice(0, split) : '';
    const after = cursor(page.slice(split + 1));
    if (!/^\d{1,16}$/.test(score) || !after) return problem('invalid_feed_cursor');
    conditions.push('(p.popularity, p.created_at, p.id) < (?, ?, ?)'); params.push(Number(score), ...after);
  } else if (page) {
    const after = cursor(page);
    if (!after) return problem('invalid_feed_cursor');
    conditions.push(`(p.created_at, p.id) ${compare} (?, ?)`); params.push(...after);
  }
  if (uid) { conditions.push('p.uid=?'); params.push(uid); }
  if (following) { conditions.push('EXISTS(SELECT 1 FROM community_follows f WHERE f.follower=? AND f.followed=p.uid)'); params.push(viewer); }
  const size = url.searchParams.get('compact') === '1' ? 12 : 30;
  const rows = await stmt(env, `${postSelect}
    WHERE ${conditions.join(' AND ')}
    ORDER BY ${popular ? 'p.popularity DESC, ' : ''}p.created_at ${order}, p.id ${order} LIMIT ?`, ...params, size + 1).all();
  const items = rows.results.slice(0, size).map(postJson);
  const next = rows.results.length > size ? itemCursor(items.at(-1)) : null;
  return { items, cursor: next && popular ? `${rows.results[size - 1].popularity}:${next}` : next };
}

async function activity(env, url, viewer) {
  const read = await stmt(env, 'SELECT seen_at FROM community_activity_read WHERE uid=?', viewer).first();
  const seen = read?.seen_at ?? 0;
  const through = Date.now();
  const entries = `SELECT n.comment AS id, n.kind, n.created_at, c.post, c.uid, COALESCE(NULLIF(a.name,''), a.username, '') AS name
    FROM community_comment_activity n JOIN community_comments c ON c.id=n.comment
    JOIN community_posts p ON p.id=c.post JOIN community_profiles a ON a.uid=c.uid
    WHERE n.recipient=? AND ${notBlocked('c.uid')} AND ${notBlocked('p.uid')}
      AND ${notHidden('comment', 'c.id', 'c.uid')} AND ${notHidden('post', 'p.id', 'p.uid')}
    UNION ALL
    SELECT p.id, 'announcement', p.created_at, p.id, p.uid, COALESCE(NULLIF(a.name,''), a.username, '')
    FROM community_posts p JOIN community_profiles a ON a.uid=p.uid
    WHERE p.category='announcement' AND p.uid<>? AND ${notBlocked('p.uid')} AND ${notHidden('post', 'p.id', 'p.uid')}`;
  const args = Array(11).fill(viewer);
  const count = stmt(env, `WITH entries AS (${entries}) SELECT COUNT(*) AS unread FROM
    (SELECT 1 FROM entries WHERE created_at>? AND created_at<=? LIMIT 100)`, ...args, seen, through).first();
  if (url.searchParams.get('summary') === '1') return json({ unread: (await count).unread });
  const page = cursor(url.searchParams.get('cursor')) ?? [through + 1, ''];
  const list = stmt(env, `WITH entries AS (${entries}) SELECT * FROM entries
    WHERE (created_at, id) < (?, ?) ORDER BY created_at DESC, id DESC LIMIT 31`, ...args, ...page).all();
  const [readCount, rows] = await Promise.all([count, list]);
  const items = rows.results.slice(0, 30).map(r => ({ id: r.id, post: r.post, kind: r.kind, name: r.name,
    createdAt: r.created_at, unread: r.created_at > seen }));
  return json({ items, unread: readCount.unread, through, cursor: rows.results.length > 30 ? itemCursor(items.at(-1)) : null });
}
async function download(req, env, id) {
  const asset = await stmt(env, `SELECT * FROM community_assets WHERE id=? AND
    (EXISTS(SELECT 1 FROM community_profiles WHERE avatar=?) OR EXISTS(SELECT 1 FROM community_posts WHERE asset=?))`, id, id, id).first();
  if (!asset || !env.COMMUNITY_FILES) return problem('not_found', 404);
  const object = await env.COMMUNITY_FILES.get(id);
  if (!object) return problem('not_found', 404);
  return new Response(req.method === 'HEAD' ? null : object.body, { headers: {
    'content-type': asset.mime, 'content-length': String(object.size),
    'content-disposition': `${asset.kind === 'avatar' ? 'inline' : 'attachment'}; filename*=UTF-8''${encodeURIComponent(asset.name)}`,
    // 1.0.9: an avatar id never changes content (a new photo is a new id), so devices keep it
    // a day instead of asking again on every feed scroll. Presets/projects stay at 5 minutes.
    'cache-control': asset.kind === 'avatar' ? 'public, max-age=86400' : 'public, max-age=300', 'x-content-type-options': 'nosniff',
    'content-security-policy': "default-src 'none'; sandbox", 'etag': object.httpEtag ?? `"${id}"`,
  } });
}
async function upload(req, env, session, url) {
  if (!env.COMMUNITY_FILES) return problem('storage_unavailable', 503);
  const kind = url.searchParams.get('kind');
  const name = text(url.searchParams.get('name'), 100)?.replace(/[\x00-\x1f/\\]/g, '_');
  if (!['avatar', 'preset', 'project'].includes(kind) || !name) return problem('invalid_file');
  const length = Number(req.headers.get('content-length'));
  const limit = kind === 'avatar' ? 512 * 1024 : kind === 'preset' ? 1024 * 1024 : MAX_FILE;
  if (!Number.isSafeInteger(length) || length <= 0 || length > limit || !req.body) return problem('file_too_large', 413);
  if (!(await dentroDoLimite(env, 'community-upload', session.uid, { max: 20, janela: 3600 }))) return problem('rate_limited', 429);
  const id = crypto.randomUUID();
  let mime = 'application/octet-stream';
  // Small resources are validated before storage. Project ZIPs are streamed with a strict bound.
  if (kind !== 'project') {
    const reader = req.body.getReader(), chunks = []; let bytes = 0;
    try {
      while (true) { const { done, value } = await reader.read(); if (done) break;
        bytes += value.byteLength; if (bytes > limit) { await reader.cancel(); return problem('file_too_large', 413); } chunks.push(value); }
    } finally { reader.releaseLock(); }
    if (bytes !== length) return problem('invalid_file');
    const data = new Uint8Array(bytes); let offset = 0;
    for (const part of chunks) { data.set(part, offset); offset += part.byteLength; }
    if (kind === 'preset') {
      try {
        const value = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(data));
        if (!value || value.aurea_preset !== 1 || !['effects', 'text', 'animation', 'caption', 'curve'].includes(value.kind) || Array.isArray(value)) return problem('invalid_preset');
      } catch { return problem('invalid_preset'); }
      mime = 'application/json';
    } else {
      mime = avatarMime(data);
      if (!mime) return problem('invalid_image');
    }
    await env.COMMUNITY_FILES.put(id, data, { httpMetadata: { contentType: mime } });
  } else {
    if (!name.toLowerCase().endsWith('.aureaproj')) return problem('invalid_project');
    const stream = typeof FixedLengthStream === 'function' ? new FixedLengthStream(length) : new TransformStream();
    let bytes = 0, prefix = [];
    const pumping = (async () => {
      const reader = req.body.getReader(), writer = stream.writable.getWriter();
      try {
        while (true) {
          const { done, value } = await reader.read(); if (done) break;
          bytes += value.byteLength;
          if (bytes > length) throw new Error('file_too_large');
          for (let i = 0; prefix.length < 4 && i < value.length; ++i) prefix.push(value[i]);
          if (prefix.length === 4 && prefix.join(',') !== '80,75,3,4') throw new Error('invalid_project');
          await writer.write(value);
        }
        if (bytes !== length || prefix.length !== 4) throw new Error('invalid_file');
        await writer.close();
      } catch (e) { await writer.abort(e); await reader.cancel(e); throw e; }
      finally { reader.releaseLock(); writer.releaseLock(); }
    })();
    try { await Promise.all([pumping, env.COMMUNITY_FILES.put(id, stream.readable)]); }
    catch { await env.COMMUNITY_FILES.delete(id); return problem('invalid_file'); }
  }
  try {
    await stmt(env, 'INSERT INTO community_assets(id,uid,kind,name,mime,bytes,created_at) VALUES(?,?,?,?,?,?,?)',
      id, session.uid, kind, name, mime, length, Date.now()).run();
  } catch (e) { await env.COMMUNITY_FILES.delete(id); throw e; }
  return json({ id, kind, name, bytes: length }, 201);
}

// Denúncia: uma por pessoa e alvo (repetir não conta de novo). O dono recebe
// um e-mail para revisar; com HIDE_AFTER denúncias o conteúdo sai do feed.
async function report(req, env, ctx, session) {
  const { valor: v, erro } = await lerJson(req, 4096); if (erro) return problem(erro);
  const kind = v?.kind, id = typeof v?.id === 'string' ? v.id : '', reason = v?.reason;
  if (!REPORT_KINDS.has(kind) || !REPORT_REASONS.has(reason)) return problem('invalid_report');
  if (kind === 'ai_video' ? !/^[A-Za-z0-9_.:-]{1,80}$/.test(id) : !UUID.test(id)) return problem('invalid_report');
  const details = typeof v.details === 'string' ? [...v.details.trim()].slice(0, 500).join('') : '';
  if (!(await dentroDoLimite(env, 'community-report', session.uid, { max: 30, janela: 3600 }))) return problem('rate_limited', 429);
  const table = { post: 'community_posts', comment: 'community_comments', user: 'community_profiles', mural: 'mural_posts', store: 'community_store_items' }[kind];
  if (table) {
    const column = kind === 'user' ? 'uid' : 'id';
    const target = await stmt(env, `SELECT ${column} AS id FROM ${table} WHERE ${column}=?`, id).first();
    if (!target) return problem('not_found', 404);
    if (kind === 'user' && id === session.uid) return problem('cannot_report_self');
  }
  const result = await stmt(env, `INSERT OR IGNORE INTO community_reports(id,reporter,target_kind,target_id,reason,details,created_at)
    VALUES(?,?,?,?,?,?,?)`, crypto.randomUUID(), session.uid, kind, id, reason, details, Date.now()).run();
  const created = (result?.meta?.changes ?? 1) > 0;
  if (created) {
    const count = await stmt(env, "SELECT COUNT(*) AS n FROM community_reports WHERE target_kind=? AND target_id=? AND status='open'", kind, id).first();
    const mail = enviarEmail(env, {
      assunto: `[Aurea] Denúncia: ${kind} ${id} (${count?.n ?? 1})`,
      texto: `Tipo: ${kind}\nAlvo: ${id}\nMotivo: ${reason}\nDenúncias abertas: ${count?.n ?? 1}\nDetalhes: ${details || '-'}\n\n` +
        `Revisar: GET /api/community/reports (admin). Remover: PUT /api/community/reports/${kind}/${id} {"status":"removed"}.`,
    }).catch(() => {});
    if (ctx?.waitUntil) ctx.waitUntil(mail); else await mail;
  }
  return json({ ok: true }, created ? 201 : 200);
}

export async function communityRoute(req, env, ctx, url) {
  const path = url.pathname.replace(/\/+$/, '');
  if (!path.startsWith('/api/community')) return null;
  if (url.protocol !== 'https:' && !['localhost', '127.0.0.1'].includes(url.hostname)) return problem('https_required', 403);
  if (!env.AUREA_DB) return problem('community_unavailable', 503);
  try {
    const assetMatch = /^\/api\/community\/assets\/([0-9a-f-]{36})$/.exec(path);
    if (assetMatch && ['GET', 'HEAD'].includes(req.method)) return await download(req, env, assetMatch[1]);
    const mediaMatch = /^\/api\/community\/media\/([0-9a-f-]{36})$/.exec(path);
    if (mediaMatch && ['GET', 'HEAD'].includes(req.method)) return await readCommunityMedia(req, env, mediaMatch[1]);
    const session = await lerSessao(env, req);
    if (!session) return problem('unauthorized', 401);
    // lerSessao already joined the live account row (revoked/deleted accounts return null),
    // so the admin role and the profile come from ONE query; the profile is written only
    // when missing. D1 is far from most users (WEUR): every round trip costs ~160 ms.
    const who = await stmt(env, `SELECT EXISTS(SELECT 1 FROM users WHERE id=?) AS account,
      EXISTS(SELECT 1 FROM account_roles WHERE uid=? AND role='community_admin') AS admin,
      EXISTS(SELECT 1 FROM community_profiles WHERE uid=?) AS profiled`, session.uid, session.uid, session.uid).first();
    if (!who?.account) return problem('unauthorized', 401);
    const admin = !!who.admin;
    if (!who.profiled) await ensureProfile(env, session.uid);
    if (req.method !== 'GET' && !(await dentroDoLimite(env, 'community-write', session.uid, { max: 120, janela: 60 }))) return problem('rate_limited', 429);
    if (path === '/api/community/me' && req.method === 'GET') {
      const withFeed = url.searchParams.get('includeFeed') === '1';
      const [owner, feed] = await Promise.all([
        profile(env, session.uid, session.uid),
        withFeed ? postPage(env, new URL('/api/community/posts?compact=1', url), session.uid) : undefined,
      ]);
      return json({ profile: owner, canVerify: admin, feedFilters: true, mediaPosts: !!env.COMMUNITY_FILES, activity: true, feed });
    }
    if (path === '/api/community/notifications' && req.method === 'GET') return await activity(env, url, session.uid);
    if (path === '/api/community/notifications/read' && req.method === 'PUT') {
      const { valor: v, erro } = await lerJson(req, 256);
      if (erro || !Number.isSafeInteger(v?.through) || v.through < 0) return problem('invalid_activity');
      await stmt(env, `INSERT INTO community_activity_read(uid,seen_at) VALUES(?,?)
        ON CONFLICT(uid) DO UPDATE SET seen_at=MAX(seen_at, excluded.seen_at)`, session.uid, Math.min(v.through, Date.now())).run();
      return json({ ok: true });
    }
    if (path === '/api/community/me' && req.method === 'PUT') {
      const { valor: v, erro } = await lerJson(req, 4096); if (erro) return problem(erro);
      const username = text(v.username, 24)?.toLowerCase(), name = text(v.name, 50), bio = text(v.bio, 240);
      if (!username || !/^[a-z0-9_]{3,24}$/.test(username) || !name || bio === null) return problem('invalid_profile');
      let avatar = v.avatar ?? null;
      if (avatar && !(await stmt(env, "SELECT id FROM community_assets WHERE id=? AND uid=? AND kind='avatar'", avatar, session.uid).first())) return problem('invalid_avatar');
      try { await stmt(env, 'UPDATE community_profiles SET username=?,name=?,bio=?,avatar=?,updated_at=? WHERE uid=?', username, name, bio, avatar, Date.now(), session.uid).run(); }
      catch (e) { if (/UNIQUE/i.test(String(e))) return problem('username_taken', 409); throw e; }
      return json({ profile: await profile(env, session.uid, session.uid), canVerify: admin });
    }
    if (path === '/api/community/profiles' && req.method === 'GET') {
      const q = (url.searchParams.get('q') ?? '').toLowerCase().replace(/[^a-z0-9_]/g, '').slice(0, 24);
      if (q.length < 2) return json({ items: [] });
      const rows = await stmt(env, "SELECT * FROM community_profiles WHERE username LIKE ? ESCAPE '\\' ORDER BY username LIMIT 30", q.replace(/_/g, '\\_') + '%').all();
      return json({ items: rows.results.map(author) });
    }
    const userMatch = /^\/api\/community\/profiles\/([0-9a-f-]{36})(?:\/(follow|verification|followers|following|block))?$/.exec(path);
    if (userMatch) {
      const [, uid, action] = userMatch;
      if (!await stmt(env, 'SELECT uid FROM community_profiles WHERE uid=?', uid).first()) return problem('not_found', 404);
      if (!action && req.method === 'GET') return json({ profile: await profile(env, uid, session.uid) });
      if (action === 'verification' && req.method === 'PUT') {
        if (!admin) return problem('forbidden', 403);
        const { valor: v, erro } = await lerJson(req, 256); if (erro || !BADGES.has(v?.verification)) return problem('invalid_verification');
        await env.AUREA_DB.batch([
          stmt(env, 'UPDATE community_profiles SET verification=?,updated_at=? WHERE uid=?', v.verification, Date.now(), uid),
          stmt(env, 'INSERT INTO community_verifications(id,admin,uid,verification,created_at) VALUES(?,?,?,?,?)', crypto.randomUUID(), session.uid, uid, v.verification, Date.now()),
        ]);
        return json({ profile: await profile(env, uid, session.uid) });
      }
      if (action === 'follow' && ['PUT', 'DELETE'].includes(req.method)) {
        if (uid === session.uid) return problem('cannot_follow_self');
        await stmt(env, req.method === 'PUT' ? 'INSERT OR IGNORE INTO community_follows(follower,followed) VALUES(?,?)' : 'DELETE FROM community_follows WHERE follower=? AND followed=?', session.uid, uid).run();
        return json({ profile: await profile(env, uid, session.uid) });
      }
      if (action === 'block' && ['PUT', 'DELETE'].includes(req.method)) {
        if (uid === session.uid) return problem('cannot_block_self');
        if (req.method === 'PUT') {
          await env.AUREA_DB.batch([
            stmt(env, 'INSERT OR IGNORE INTO community_blocks(blocker,blocked,created_at) VALUES(?,?,?)', session.uid, uid, Date.now()),
            stmt(env, 'DELETE FROM community_follows WHERE (follower=? AND followed=?) OR (follower=? AND followed=?)', session.uid, uid, uid, session.uid),
          ]);
        } else {
          await stmt(env, 'DELETE FROM community_blocks WHERE blocker=? AND blocked=?', session.uid, uid).run();
        }
        return json({ profile: await profile(env, uid, session.uid) });
      }
      if (['followers', 'following'].includes(action) && req.method === 'GET') {
        const own = action === 'followers' ? 'followed' : 'follower', other = action === 'followers' ? 'follower' : 'followed';
        const after = url.searchParams.get('cursor') ?? '';
        const rows = await stmt(env, `SELECT p.* FROM community_follows f JOIN community_profiles p ON p.uid=f.${other} WHERE f.${own}=? AND p.uid>? ORDER BY p.uid LIMIT 30`, uid, after).all();
        return json({ items: rows.results.map(author), cursor: rows.results.length === 30 ? rows.results.at(-1).uid : null });
      }
    }
    if (path === '/api/community/assets' && req.method === 'POST') return await upload(req, env, session, url);
    if (path === '/api/community/media' && req.method === 'POST') {
      if (!(await profile(env, session.uid, session.uid)).username) return problem('profile_required');
      return await uploadCommunityMedia(req, env, session.uid, url, avatarMime);
    }
    if (mediaMatch && req.method === 'DELETE') {
      const id = mediaMatch[1];
      const claim = await stmt(env, `DELETE FROM community_media WHERE id=? AND uid=?
        AND NOT EXISTS(SELECT 1 FROM community_posts WHERE media=?)`, id, session.uid, id).run();
      if (claim.meta.changes) await deleteCommunityMedia(env, id);
      return json({ ok: true });
    }
    if (path === '/api/community/posts' && req.method === 'GET') {
      const page = await postPage(env, url, session.uid);
      return page instanceof Response ? page : json(page);
    }
    if (path === '/api/community/posts' && req.method === 'POST') {
      const { valor: v, erro } = await lerJson(req, 12000); if (erro) return problem(erro);
      const body = text(v?.body, 2000), asset = v?.asset ?? null, media = v?.media ?? null;
      const category = v?.category ?? 'standard';
      if (!['standard', 'showcase', 'announcement'].includes(category)) return problem('invalid_post_category');
      if (category === 'announcement' && !admin) return problem('forbidden', 403);
      if (body === null || (!body && !asset && !media)) return problem('empty_post');
      if ((asset !== null && (typeof asset !== 'string' || !UUID.test(asset))) ||
          (media !== null && (typeof media !== 'string' || !UUID.test(media)))) return problem('invalid_attachment');
      const p = await profile(env, session.uid, session.uid); if (!p.username) return problem('profile_required');
      const attachment = asset ? await stmt(env, "SELECT kind FROM community_assets WHERE id=? AND uid=? AND kind IN ('project','preset')", asset, session.uid).first() : null;
      if (asset && !attachment) return problem('invalid_attachment');
      const id = crypto.randomUUID();
      const storedCategory = category === 'standard' ? attachment?.kind ?? (media ? 'showcase' : 'text') : category;
      const saved = await stmt(env, `INSERT INTO community_posts(id,uid,body,asset,created_at,category,media)
        SELECT ?,?,?,?,?,?,? WHERE ? IS NULL OR (EXISTS(SELECT 1 FROM community_media WHERE id=? AND uid=?)
          AND NOT EXISTS(SELECT 1 FROM community_posts WHERE media=?))`,
        id, session.uid, body, asset, Date.now(), storedCategory, media, media, media, session.uid, media).run();
      if (!saved.meta.changes) return problem('invalid_attachment');
      return json({ id }, 201);
    }
    const postMatch = /^\/api\/community\/posts\/([0-9a-f-]{36})(?:\/(like|comments))?$/.exec(path);
    if (postMatch) {
      const [, id, action] = postMatch;
      if (!action && req.method === 'GET') {
        const row = await stmt(env, `${postSelect} WHERE p.id=? AND ${notBlocked('p.uid')} AND ${notHidden('post', 'p.id', 'p.uid')}`,
          session.uid, id, session.uid, session.uid, session.uid).first();
        return row ? json(postJson(row)) : problem('not_found', 404);
      }
      const post = await stmt(env, `SELECT p.uid, p.media FROM community_posts p WHERE p.id=? AND (
        ? OR (${notBlocked('p.uid')} AND ${notHidden('post', 'p.id', 'p.uid')}))`,
        id, !action && req.method === 'DELETE' && admin ? 1 : 0, session.uid, session.uid, session.uid).first();
      if (!post) return problem('not_found', 404);
      if (!action && req.method === 'DELETE') {
        if (post.uid !== session.uid && !admin) return problem('forbidden', 403);
        await env.AUREA_DB.batch([
          stmt(env, 'DELETE FROM community_posts WHERE id=?', id),
          stmt(env, "UPDATE community_reports SET status='removed' WHERE target_kind='post' AND target_id=?", id),
        ]);
        await deleteCommunityMedia(env, post.media);
        return json({ ok: true });
      }
      if (action === 'like' && ['PUT', 'DELETE'].includes(req.method)) {
        await stmt(env, req.method === 'PUT' ? 'INSERT OR IGNORE INTO community_likes(post,uid) VALUES(?,?)' : 'DELETE FROM community_likes WHERE post=? AND uid=?', id, session.uid).run();
        const count = await stmt(env, 'SELECT COUNT(*) AS likes FROM community_likes WHERE post=?', id).first();
        return json({ likes: count.likes, liked: req.method === 'PUT' });
      }
      if (action === 'comments' && req.method === 'GET') {
        const after = cursor(url.searchParams.get('cursor')) ?? [0, ''];
        const rows = await stmt(env, `SELECT c.*, p.username,p.name,p.avatar,p.verification,
          CASE WHEN pc.id IS NOT NULL THEN COALESCE(NULLIF(pa.name,''),pa.username) END AS reply_name
          FROM community_comments c JOIN community_profiles p ON p.uid=c.uid
          LEFT JOIN community_comments pc ON pc.id=c.reply_to AND ${notBlocked('pc.uid')} AND ${notHidden('comment', 'pc.id', 'pc.uid')}
          LEFT JOIN community_profiles pa ON pa.uid=pc.uid
          WHERE c.post=? AND (c.created_at,c.id) > (?,?) AND ${notBlocked('c.uid')} AND ${notHidden('comment', 'c.id', 'c.uid')}
          ORDER BY c.created_at,c.id LIMIT 30`, session.uid, session.uid, session.uid, id, ...after, session.uid, session.uid, session.uid).all();
        const items = rows.results.map(r => ({ id: r.id, body: r.body, createdAt: r.created_at, author: author(r),
          replyTo: r.reply_name ? r.reply_to : null, replyName: r.reply_name }));
        return json({ items, cursor: nextCursor(items) });
      }
      if (action === 'comments' && req.method === 'POST') {
        const { valor: v, erro } = await lerJson(req, 6000); if (erro) return problem(erro);
        const body = text(v?.body, 1000); if (!body) return problem('empty_comment');
        const replyTo = v?.replyTo ?? null;
        if (replyTo !== null && (typeof replyTo !== 'string' || !UUID.test(replyTo))) return problem('invalid_reply');
        if (!(await profile(env, session.uid, session.uid)).username) return problem('profile_required');
        const comment = crypto.randomUUID();
        const saved = await stmt(env, `INSERT INTO community_comments(id,post,uid,body,created_at,reply_to)
          SELECT ?,?,?,?,?,? WHERE ? IS NULL OR EXISTS(SELECT 1 FROM community_comments parent WHERE parent.id=?
            AND parent.post=? AND ${notBlocked('parent.uid')} AND ${notHidden('comment', 'parent.id', 'parent.uid')})`,
          comment, id, session.uid, body, Date.now(), replyTo, replyTo, replyTo, id, session.uid, session.uid, session.uid).run();
        if (!saved.meta.changes) return problem('invalid_reply');
        return json({ id: comment }, 201);
      }
    }
    if (path === '/api/community/reports' && req.method === 'POST') return await report(req, env, ctx, session);
    if (path === '/api/community/reports' && req.method === 'GET') {
      if (!admin) return problem('forbidden', 403);
      const status = ['open', 'removed', 'dismissed'].includes(url.searchParams.get('status')) ? url.searchParams.get('status') : 'open';
      const rows = await stmt(env, `SELECT target_kind, target_id, COUNT(*) AS reports, MIN(created_at) AS first, MAX(created_at) AS last,
        GROUP_CONCAT(DISTINCT reason) AS reasons FROM community_reports WHERE status=?
        GROUP BY target_kind, target_id ORDER BY reports DESC, last DESC LIMIT 100`, status).all();
      return json({ items: rows.results.map(r => ({ kind: r.target_kind, id: r.target_id, reports: r.reports, first: r.first, last: r.last,
        reasons: String(r.reasons ?? '').split(',').filter(Boolean) })) });
    }
    const reportMatch = /^\/api\/community\/reports\/(post|comment|user|preset|ai_video|store|mural)\/([A-Za-z0-9_.:-]{1,80})$/.exec(path);
    if (reportMatch && req.method === 'PUT') {
      if (!admin) return problem('forbidden', 403);
      const { valor: v, erro } = await lerJson(req, 256); if (erro || !['removed', 'dismissed'].includes(v?.status)) return problem('invalid_status');
      const [, kind, id] = reportMatch;
      const deletedMedia = v.status === 'removed' && kind === 'post'
        ? (await stmt(env, 'SELECT media FROM community_posts WHERE id=?', id).first())?.media : null;
      const updates = [stmt(env, 'UPDATE community_reports SET status=? WHERE target_kind=? AND target_id=?', v.status, kind, id)];
      if (v.status === 'removed' && kind === 'post') updates.push(stmt(env, 'DELETE FROM community_posts WHERE id=?', id));
      if (v.status === 'removed' && kind === 'comment') updates.push(stmt(env, 'DELETE FROM community_comments WHERE id=?', id));
      if (v.status === 'removed' && kind === 'store') updates.push(stmt(env, "UPDATE community_store_items SET status='rejected',review_note='removido por denuncia',updated_at=? WHERE id=?", Date.now(), id));
      await env.AUREA_DB.batch(updates);
      await deleteCommunityMedia(env, deletedMedia);
      if (v.status === 'removed' && kind === 'mural') await removeMuralPost(env, id);
      return json({ ok: true });
    }
    if (path === '/api/community/store' || path.startsWith('/api/community/store/')) {
      const handled = await storeRoute(req, env, url, path, session, admin);
      if (handled) return handled;
    }
    const commentMatch = /^\/api\/community\/comments\/([0-9a-f-]{36})$/.exec(path);
    if (commentMatch && req.method === 'DELETE') {
      const comment = await stmt(env, 'SELECT uid FROM community_comments WHERE id=?', commentMatch[1]).first();
      if (!comment) return problem('not_found', 404);
      if (comment.uid !== session.uid && !admin) return problem('forbidden', 403);
      await env.AUREA_DB.batch([
        stmt(env, 'DELETE FROM community_comments WHERE id=?', commentMatch[1]),
        stmt(env, "UPDATE community_reports SET status='removed' WHERE target_kind='comment' AND target_id=?", commentMatch[1]),
      ]);
      return json({ ok: true });
    }
    return problem('not_found', 404);
  } catch (e) {
    console.error('community: request failed');
    return problem('community_unavailable', 503);
  }
}
