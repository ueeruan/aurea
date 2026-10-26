-- Direitos pagos por conta (entitlements.js). Hoje só o AureaHyper
-- (`aureahyper_lifetime`), concedido à mão pelo admin (POST
-- /api/admin/entitlements); a cobrança entra depois, com outro `source`.
--
-- Uma linha = a conta TEM o produto. Revogar = apagar a linha (não há
-- `revoked_at`): o que não está aqui, a conta não tem. Quem foi concedido e
-- quando só fica registrado enquanto o direito vale.
--
-- A exclusão de conta (contas.js, excluirConta) apaga as linhas da conta de
-- forma explícita, na mesma transação que apaga o usuário. O ON DELETE CASCADE
-- é a segunda trava (o D1 aplica chaves estrangeiras por padrão).
CREATE TABLE IF NOT EXISTS entitlements (
  user_id TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  product TEXT NOT NULL,
  granted_at INTEGER NOT NULL,          -- segundos Unix
  source TEXT NOT NULL,                 -- 'admin' por enquanto
  note TEXT CHECK (note IS NULL OR length(note) <= 200),
  PRIMARY KEY (user_id, product)
);
-- A chave primária já atende "o que esta conta tem". Este índice atende
-- "quem tem este produto" (auditoria do admin).
CREATE INDEX IF NOT EXISTS entitlements_product ON entitlements(product, granted_at);
