# Configuração remota assinada e AureaHyper (0.3.2)

O Worker (`discovery/appconfig.js`) entrega por plataforma uma config **assinada**
que os apps conferem com a chave pública embutida
(`android/.../config/AppConfig.kt`, `engine/platform/ios/app/AppConfig.swift`):

- `minVersionCode`: abaixo dele o app mostra só a tela **Atualização obrigatória**
  (o `versionCode` do Android / `CFBundleVersion` do iOS). Sem nenhuma config
  recebida, o app não bloqueia. A última config verificada continua valendo offline.
- `hyper.mode`:
  - `free`: AureaHyper liberado para todos, mas só com uma config emitida há no máximo 7 dias.
  - `entitled`: o portão por conta (`/api/entitlements`).
  - `off`: bloqueado, com o `message` da config.
- `message`: aviso opcional na Home (pt-BR, en, es, hi, id, ru, ar).
- `updateUrl`: para onde o botão de atualizar leva. O padrão do Android é a Play;
  o do iOS é vazio, e aí a tela diz para instalar pelo mesmo lugar de onde o app veio.

Assinatura: ECDSA P-256 com SHA-256 sobre os bytes UTF-8 do campo `payload`,
no formato cru r||s (64 bytes) em base64. A resposta é
`{"payload": "<json>", "signature": "<base64>"}`.

## Chave

A chave foi gerada uma vez com `node discovery/scripts/gen-config-key.mjs <arquivo>`.
A privada está em `build/release-signing/app-config-private-key.txt`. A pasta
`build/` não entra no git; não copie a chave para nenhum arquivo versionado. A
pública está embutida nos dois apps. Trocar a chave exige publicar apps novos com
a pública nova, antes de trocar o secret.

## Deploy (em `discovery/`)

1. Migração dos direitos (se ainda não foi aplicada):
   `npx wrangler d1 migrations apply aurea-contas --remote`
2. Secrets:
   - `npx wrangler secret put AUREA_ADMIN_SECRET`
     (valor aleatório com 16 caracteres ou mais, por exemplo `openssl rand -base64 32`)
   - `npx wrangler secret put AUREA_CONFIG_SIGNING_KEY`
     (cole o conteúdo de `build/release-signing/app-config-private-key.txt`)
3. Publicar **sem o R2**. A produção roda sem o bucket, e `wrangler deploy` direto falha:
   - copie `wrangler.toml` para `wrangler.sem-r2.toml`;
   - na cópia, apague o bloco `[[r2_buckets]]`;
   - rode `npx wrangler deploy -c wrangler.sem-r2.toml`;
   - apague a cópia.

Sem `AUREA_CONFIG_SIGNING_KEY`, a rota responde 503 e os apps continuam com a
última config válida: nada é bloqueado por isso, e o AureaHyper `free` cai para
"conecte-se" depois de 7 dias.

## Comandos (admin)

Use `HOST=https://aurea-ai-discovery.aureaapp.workers.dev` e
`AUREA_ADMIN_SECRET` no ambiente. O POST só muda os campos enviados; o resto
fica como estava.

Forçar atualização no Android para versionCode N (por exemplo 2199):

```
curl -X POST "$HOST/api/admin/app-config" -H "Authorization: Bearer $AUREA_ADMIN_SECRET" \
  -H "content-type: application/json" \
  -d '{"platform":"android","minVersionCode":2199,"latestVersionCode":2199}'
```

O mesmo no iOS, com o link da IPA ou da loja:

```
curl -X POST "$HOST/api/admin/app-config" -H "Authorization: Bearer $AUREA_ADMIN_SECRET" \
  -H "content-type: application/json" \
  -d '{"platform":"ios","minVersionCode":2199,"updateUrl":"https://exemplo.com/aurea.ipa"}'
```

AureaHyper por conta (pago):

```
curl -X POST "$HOST/api/admin/app-config" -H "Authorization: Bearer $AUREA_ADMIN_SECRET" \
  -H "content-type: application/json" -d '{"platform":"android","hyperMode":"entitled"}'
```

Desligar o AureaHyper, com um aviso:

```
curl -X POST "$HOST/api/admin/app-config" -H "Authorization: Bearer $AUREA_ADMIN_SECRET" \
  -H "content-type: application/json" \
  -d '{"platform":"android","hyperMode":"off","message":{"pt-BR":"O AureaHyper volta em breve.","en":"AureaHyper will be back soon."}}'
```

Voltar ao grátis e apagar o aviso:

```
curl -X POST "$HOST/api/admin/app-config" -H "Authorization: Bearer $AUREA_ADMIN_SECRET" \
  -H "content-type: application/json" -d '{"platform":"android","hyperMode":"free","message":null}'
```

Repita com `"platform":"ios"` para o iOS. Para conferir o que os apps recebem:
`curl "$HOST/api/app-config?platform=android"`.

Outros campos:
- `ttlDays` (1–30, padrão 7): validade de cada resposta.
- Valores aceitos: `hyperMode` `free`/`entitled`/`off`; `updateUrl` precisa ser https
  ou vazio; `message` aceita no máximo 500 caracteres por idioma.

Conceder o AureaHyper a uma conta (modo `entitled`):

```
curl -X POST "$HOST/api/admin/entitlements" -H "Authorization: Bearer $AUREA_ADMIN_SECRET" \
  -H "content-type: application/json" \
  -d '{"email":"pessoa@exemplo.com","product":"aureahyper_lifetime","action":"grant"}'
```
