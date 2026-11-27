import { dentroDoLimite, json } from './contas.js';

// 1.0.9: post photos and videos live in their own bucket (COMMUNITY_MEDIA), published
// through the Cloudflare CDN at COMMUNITY_MEDIA_URL (media.aureaeditor.com). The Worker
// still checks moderation and then redirects, so the bytes come from the edge near the
// viewer instead of Worker -> R2 (ENAM) on every range request. Keys carry an extension
// the CDN caches by default (mp4/jpg/png); the real type stays in httpMetadata.
// Without the bucket, everything falls back to COMMUNITY_FILES as before.
const MEDIA_CACHE = 'public, max-age=3600';
const extension = mime => mime.startsWith('video/') ? 'mp4' : mime === 'image/png' ? 'png' : 'jpg';
const mediaBucket = env => env.COMMUNITY_MEDIA ?? env.COMMUNITY_FILES;
const mediaKey = (env, id, mime) => env.COMMUNITY_MEDIA ? `community/media/${id}.${extension(mime)}` : `community/media/${id}`;
const error = (message, status = 400) => json({ error: message }, status);
const imageLimit = 8 * 1024 * 1024;
const videoLimit = 90 * 1024 * 1024;

export async function uploadCommunityMedia(req, env, uid, url, imageMime) {
  const bucket = mediaBucket(env);
  if (!bucket) return error('storage_unavailable', 503);
  const kind = url.searchParams.get('kind');
  const raw = url.searchParams.get('name')?.trim() ?? '';
  const name = [...raw.replace(/[\x00-\x1f/\\]/g, '_')].slice(0, 100).join('');
  if (!name || !['image', 'video'].includes(kind)) return error('invalid_file');
  const bytes = Number(req.headers.get('content-length'));
  const limit = kind === 'image' ? imageLimit : videoLimit;
  if (!req.body || !Number.isSafeInteger(bytes) || bytes < 1 || bytes > limit) return error('file_too_large', 413);
  if (!(await dentroDoLimite(env, 'community-upload', uid, { max: 20, janela: 3600 }))) return error('rate_limited', 429);

  const id = crypto.randomUUID();
  let mime, key;
  if (kind === 'image') {
    const data = new Uint8Array(bytes);
    let size = 0;
    for await (const chunk of req.body) {
      if (size + chunk.byteLength > bytes) return error('invalid_file');
      data.set(chunk, size); size += chunk.byteLength;
    }
    mime = size === bytes ? imageMime(data, 4096) : null;
    if (!mime) return error('invalid_image');
    key = mediaKey(env, id, mime);
    await bucket.put(key, data, { httpMetadata: { contentType: mime, cacheControl: MEDIA_CACHE } });
  } else {
    const mov = /\.mov$/i.test(name), mp4 = /\.m(p4|4v)$/i.test(name);
    if (!mov && !mp4) return error('invalid_video');
    mime = mov ? 'video/quicktime' : 'video/mp4';
    key = mediaKey(env, id, mime);
    const stream = typeof FixedLengthStream === 'function' ? new FixedLengthStream(bytes) : new TransformStream();
    const pump = async () => {
      const writer = stream.writable.getWriter();
      let size = 0;
      const prefix = [];
      try {
        for await (const chunk of req.body) {
          size += chunk.byteLength;
          if (size > bytes) throw new Error('size');
          for (let i = 0; prefix.length < 12 && i < chunk.length; ++i) prefix.push(chunk[i]);
          if (prefix.length === 12 && String.fromCharCode(...prefix.slice(4, 8)) !== 'ftyp') throw new Error('format');
          await writer.write(chunk);
        }
        if (size !== bytes || prefix.length !== 12) throw new Error('size');
        await writer.close();
      } catch (cause) { await writer.abort(cause); throw cause; }
      finally { writer.releaseLock(); }
    };
    const save = bucket.put(key, stream.readable, { httpMetadata: { contentType: mime, cacheControl: MEDIA_CACHE } })
      .catch(async cause => { await stream.readable.cancel(cause).catch(() => {}); throw cause; });
    try { await Promise.all([pump(), save]); }
    catch { await Promise.allSettled([save]); await bucket.delete(key); return error('invalid_video'); }
  }
  try {
    await env.AUREA_DB.prepare('INSERT INTO community_media(id, uid, kind, name, mime, bytes, created_at) VALUES(?,?,?,?,?,?,?)')
      .bind(id, uid, kind, name, mime, bytes, Date.now()).run();
  } catch (cause) { await bucket.delete(key); throw cause; }
  return json({ id, kind, name, bytes }, 201);
}

export async function readCommunityMedia(req, env, id) {
  const bucket = mediaBucket(env);
  if (!bucket) return error('not_found', 404);
  const media = await env.AUREA_DB.prepare(`SELECT m.mime, m.bytes FROM community_posts p JOIN community_media m ON p.media=m.id
    WHERE p.media=? AND (SELECT COUNT(*) FROM community_reports r WHERE r.target_kind='post' AND r.target_id=p.id AND r.status='open') < 3`).bind(id).first();
  if (!media) return error('not_found', 404);
  const cdn = env.COMMUNITY_MEDIA && /^https:\/\/[a-z0-9.-]+$/.test(env.COMMUNITY_MEDIA_URL ?? '') ? env.COMMUNITY_MEDIA_URL : '';
  if (cdn) {
    // The redirect itself is never cached: moderation is checked again on the next open.
    return new Response(null, { status: 302, headers: {
      location: `${cdn}/${mediaKey(env, id, media.mime)}`, 'cache-control': 'no-store', 'x-content-type-options': 'nosniff',
    } });
  }
  const range = /^bytes=(\d{0,15})-(\d{0,15})$/.exec(req.headers.get('range') ?? '');
  let start = 0, end = media.bytes - 1;
  const partial = range && (range[1] || range[2]);
  if (partial) {
    if (!range[1]) start = Math.max(0, media.bytes - Number(range[2]));
    else {
      start = Number(range[1]);
      if (range[2]) end = Math.min(end, Number(range[2]));
    }
    if (start > end) return new Response(null, { status: 416, headers: { 'content-range': `bytes */${media.bytes}` } });
  }
  const length = end - start + 1;
  const key = mediaKey(env, id, media.mime);
  const object = req.method === 'HEAD' ? await bucket.head(key)
    : await bucket.get(key, partial ? { range: { offset: start, length } } : undefined);
  if (!object) return error('not_found', 404);
  const headers = {
    'content-type': media.mime, 'accept-ranges': 'bytes', 'content-length': String(length),
    'cache-control': 'public, max-age=300', 'x-content-type-options': 'nosniff',
    'content-security-policy': "default-src 'none'; sandbox",
  };
  if (object.httpEtag) headers.etag = object.httpEtag;
  if (partial) headers['content-range'] = `bytes ${start}-${end}/${media.bytes}`;
  return new Response(req.method === 'HEAD' ? null : object.body, { status: partial ? 206 : 200, headers });
}

export async function deleteCommunityMedia(env, id) {
  if (!id) return;
  // The row is usually gone already (trigger), so the extension is unknown: remove every form.
  if (env.COMMUNITY_MEDIA) await env.COMMUNITY_MEDIA.delete(['mp4', 'jpg', 'png'].map(ext => `community/media/${id}.${ext}`));
  await env.COMMUNITY_FILES?.delete(`community/media/${id}`);
}
