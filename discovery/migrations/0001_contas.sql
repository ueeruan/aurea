-- Contas do Aurea (D1 "aurea-contas", binding AUREA_DB).
-- Aplicar: npx wrangler d1 migrations apply aurea-contas --remote
--
-- O e-mail chega normalizado (trim + minúsculas) do Worker; o UNIQUE com
-- NOCASE é a segunda trava: dois cadastros simultâneos do mesmo e-mail, um só
-- entra. A senha é só o hash PBKDF2 (pbkdf2_sha256$iter$sal$hash).
CREATE TABLE IF NOT EXISTS users (
  id TEXT PRIMARY KEY,
  email TEXT NOT NULL UNIQUE COLLATE NOCASE,
  password_hash TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  last_login_at INTEGER
);

-- Contador de cadastrados: somado na MESMA transação do INSERT (batch do D1),
-- então /api/stats/users lê uma linha em vez de contar a tabela inteira.
CREATE TABLE IF NOT EXISTS counters (
  name TEXT PRIMARY KEY,
  value INTEGER NOT NULL
);
INSERT OR IGNORE INTO counters (name, value) VALUES ('users', 0);
