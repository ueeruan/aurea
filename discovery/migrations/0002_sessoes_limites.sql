-- Sessões e limites de tentativa no D1 (antes no KV, cujo plano grátis só
-- aceita 1 000 gravações/dia: em 28/09/2026 acabaram e todo cadastro falhou).
CREATE TABLE IF NOT EXISTS sessions (
  token_hash TEXT PRIMARY KEY,
  uid TEXT NOT NULL,
  email TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS rate_limits (
  key TEXT PRIMARY KEY,
  count INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);
