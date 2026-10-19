-- Moderação da Comunidade (Google Play: política de conteúdo gerado pelo
-- usuário e de IA generativa). Denunciar post, comentário, perfil, preset de
-- legenda e vídeo gerado por IA; bloquear perfis. Um conteúdo denunciado por
-- 3 pessoas diferentes sai do feed até o admin decidir (moderation.js).
CREATE TABLE IF NOT EXISTS community_reports (
  id TEXT PRIMARY KEY,
  reporter TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  target_kind TEXT NOT NULL CHECK (target_kind IN ('post', 'comment', 'user', 'preset', 'ai_video')),
  target_id TEXT NOT NULL,
  reason TEXT NOT NULL CHECK (reason IN ('spam', 'nudity', 'violence', 'hate', 'harassment', 'illegal', 'copyright', 'other')),
  details TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'open' CHECK (status IN ('open', 'removed', 'dismissed')),
  created_at INTEGER NOT NULL,
  UNIQUE (reporter, target_kind, target_id)
);
CREATE INDEX IF NOT EXISTS community_reports_target ON community_reports(target_kind, target_id, status);
CREATE INDEX IF NOT EXISTS community_reports_open ON community_reports(status, created_at);
CREATE TABLE IF NOT EXISTS community_blocks (
  blocker TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  blocked TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  created_at INTEGER NOT NULL,
  PRIMARY KEY (blocker, blocked),
  CHECK (blocker != blocked)
);
CREATE INDEX IF NOT EXISTS community_blocked_by ON community_blocks(blocked, blocker);
