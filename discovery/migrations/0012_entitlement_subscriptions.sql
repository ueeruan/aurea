-- Assinaturas mensais (16/11/2026, decisão do dono): AureaHyper (aurea_hyper_mensal)
-- e cada efeito pago / Texto 3D avulso (aurea_fx_*, aurea_texto3d). Ver
-- docs/release/assinaturas.md e entitlements.js.
--
-- ADITIVA: só colunas novas e anuláveis em `entitlements`. Linhas que já existem
-- (concessões do admin, AureaOS) ficam com expires_at NULL = valem para sempre,
-- como antes. Uma linha de assinatura guarda até quando o período pago vale e o
-- que se manda à loja para conferir a renovação.
--
-- Aplicar ANTES do deploy do Worker que usa estas colunas, depois do `wrangler d1 export`
-- (regra da casa). Só o dono aplica:
--   npx wrangler d1 migrations apply aurea-contas --remote
ALTER TABLE entitlements ADD COLUMN expires_at INTEGER;        -- segundos Unix; NULL = sem vencimento
ALTER TABLE entitlements ADD COLUMN store_ref TEXT;            -- purchaseToken da Play / originalTransactionId da Apple
ALTER TABLE entitlements ADD COLUMN store_checked_at INTEGER;  -- última conferência na loja (segundos Unix)
-- "Quais assinaturas venceram e precisam de nova conferência."
CREATE INDEX IF NOT EXISTS entitlements_expires ON entitlements(expires_at) WHERE expires_at IS NOT NULL;
