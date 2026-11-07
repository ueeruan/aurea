-- Mural (0.4.8): edits em vídeo que todo mundo vê na tela inicial. Ver é grátis;
-- cada publicação consome um crédito comprado na Play ou na App Store. Publica na
-- hora e sai do ar com denúncias (community_reports, target_kind = 'mural').
CREATE TABLE IF NOT EXISTS mural_credits (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  platform TEXT NOT NULL CHECK (platform IN ('google', 'apple')),
  purchase_hash TEXT NOT NULL UNIQUE,
  post_id TEXT,
  created_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS mural_credits_owner ON mural_credits(uid, created_at);
CREATE TABLE IF NOT EXISTS mural_posts (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL REFERENCES community_profiles(uid) ON DELETE CASCADE,
  credit TEXT NOT NULL REFERENCES mural_credits(id),
  caption TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL CHECK (status IN ('uploading', 'live', 'removed')),
  bytes INTEGER NOT NULL,
  width INTEGER NOT NULL,
  height INTEGER NOT NULL,
  duration_ms INTEGER NOT NULL,
  created_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS mural_posts_feed ON mural_posts(status, created_at DESC, id DESC);
CREATE INDEX IF NOT EXISTS mural_posts_owner ON mural_posts(uid, created_at);

-- Denúncia do Mural: o CHECK de target_kind ganha 'mural'. SQLite não altera CHECK;
-- a tabela é refeita. Sem isto o INSERT OR IGNORE engolia a denúncia em silêncio.
CREATE TABLE community_reports_v3 (
  id TEXT PRIMARY KEY,
  reporter TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  target_kind TEXT NOT NULL CHECK (target_kind IN ('post', 'comment', 'user', 'preset', 'ai_video', 'store', 'mural')),
  target_id TEXT NOT NULL,
  reason TEXT NOT NULL CHECK (reason IN ('spam', 'nudity', 'violence', 'hate', 'harassment', 'illegal', 'copyright', 'other')),
  details TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'removed', 'dismissed')),
  created_at INTEGER NOT NULL,
  UNIQUE (reporter, target_kind, target_id)
);
INSERT INTO community_reports_v3 SELECT id, reporter, target_kind, target_id, reason, details, status, created_at FROM community_reports;
DROP TABLE community_reports;
ALTER TABLE community_reports_v3 RENAME TO community_reports;
CREATE INDEX IF NOT EXISTS community_reports_target ON community_reports(target_kind, target_id, status);
CREATE INDEX IF NOT EXISTS community_reports_open ON community_reports(status, created_at);
