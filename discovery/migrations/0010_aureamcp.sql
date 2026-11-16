-- AureaMCP (docs/aureamcp/DESIGN.md): agentes de IA (Claude, ChatGPT, Codex...)
-- controlam o Aurea da própria conta pelo MCP do Worker. Nada aqui guarda token
-- cru: só o SHA-256. Aplicar só depois do `wrangler d1 export` (regra da casa).

-- Clientes OAuth do registro dinâmico (RFC 7591). Públicos: sem segredo.
CREATE TABLE IF NOT EXISTS mcp_clients (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  redirect_uris TEXT NOT NULL,          -- JSON: lista de URIs exatas
  created_at INTEGER NOT NULL
);

-- Autorizações pendentes: a página mostra o código; o app (com sessão) aprova.
CREATE TABLE IF NOT EXISTS mcp_auth_requests (
  id TEXT PRIMARY KEY,
  client_id TEXT NOT NULL REFERENCES mcp_clients(id) ON DELETE CASCADE,
  redirect_uri TEXT NOT NULL,
  code_challenge TEXT NOT NULL,
  state TEXT NOT NULL DEFAULT '',
  scope TEXT NOT NULL DEFAULT '',
  resource TEXT NOT NULL DEFAULT '',
  user_code_hash TEXT NOT NULL UNIQUE,
  browser_hash TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending', 'approved', 'denied', 'issued', 'used')),
  uid TEXT REFERENCES users(id) ON DELETE CASCADE,
  code_hash TEXT UNIQUE,
  code_expires_at INTEGER,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS mcp_auth_requests_expiry ON mcp_auth_requests(expires_at);

-- Credenciais dos agentes: token pessoal ('pat', Claude Code/Codex) ou OAuth.
CREATE TABLE IF NOT EXISTS mcp_tokens (
  id TEXT PRIMARY KEY,
  uid TEXT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  kind TEXT NOT NULL CHECK (kind IN ('pat', 'oauth')),
  name TEXT NOT NULL,
  client_id TEXT,
  token_hash TEXT NOT NULL UNIQUE,
  token_expires_at INTEGER NOT NULL,
  refresh_hash TEXT UNIQUE,
  refresh_expires_at INTEGER,
  -- O refresh anterior (já trocado): reaparecer = vazou; a concessão inteira cai.
  prev_refresh_hash TEXT UNIQUE,
  scope TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL,
  last_used_at INTEGER,
  revoked_at INTEGER
);
CREATE INDEX IF NOT EXISTS mcp_tokens_owner ON mcp_tokens(uid, created_at);
