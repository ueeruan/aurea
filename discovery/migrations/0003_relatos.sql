-- Relatos de problema escritos pela pessoa no app ("Relatar um problema").
-- No D1, não no KV: o KV grátis só grava 1 000 vezes por dia na conta inteira.
CREATE TABLE IF NOT EXISTS user_reports (
  id TEXT PRIMARY KEY,
  dedupe TEXT NOT NULL UNIQUE,
  received_at INTEGER NOT NULL,
  platform TEXT NOT NULL,
  install_id TEXT NOT NULL,
  email TEXT NOT NULL DEFAULT '',
  country TEXT NOT NULL DEFAULT '',
  app_version TEXT NOT NULL DEFAULT '',
  app_build TEXT NOT NULL DEFAULT '',
  os TEXT NOT NULL DEFAULT '',
  os_version TEXT NOT NULL DEFAULT '',
  device_model TEXT NOT NULL DEFAULT '',
  manufacturer TEXT NOT NULL DEFAULT '',
  abi TEXT NOT NULL DEFAULT '',
  locale TEXT NOT NULL DEFAULT '',
  what_did TEXT NOT NULL DEFAULT '',
  what_happened TEXT NOT NULL,
  steps TEXT NOT NULL DEFAULT '',
  emailed INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS user_reports_received ON user_reports (received_at);
