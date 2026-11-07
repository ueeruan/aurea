// =============================================================================
//  Mural (0.4.8): edits em vídeo para todo mundo ver na tela inicial.
//
//  Ver é aberto. Publicar consome um CRÉDITO, comprado na Play ou na App Store e
//  conferido aqui com a loja: o app nunca decide sozinho que pagou. Sem as chaves
//  da loja a compra responde 503, e sem o R2 a publicação também; nada fica aberto.
//
//    GET    /api/mural/config                → {posting, productId}
//    GET    /api/mural/feed?cursor=          → {items, cursor}
//    GET    /api/mural/media/<id>/video      → vídeo, com Range
//    GET    /api/mural/media/<id>/thumb      → capa
//    POST   /api/mural/credits               {platform, token} → {id, postId}
//    POST   /api/mural/posts                 {creditId, caption, bytes, width, height, durationMs}
//    PUT    /api/mural/posts/<id>/video      corpo = o .mp4
//    PUT    /api/mural/posts/<id>/thumb      corpo = o .jpg
//    POST   /api/mural/posts/<id>/publish    entra no ar na hora
//    DELETE /api/mural/posts/<id>            o autor ou um admin
//
//  Segredos (`wrangler secret put`):
//    GOOGLE_PLAY_SERVICE_ACCOUNT   JSON da conta de serviço com acesso à Play
//    APPLE_IAP_KEY_ID, APPLE_IAP_ISSUER_ID, APPLE_IAP_PRIVATE_KEY (.p8), APPLE_BUNDLE_ID
//  Denúncia e remoção usam a moderação da Comunidade (kind "mural").
// =============================================================================
import { lerSessao, lerJson, json, dentroDoLimite, sha256hex } from './contas.js';

export const MURAL_PRODUCT = 'aurea_mural_post';
// O Worker recusa corpo acima de 100 MB.
export const MURAL_MAX_VIDEO = 95 * 1024 * 1024;
const MAX_THUMB = 1024 * 1024;
const MAX_CAPTION = 200;
const HIDE_AFTER = 3;
const PAGE = 20;
const PLAY_PACKAGE = 'com.aurea.editor';
const UUID = /^[0-9a-f-]{36}$/;

const problem = (error, status = 400) => json({ error }, status);
const stmt = (env, sql, ...params) => env.AUREA_DB.prepare(sql).bind(...params);
const videoKey = (id) => `mural/${id}/video.mp4`;
const thumbKey = (id) => `mural/${id}/thumb.jpg`;
const b64url = (bytes) => btoa(String.fromCharCode(...new Uint8Array(bytes))).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
const b64urlText = (text) => b64url(new TextEncoder().encode(text));
const pemBytes = (pem) => Uint8Array.from(atob(pem.replace(/-----[^-]+-----/g, '').replace(/\s+/g, '')), (c) => c.charCodeAt(0));
const payments = (env) => !!env.GOOGLE_PLAY_SERVICE_ACCOUNT
  || !!(env.APPLE_IAP_KEY_ID && env.APPLE_IAP_ISSUER_ID && env.APPLE_IAP_PRIVATE_KEY && env.APPLE_BUNDLE_ID);

async function signedJwt(header, claims, key, algorithm) {
  const body = `${b64urlText(JSON.stringify(header))}.${b64urlText(JSON.stringify(claims))}`;
  return `${body}.${b64url(await crypto.subtle.sign(algorithm, key, new TextEncoder().encode(body)))}`;
}

async function googleAccessToken(env) {
  const account = JSON.parse(env.GOOGLE_PLAY_SERVICE_ACCOUNT);
  const key = await crypto.subtle.importKey('pkcs8', pemBytes(account.private_key),
    { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' }, false, ['sign']);
  const now = Math.floor(Date.now() / 1000);
  const assertion = await signedJwt({ alg: 'RS256', typ: 'JWT' }, { iss: account.client_email,
    scope: 'https://www.googleapis.com/auth/androidpublisher', aud: 'https://oauth2.googleapis.com/token',
    iat: now, exp: now + 600 }, key, 'RSASSA-PKCS1-v1_5');
  const r = await fetch('https://oauth2.googleapis.com/token', { method: 'POST',
    headers: { 'content-type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ grant_type: 'urn:ietf:params:oauth:grant-type:jwt-bearer', assertion }) });
  if (!r.ok) throw new Error('google_auth_failed');
  return (await r.json()).access_token;
}

/** A compra existe, é deste produto e foi paga? A Play diz; o app só traz o token. */
async function verifyGoogle(env, token) {
  if (!env.GOOGLE_PLAY_SERVICE_ACCOUNT) return 'payments_unavailable';
  const access = await googleAccessToken(env);
  const r = await fetch(`https://androidpublisher.googleapis.com/androidpublisher/v3/applications/${PLAY_PACKAGE}`
    + `/purchases/products/${MURAL_PRODUCT}/tokens/${encodeURIComponent(token)}`, { headers: { authorization: `Bearer ${access}` } });
  if (r.status === 404 || r.status === 400 || r.status === 410) return 'purchase_invalid';
  if (!r.ok) throw new Error('google_verify_failed');
  return (await r.json()).purchaseState === 0 ? null : 'purchase_invalid';
}

/** A App Store responde pela transação; a resposta vem dela por TLS, com chave nossa. */
async function verifyApple(env, transactionId) {
  if (!(env.APPLE_IAP_KEY_ID && env.APPLE_IAP_ISSUER_ID && env.APPLE_IAP_PRIVATE_KEY && env.APPLE_BUNDLE_ID)) return 'payments_unavailable';
  if (!/^\d{1,32}$/.test(transactionId)) return 'purchase_invalid';
  const key = await crypto.subtle.importKey('pkcs8', pemBytes(env.APPLE_IAP_PRIVATE_KEY),
    { name: 'ECDSA', namedCurve: 'P-256' }, false, ['sign']);
  const now = Math.floor(Date.now() / 1000);
  const jwt = await signedJwt({ alg: 'ES256', kid: env.APPLE_IAP_KEY_ID, typ: 'JWT' }, { iss: env.APPLE_IAP_ISSUER_ID,
    iat: now, exp: now + 600, aud: 'appstoreconnect-v1', bid: env.APPLE_BUNDLE_ID }, key, { name: 'ECDSA', hash: 'SHA-256' });
  for (const host of ['api.storekit.itunes.apple.com', 'api.storekit-sandbox.itunes.apple.com']) {
    const r = await fetch(`https://${host}/inApps/v1/transactions/${transactionId}`, { headers: { authorization: `Bearer ${jwt}` } });
    if (r.status === 404) continue;
    if (!r.ok) throw new Error('apple_verify_failed');
    const signed = (await r.json()).signedTransactionInfo ?? '';
    const part = signed.split('.')[1] ?? '';
    const info = JSON.parse(new TextDecoder().decode(Uint8Array.from(atob(part.replace(/-/g, '+').replace(/_/g, '/')), (c) => c.charCodeAt(0))));
    return info.productId === MURAL_PRODUCT && info.bundleId === env.APPLE_BUNDLE_ID && !info.revocationDate ? null : 'purchase_invalid';
  }
  return 'purchase_invalid';
}

async function addCredit(req, env, session) {
  const { valor: v, erro } = await lerJson(req, 8192); if (erro) return problem(erro);
  const platform = v?.platform, token = typeof v?.token === 'string' ? v.token.trim() : '';
  if (!['google', 'apple'].includes(platform) || !token || token.length > 4096) return problem('purchase_invalid');
  const hash = await sha256hex(`${platform}:${token}`);
  // A mesma compra nunca vale duas vezes, e repetir o pedido devolve o mesmo crédito:
  // o app pode cair entre pagar e publicar sem perder o que pagou.
  const known = await stmt(env, 'SELECT id, uid, post_id FROM mural_credits WHERE purchase_hash=?', hash).first();
  if (known) return known.uid === session.uid ? json({ id: known.id, postId: known.post_id }) : problem('purchase_in_use', 409);
  let failure;
  try { failure = platform === 'google' ? await verifyGoogle(env, token) : await verifyApple(env, token); }
  catch { return problem('payments_unavailable', 503); }
  if (failure) return problem(failure, failure === 'payments_unavailable' ? 503 : 402);
  const id = crypto.randomUUID();
  await stmt(env, 'INSERT INTO mural_credits(id,uid,platform,purchase_hash,created_at) VALUES(?,?,?,?,?)',
    id, session.uid, platform, hash, Date.now()).run();
  return json({ id, postId: null }, 201);
}

async function createPost(req, env, session) {
  if (!env.COMMUNITY_FILES) return problem('storage_unavailable', 503);
  const { valor: v, erro } = await lerJson(req, 4096); if (erro) return problem(erro);
  const caption = typeof v?.caption === 'string' ? v.caption.trim() : '';
  const bytes = v?.bytes, width = v?.width, height = v?.height, duration = v?.durationMs;
  const whole = (n, max) => Number.isSafeInteger(n) && n > 0 && n <= max;
  if ([...caption].length > MAX_CAPTION || !whole(width, 8192) || !whole(height, 8192) || !whole(duration, 600000)) return problem('invalid_post');
  if (!whole(bytes, MURAL_MAX_VIDEO)) return problem('file_too_large', 413);
  const owner = await stmt(env, 'SELECT username FROM community_profiles WHERE uid=?', session.uid).first();
  if (!owner?.username) return problem('profile_required', 403);
  const credit = await stmt(env, 'SELECT id, post_id FROM mural_credits WHERE id=? AND uid=?', v.creditId ?? '', session.uid).first();
  if (!credit) return problem('payment_required', 402);
  if (credit.post_id) {
    const open = await stmt(env, "SELECT id FROM mural_posts WHERE id=? AND status='uploading'", credit.post_id).first();
    if (!open) return problem('credit_used', 409);
    await stmt(env, 'UPDATE mural_posts SET caption=?,bytes=?,width=?,height=?,duration_ms=? WHERE id=?',
      caption, bytes, width, height, duration, open.id).run();
    return json({ id: open.id });
  }
  const id = crypto.randomUUID();
  const claimed = await stmt(env, 'UPDATE mural_credits SET post_id=? WHERE id=? AND post_id IS NULL', id, credit.id).run();
  if ((claimed?.meta?.changes ?? 1) < 1) return problem('credit_used', 409);
  await stmt(env, `INSERT INTO mural_posts(id,uid,credit,caption,status,bytes,width,height,duration_ms,created_at)
    VALUES(?,?,?,?,'uploading',?,?,?,?,?)`, id, session.uid, credit.id, caption, bytes, width, height, duration, Date.now()).run();
  return json({ id }, 201);
}

async function ownPost(env, session, id) {
  return stmt(env, "SELECT id, bytes, status FROM mural_posts WHERE id=? AND uid=? AND status<>'removed'", id, session.uid).first();
}

async function putFile(req, env, session, id, kind) {
  if (!env.COMMUNITY_FILES) return problem('storage_unavailable', 503);
  const post = await ownPost(env, session, id);
  if (!post) return problem('not_found', 404);
  if (post.status !== 'uploading') return problem('already_published', 409);
  const length = Number(req.headers.get('content-length'));
  const limit = kind === 'video' ? post.bytes : MAX_THUMB;
  if (!Number.isSafeInteger(length) || length <= 0 || length > limit || (kind === 'video' && length !== post.bytes) || !req.body)
    return problem('file_too_large', 413);
  const type = kind === 'video' ? 'video/mp4' : 'image/jpeg';
  await env.COMMUNITY_FILES.put(kind === 'video' ? videoKey(id) : thumbKey(id), req.body, { httpMetadata: { contentType: type } });
  return json({ ok: true });
}

async function publish(env, session, id) {
  const post = await ownPost(env, session, id);
  if (!post) return problem('not_found', 404);
  if (post.status === 'live') return json({ id });
  const video = await env.COMMUNITY_FILES?.get(videoKey(id), { range: { offset: 0, length: 12 } });
  const head = video ? new Uint8Array(await new Response(video.body).arrayBuffer()) : new Uint8Array();
  // MP4 e MOV têm "ftyp" no byte 4: o resto não toca nos dois apps.
  if (String.fromCharCode(...head.subarray(4, 8)) !== 'ftyp' || video.size !== post.bytes) return problem('invalid_file');
  if (!(await env.COMMUNITY_FILES.get(thumbKey(id), { range: { offset: 0, length: 1 } }))) return problem('invalid_file');
  await stmt(env, "UPDATE mural_posts SET status='live', created_at=? WHERE id=?", Date.now(), id).run();
  return json({ id });
}

const hiddenSql = `(SELECT COUNT(*) FROM community_reports r WHERE r.target_kind='mural' AND r.target_id=p.id AND r.status='open')`;

async function feed(env, session, url) {
  const viewer = session?.uid ?? '';
  const m = /^(\d{1,16}):([0-9a-f-]{36})$/.exec(url.searchParams.get('cursor') ?? '');
  const params = [viewer, HIDE_AFTER, viewer, viewer];
  let after = '';
  if (m) { after = 'AND (p.created_at<? OR (p.created_at=? AND p.id<?))'; params.push(Number(m[1]), Number(m[1]), m[2]); }
  const rows = await stmt(env, `SELECT p.id,p.uid,p.caption,p.width,p.height,p.duration_ms,p.created_at,
      f.username,f.name,f.avatar,f.verification
    FROM mural_posts p JOIN community_profiles f ON f.uid=p.uid
    WHERE p.status='live' AND (p.uid=? OR ${hiddenSql}<?)
      AND NOT EXISTS(SELECT 1 FROM community_blocks b WHERE (b.blocker=? AND b.blocked=p.uid) OR (b.blocked=? AND b.blocker=p.uid))
      ${after}
    ORDER BY p.created_at DESC, p.id DESC LIMIT ${PAGE}`, ...params).all();
  const items = rows.results.map((r) => ({ id: r.id, caption: r.caption, width: r.width, height: r.height, durationMs: r.duration_ms,
    createdAt: r.created_at, mine: r.uid === viewer,
    author: { id: r.uid, username: r.username, name: r.name, avatar: r.avatar, verification: r.verification } }));
  return json({ items, cursor: items.length === PAGE ? `${items.at(-1).createdAt}:${items.at(-1).id}` : null });
}

async function media(req, env, id, kind) {
  if (!env.COMMUNITY_FILES) return problem('not_found', 404);
  const post = await stmt(env, `SELECT p.id FROM mural_posts p WHERE p.id=? AND p.status='live' AND ${hiddenSql}<?`, id, HIDE_AFTER).first();
  if (!post) return problem('not_found', 404);
  const key = kind === 'video' ? videoKey(id) : thumbKey(id);
  const type = kind === 'video' ? 'video/mp4' : 'image/jpeg';
  const m = /^bytes=(\d{1,15})-(\d{0,15})$/.exec(req.headers.get('range') ?? '');
  const base = { 'content-type': type, 'accept-ranges': 'bytes', 'cache-control': 'public, max-age=300', 'x-content-type-options': 'nosniff' };
  const whole = await env.COMMUNITY_FILES.get(key, m ? { range: { offset: Number(m[1]), length: 1 } } : undefined);
  if (!whole) return problem('not_found', 404);
  if (!m) return new Response(req.method === 'HEAD' ? null : whole.body, { headers: { ...base, 'content-length': String(whole.size) } });
  const start = Number(m[1]), end = Math.min(m[2] === '' ? whole.size - 1 : Number(m[2]), whole.size - 1);
  if (start > end) return new Response(null, { status: 416, headers: { 'content-range': `bytes */${whole.size}` } });
  const part = await env.COMMUNITY_FILES.get(key, { range: { offset: start, length: end - start + 1 } });
  return new Response(req.method === 'HEAD' ? null : part.body, { status: 206,
    headers: { ...base, 'content-length': String(end - start + 1), 'content-range': `bytes ${start}-${end}/${whole.size}` } });
}

/** Tira o vídeo e a capa do armazenamento. O registro fica como removido. */
export async function removeMuralPost(env, id) {
  await stmt(env, "UPDATE mural_posts SET status='removed' WHERE id=?", id).run();
  await env.COMMUNITY_FILES?.delete(videoKey(id));
  await env.COMMUNITY_FILES?.delete(thumbKey(id));
}

async function removeOwn(env, session, id) {
  const post = await stmt(env, 'SELECT uid FROM mural_posts WHERE id=?', id).first();
  if (!post) return problem('not_found', 404);
  const admin = !!(await stmt(env, "SELECT uid FROM account_roles WHERE uid=? AND role='community_admin'", session.uid).first());
  if (post.uid !== session.uid && !admin) return problem('forbidden', 403);
  await removeMuralPost(env, id);
  return json({ ok: true });
}

export async function muralRoute(req, env, ctx, url) {
  const path = url.pathname.replace(/\/+$/, '');
  if (!path.startsWith('/api/mural')) return null;
  if (url.protocol !== 'https:' && !['localhost', '127.0.0.1'].includes(url.hostname)) return problem('https_required', 403);
  if (!env.AUREA_DB) return problem('mural_unavailable', 503);
  try {
    if (path === '/api/mural/config' && req.method === 'GET')
      return json({ posting: !!env.COMMUNITY_FILES && payments(env), productId: MURAL_PRODUCT, maxBytes: MURAL_MAX_VIDEO });
    const file = /^\/api\/mural\/media\/([0-9a-f-]{36})\/(video|thumb)$/.exec(path);
    if (file && ['GET', 'HEAD'].includes(req.method)) return await media(req, env, file[1], file[2]);
    const session = await lerSessao(env, req);
    if (path === '/api/mural/feed' && req.method === 'GET') return await feed(env, session, url);
    if (!session) return problem('unauthorized', 401);
    if (!(await dentroDoLimite(env, 'mural-write', session.uid, { max: 60, janela: 3600 }))) return problem('rate_limited', 429);
    if (path === '/api/mural/credits' && req.method === 'POST') return await addCredit(req, env, session);
    if (path === '/api/mural/posts' && req.method === 'POST') return await createPost(req, env, session);
    const upload = /^\/api\/mural\/posts\/([0-9a-f-]{36})\/(video|thumb)$/.exec(path);
    if (upload && req.method === 'PUT') return await putFile(req, env, session, upload[1], upload[2]);
    const done = /^\/api\/mural\/posts\/([0-9a-f-]{36})\/publish$/.exec(path);
    if (done && req.method === 'POST') return await publish(env, session, done[1]);
    const one = /^\/api\/mural\/posts\/([0-9a-f-]{36})$/.exec(path);
    if (one && UUID.test(one[1]) && req.method === 'DELETE') return await removeOwn(env, session, one[1]);
    return problem('not_found', 404);
  } catch {
    return problem('mural_unavailable', 503);
  }
}
