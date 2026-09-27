# Contas obrigatórias e relatórios de crash — 27/09/2026

Worker: `aurea-ai-discovery` (`https://aurea-ai-discovery.aureaapp.workers.dev`).
Código em `discovery/contas.js`, `discovery/crash.js`, `discovery/email.js`;
testes em `discovery/test/contas.test.mjs` e `crash.test.mjs`
(`node --import ./test/cf-stub.mjs --test test/*.test.mjs`).

## Estado (27/09, 15h)

- D1 `aurea-contas` criado (`89dcf8de-9102-4a1a-a50d-1126174632b2`), migração
  `0001_contas.sql` aplicada, worker publicado (versão `d63ec0c6`).
- Testado ao vivo: cadastro, login, senha errada (`credenciais_invalidas`),
  `/api/stats/users` e `/api/crash` (armazena; `emailed:false` até configurar
  o e-mail abaixo). O usuário de teste foi apagado e o contador zerado.

## Endpoints

| Rota | Uso |
|---|---|
| `POST /api/auth/signup` `{email,password}` | cria a conta; devolve `{token,email,users}` |
| `POST /api/auth/login` `{email,password}` | devolve `{token,email}`; erro genérico `credenciais_invalidas` |
| `GET /api/auth/session` (Bearer) | revalida a sessão quando o app tem rede |
| `POST /api/auth/logout` (Bearer) | encerra a sessão |
| `GET /api/stats/users` | `{count}` — "N pessoas cadastradas" na Home |
| `POST /api/crash` | relatório do app (limitado por tamanho e por instalação) |
| `GET /api/crash/list`, `/api/crash/item?id=` | admin (Bearer `CRASH_ADMIN_TOKEN`) |

Segurança: senha só como hash PBKDF2-SHA256 (sal de 16 bytes, 100k iterações,
comparação em tempo constante); e-mail normalizado; mínimo 8 caracteres;
limite de tentativas por IP e por e-mail (KV com TTL); sessão = token
aleatório de 32 bytes guardado com hash no KV (180 dias). O app guarda só o
token + e-mail (Android: EncryptedSharedPreferences; iOS: Keychain).

## O que falta configurar (só o dono da conta consegue)

1. **E-mail dos relatórios de crash** — escolha UM caminho:
   - **Resend** (mais simples, sem domínio): crie uma chave em resend.com e
     rode `npx.cmd wrangler secret put RESEND_API_KEY` (cole a chave). O
     remetente `onboarding@resend.dev` só entrega para o e-mail dono da conta
     Resend — cadastre a Resend com `ruanpablombl@gmail.com`.
   - **Cloudflare Email Workers**: ative Email Routing num domínio da conta,
     verifique `ruanpablombl@gmail.com` como destino, descomente o bloco
     `[[send_email]]` e defina `CRASH_EMAIL_FROM` no `wrangler.toml`.
   Depois: `npx.cmd wrangler deploy`. Sem isso os relatórios ficam guardados
   30 dias e podem ser lidos pelo `/api/crash/list`.
2. **Token de admin** para ler os relatórios pela API:
   `npx.cmd wrangler secret put CRASH_ADMIN_TOKEN`.

No PowerShell use `;` no lugar de `&&` e `npx.cmd` (a política de scripts
bloqueia `npx.ps1`). Se o npm reclamar de cache (`ENOENT ... _cacache`),
rode `npm.cmd cache clean --force` antes.
