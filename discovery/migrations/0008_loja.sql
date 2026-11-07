-- Loja da Comunidade (0.4.6): plugins, efeitos e presets feitos pela
-- comunidade. O criador manda o pacote (.aureaplugin ou preset .json), nome,
-- descrição, link de tutorial e até 4 fotos de exemplo. Plugin entra
-- PENDENTE e só aparece depois que um admin aprova; preset entra aprovado.
CREATE TABLE IF NOT EXISTS community_store_files (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  role TEXT NOT NULL CHECK (role IN ('package', 'photo')),
  name TEXT NOT NULL,
  mime TEXT NOT NULL,
  bytes INTEGER NOT NULL,
  meta TEXT NOT NULL DEFAULT '{}',
  created_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS community_store_files_owner ON community_store_files(uid, created_at);
CREATE TABLE IF NOT EXISTS community_store_items (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL REFERENCES community_profiles(uid) ON DELETE CASCADE,
  type TEXT NOT NULL CHECK (type IN ('plugin', 'effect', 'preset')),
  format TEXT NOT NULL CHECK (format IN ('plugin', 'preset')),
  name TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  tutorial_url TEXT NOT NULL DEFAULT '',
  package TEXT NOT NULL REFERENCES community_store_files(id),
  photos TEXT NOT NULL DEFAULT '[]',
  plugin_id TEXT,
  version TEXT NOT NULL DEFAULT '',
  preset_kind TEXT,
  permissions TEXT NOT NULL DEFAULT '{}',
  has_script INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'approved', 'rejected')),
  review_note TEXT NOT NULL DEFAULT '',
  downloads INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS community_store_new ON community_store_items(status, created_at DESC, id DESC);
CREATE INDEX IF NOT EXISTS community_store_popular ON community_store_items(status, downloads DESC, id DESC);
CREATE INDEX IF NOT EXISTS community_store_author ON community_store_items(uid, created_at DESC);
CREATE INDEX IF NOT EXISTS community_store_plugin ON community_store_items(plugin_id);
CREATE TABLE IF NOT EXISTS community_store_likes (
  item TEXT NOT NULL REFERENCES community_store_items(id) ON DELETE CASCADE,
  uid TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  PRIMARY KEY (item, uid)
);

-- Denúncia de item da loja: o CHECK de target_kind ganha 'store'. SQLite não
-- altera CHECK; a tabela é refeita (nada referencia community_reports).
CREATE TABLE community_reports_v2 (
  id TEXT PRIMARY KEY,
  reporter TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  target_kind TEXT NOT NULL CHECK (target_kind IN ('post', 'comment', 'user', 'preset', 'ai_video', 'store')),
  target_id TEXT NOT NULL,
  reason TEXT NOT NULL CHECK (reason IN ('spam', 'nudity', 'violence', 'hate', 'harassment', 'illegal', 'copyright', 'other')),
  details TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'removed', 'dismissed')),
  created_at INTEGER NOT NULL,
  UNIQUE (reporter, target_kind, target_id)
);
INSERT INTO community_reports_v2 SELECT id, reporter, target_kind, target_id, reason, details, status, created_at FROM community_reports;
DROP TABLE community_reports;
ALTER TABLE community_reports_v2 RENAME TO community_reports;
CREATE INDEX IF NOT EXISTS community_reports_target ON community_reports(target_kind, target_id, status);
CREATE INDEX IF NOT EXISTS community_reports_open ON community_reports(status, created_at);
