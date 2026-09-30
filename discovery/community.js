// Social profiles use the existing account session. Never trust a client email or badge.
import { lerSessao, lerJson, json, dentroDoLimite } from './contas.js';

const OWNER_EMAIL = 'ruanpablombl@gmail.com';
const BADGES = new Set(['', 'blue', 'green', 'gold']);
const MAX_FILE = 50 * 1024 * 1024;
const problem = (error, status = 400) => json({ error }, status);
const stmt = (env, sql, ...params) => env.AUREA_DB.prepare(sql).bind(...params);
const text = (v, max) => typeof v === 'string' && [...v.trim()].length <= max ? v.trim() : null;
function cursor(value) {
  const m = /^(\d{1,16}):([0-9a-f-]{36})$/.exec(value ?? '');
  return m ? [Number(m[1]), m[2]] : null;
}
const nextCursor = (items) => items.length === 30 ? `${items.at(-1).createdAt}:${items.at(-1).id}` : null;
// Bound decoded avatar dimensions too: a tiny compressed file can expand to a huge bitmap.
function avatarMime(data) {
  const dimensions = (width, height) => width > 0 && height > 0 && width <= 2048 && height <= 2048;
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
    EXISTS(SELECT 1 FROM community_follows WHERE follower=? AND followed=p.uid) AS followed
    FROM community_profiles p WHERE p.uid=?`, viewer, uid).first();
  return row && { ...author(row), followers: row.followers, following: row.following, posts: row.posts, followed: !!row.followed };
}
async function posts(env, url, viewer) {
  const before = cursor(url.searchParams.get('cursor'));
  const uid = url.searchParams.get('user');
  const following = url.searchParams.get('following') === '1';
  const conditions = [], params = [viewer];
  if (before) { conditions.push('(p.created_at < ? OR (p.created_at = ? AND p.id < ?))'); params.push(before[0], ...before); }
  if (uid) { conditions.push('p.uid=?'); params.push(uid); }
  if (following) { conditions.push('EXISTS(SELECT 1 FROM community_follows f WHERE f.follower=? AND f.followed=p.uid)'); params.push(viewer); }
  const rows = await stmt(env, `SELECT p.*, a.username, a.name, a.avatar, a.verification,
    f.kind AS asset_kind, f.name AS asset_name, f.bytes AS asset_bytes,
    (SELECT COUNT(*) FROM community_likes WHERE post=p.id) AS likes,
    (SELECT COUNT(*) FROM community_comments WHERE post=p.id) AS comments,
    EXISTS(SELECT 1 FROM community_likes WHERE post=p.id AND uid=?) AS liked
    FROM community_posts p JOIN community_profiles a ON a.uid=p.uid
    LEFT JOIN community_assets f ON f.id=p.asset
    ${conditions.length ? 'WHERE ' + conditions.join(' AND ') : ''}
    ORDER BY p.created_at DESC, p.id DESC LIMIT 30`, ...params).all();
  const items = rows.results.map(r => ({ id: r.id, body: r.body, createdAt: r.created_at, author: author(r),
    likes: r.likes, comments: r.comments, liked: !!r.liked,
    asset: r.asset ? { id: r.asset, kind: r.asset_kind, name: r.asset_name, bytes: r.asset_bytes } : null }));
  return json({ items, cursor: nextCursor(items) });
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
    'cache-control': 'public, max-age=300', 'x-content-type-options': 'nosniff',
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

export async function communityRoute(req, env, ctx, url) {
  const path = url.pathname.replace(/\/+$/, '');
  if (!path.startsWith('/api/community')) return null;
  if (url.protocol !== 'https:' && !['localhost', '127.0.0.1'].includes(url.hostname)) return problem('https_required', 403);
  if (!env.AUREA_DB) return problem('community_unavailable', 503);
  try {
    const assetMatch = /^\/api\/community\/assets\/([0-9a-f-]{36})$/.exec(path);
    if (assetMatch && ['GET', 'HEAD'].includes(req.method)) return await download(req, env, assetMatch[1]);
    const session = await lerSessao(env, req);
    if (!session) return problem('unauthorized', 401);
    // Read the actual account: revoked/deleted accounts and stale session emails cannot become admins.
    const account = await stmt(env, 'SELECT email FROM users WHERE id=?', session.uid).first();
    if (!account) return problem('unauthorized', 401);
    const admin = account.email.toLowerCase() === OWNER_EMAIL;
    await ensureProfile(env, session.uid);
    if (req.method !== 'GET' && !(await dentroDoLimite(env, 'community-write', session.uid, { max: 120, janela: 60 }))) return problem('rate_limited', 429);
    if (path === '/api/community/me' && req.method === 'GET') return json({ profile: await profile(env, session.uid, session.uid), canVerify: admin });
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
    const userMatch = /^\/api\/community\/profiles\/([0-9a-f-]{36})(?:\/(follow|verification|followers|following))?$/.exec(path);
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
      if (['followers', 'following'].includes(action) && req.method === 'GET') {
        const own = action === 'followers' ? 'followed' : 'follower', other = action === 'followers' ? 'follower' : 'followed';
        const after = url.searchParams.get('cursor') ?? '';
        const rows = await stmt(env, `SELECT p.* FROM community_follows f JOIN community_profiles p ON p.uid=f.${other} WHERE f.${own}=? AND p.uid>? ORDER BY p.uid LIMIT 30`, uid, after).all();
        return json({ items: rows.results.map(author), cursor: rows.results.length === 30 ? rows.results.at(-1).uid : null });
      }
    }
    if (path === '/api/community/assets' && req.method === 'POST') return await upload(req, env, session, url);
    if (path === '/api/community/posts' && req.method === 'GET') return await posts(env, url, session.uid);
    if (path === '/api/community/posts' && req.method === 'POST') {
      const { valor: v, erro } = await lerJson(req, 12000); if (erro) return problem(erro);
      const body = text(v.body, 2000), asset = v.asset ?? null;
      if (body === null || (!body && !asset)) return problem('empty_post');
      const p = await profile(env, session.uid, session.uid); if (!p.username) return problem('profile_required');
      if (asset && !(await stmt(env, "SELECT id FROM community_assets WHERE id=? AND uid=? AND kind IN ('project','preset')", asset, session.uid).first())) return problem('invalid_attachment');
      const id = crypto.randomUUID();
      await stmt(env, 'INSERT INTO community_posts(id,uid,body,asset,created_at) VALUES(?,?,?,?,?)', id, session.uid, body, asset, Date.now()).run();
      return json({ id }, 201);
    }
    const postMatch = /^\/api\/community\/posts\/([0-9a-f-]{36})(?:\/(like|comments))?$/.exec(path);
    if (postMatch) {
      const [, id, action] = postMatch;
      const post = await stmt(env, 'SELECT uid FROM community_posts WHERE id=?', id).first();
      if (!post) return problem('not_found', 404);
      if (!action && req.method === 'DELETE') {
        if (post.uid !== session.uid) return problem('forbidden', 403);
        await stmt(env, 'DELETE FROM community_posts WHERE id=?', id).run(); return json({ ok: true });
      }
      if (action === 'like' && ['PUT', 'DELETE'].includes(req.method)) {
        await stmt(env, req.method === 'PUT' ? 'INSERT OR IGNORE INTO community_likes(post,uid) VALUES(?,?)' : 'DELETE FROM community_likes WHERE post=? AND uid=?', id, session.uid).run();
        const count = await stmt(env, 'SELECT COUNT(*) AS likes FROM community_likes WHERE post=?', id).first();
        return json({ likes: count.likes, liked: req.method === 'PUT' });
      }
      if (action === 'comments' && req.method === 'GET') {
        const after = cursor(url.searchParams.get('cursor')) ?? [0, ''];
        const rows = await stmt(env, `SELECT c.*, p.username,p.name,p.avatar,p.verification FROM community_comments c JOIN community_profiles p ON p.uid=c.uid
          WHERE c.post=? AND (c.created_at>? OR (c.created_at=? AND c.id>?)) ORDER BY c.created_at,c.id LIMIT 30`, id, after[0], ...after).all();
        const items = rows.results.map(r => ({ id: r.id, body: r.body, createdAt: r.created_at, author: author(r) }));
        return json({ items, cursor: nextCursor(items) });
      }
      if (action === 'comments' && req.method === 'POST') {
        const { valor: v, erro } = await lerJson(req, 6000); if (erro) return problem(erro);
        const body = text(v.body, 1000); if (!body) return problem('empty_comment');
        if (!(await profile(env, session.uid, session.uid)).username) return problem('profile_required');
        const comment = crypto.randomUUID();
        await stmt(env, 'INSERT INTO community_comments(id,post,uid,body,created_at) VALUES(?,?,?,?,?)', comment, id, session.uid, body, Date.now()).run();
        return json({ id: comment }, 201);
      }
    }
    const commentMatch = /^\/api\/community\/comments\/([0-9a-f-]{36})$/.exec(path);
    if (commentMatch && req.method === 'DELETE') {
      const comment = await stmt(env, 'SELECT uid FROM community_comments WHERE id=?', commentMatch[1]).first();
      if (!comment) return problem('not_found', 404);
      if (comment.uid !== session.uid) return problem('forbidden', 403);
      await stmt(env, 'DELETE FROM community_comments WHERE id=?', commentMatch[1]).run(); return json({ ok: true });
    }
    return problem('not_found', 404);
  } catch (e) {
    console.error('community:', String(e?.message ?? e).slice(0, 180));
    return problem('community_unavailable', 503);
  }
}
