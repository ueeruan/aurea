-- Relatórios de crash do app (crash.js). No D1, não no KV: cada relatório fazia
-- 2 gravações no KV grátis (1 000/dia na conta inteira) e em 22/11/2026, com
-- ~550 relatórios por dia, o limite acabou e levou junto a config remota.
--
-- `report_gz` é o registro inteiro (JSON) em gzip: a pilha do iOS chega a 200 KiB
-- e 30 dias sem compressão passariam dos 500 MB do D1 grátis (~35 KB → ~2 KB).
-- As colunas soltas são as que a lista do admin mostra sem abrir o gzip.
CREATE TABLE IF NOT EXISTS crash_reports (
  id TEXT PRIMARY KEY,               -- <ordem decrescente>-<hash>: ORDER BY id = mais novos primeiro
  dedupe TEXT NOT NULL UNIQUE,       -- sha256(instalação|reportId): reenvio não duplica
  received_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,       -- received_at + CRASH_TTL_DAYS
  platform TEXT NOT NULL,
  reason TEXT NOT NULL DEFAULT '',
  app_build TEXT NOT NULL DEFAULT '',
  device TEXT NOT NULL DEFAULT '',
  emailed INTEGER NOT NULL DEFAULT 0,
  email_via TEXT,
  email_error TEXT,
  report_gz BLOB NOT NULL
);
CREATE INDEX IF NOT EXISTS crash_reports_expires ON crash_reports (expires_at);
