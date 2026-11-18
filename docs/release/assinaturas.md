# Assinaturas mensais do Aurea (16/11/2026)

Decisão do dono em 16/11/2026: o conteúdo pago vira **assinatura mensal**.

- **AureaHyper — R$ 59,90/mês — libera TUDO o que é pago**: cada efeito pago, o Texto 3D e as 3 ferramentas Hyper (Upscale, Motion, Clean).
- **Cada efeito pago e o Texto 3D — R$ 5,00/mês cada**, uma assinatura por item.
- Fast Blur, Turbulent Displace e Drop Shadow agora são **grátis**.
- O produto antigo `aureahyper_lifetime` (concedido conta a conta pelo dono no servidor) **continua liberando tudo**.
- O AureaOS (`aurea_aureaos`, compra única do AureaMCP) **não faz parte** disto e não muda.

Os preços ficam nas lojas. O app mostra o preço localizado que a loja devolve e, enquanto ela não responde, mostra "R$ 5,00" ou "R$ 59,90".

## Produtos (os mesmos ids na Google Play e na App Store)

| id do produto | nome para a loja | preço | grupo de assinatura (App Store) | o que libera |
|---|---|---|---|---|
| `aurea_hyper_mensal` | AureaHyper | R$ 59,90/mês | `AureaHyper` | tudo o que é pago (todas as linhas abaixo + Upscale, Motion e Clean do Hyper) |
| `aurea_fx_cartoon_moblur` | Cartoon Motion Blur | R$ 5,00/mês | `Efeito Cartoon Motion Blur` | aurea.motion.cartoon_moblur |
| `aurea_fx_twitch` | Twitch | R$ 5,00/mês | `Efeito Twitch` | Twitch (referência AE e a versão antiga) |
| `aurea_fx_signal` | Signal | R$ 5,00/mês | `Efeito Signal` | Signal (referência AE e a versão antiga) |
| `aurea_fx_glitchify` | Glitchify | R$ 5,00/mês | `Efeito Glitchify` | Glitchify (referência AE e a versão antiga) |
| `aurea_fx_extrude` | Extrude | R$ 5,00/mês | `Efeito Extrude` | Extrude (referência AE e o Raster Extrude antigo) |
| `aurea_fx_sharpen` | Sharpen | R$ 5,00/mês | `Efeito Sharpen` | Sharpen (referência AE) |
| `aurea_fx_find_edges` | Find Edges | R$ 5,00/mês | `Efeito Find Edges` | Find Edges (referência AE) |
| `aurea_fx_wave_warp` | Wave Warp | R$ 5,00/mês | `Efeito Wave Warp` | Wave Warp (referência AE) |
| `aurea_fx_block_dissolve` | Block Dissolve | R$ 5,00/mês | `Efeito Block Dissolve` | Block Dissolve (referência AE) |
| `aurea_fx_radial_wipe` | Radial Wipe | R$ 5,00/mês | `Efeito Radial Wipe` | Radial Wipe (referência AE) |
| `aurea_texto3d` | Texto 3D | R$ 5,00/mês | `Texto 3D` | criar Texto 3D |
| `aurea_fx_headbop` | Headbop | R$ 5,00/mês | `Efeito Headbop` | aurea.distort.headbop (pago desde a 1.0.2) |
| `aurea_hyper_mensal` | (sem produto próprio) | — | — | Flow Retime › Qualidade dos vetores › **Neural (IA)**: só com o AureaHyper (desde a 1.0.2; hyper::effect_option_requires_access). O efeito Flow Retime continua grátis nas outras qualidades |

Produtos que já existem e continuam iguais:

| id | tipo | observação |
|---|---|---|
| `aureahyper_lifetime` | concessão do dono no servidor (não é vendido) | libera tudo, para sempre |
| `aurea_aureaos` | compra única (AureaMCP) | fora desta mudança |

Quem decide o que cada produto libera é o motor (`engine/src/hyper/Hyper.cpp`, `hyper::effect_product_id` e `hyper::has_access`). Android e iOS só mandam ao motor os produtos da conta. Um texto 3D ou um efeito pago que já está no projeto continua abrindo, editando e exportando sem a assinatura. Só para criar um novo é preciso ter a assinatura.

## 1. Google Play Console (pacote `com.aurea.editor`)

Para cada linha da tabela de produtos:

1. Play Console → app Aurea → **Monetizar → Produtos → Assinaturas → Criar assinatura**.
2. **ID do produto**: exatamente o id da tabela (ex.: `aurea_fx_twitch`). Ele não pode ser trocado depois.
3. **Nome**: o nome da tabela (ex.: "Twitch").
4. Em **Planos básicos e ofertas → Adicionar plano básico**:
   - ID do plano básico: `mensal`;
   - tipo: **Renovação automática**;
   - período de faturamento: **1 mês**;
   - preço: R$ 5,00 (AureaHyper: R$ 59,90). Deixe a Play converter para os outros países ou ajuste país por país;
   - período de carência (graça): o padrão da Play serve. O Worker aceita o estado "em período de carência";
   - **Ative** o plano básico.
5. Não crie ofertas promocionais por enquanto. O app usa a oferta do plano básico; se houver outra oferta, o app ainda usa a do plano básico.
6. Salve e ative a assinatura.

A conta de serviço da Play (`GOOGLE_PLAY_SERVICE_ACCOUNT`, a mesma do Mural e do AureaOS) precisa da permissão **"Ver dados financeiros, pedidos e respostas de pesquisa de cancelamento"** e de **"Gerenciar pedidos e assinaturas"** em Play Console → Usuários e permissões. O Worker chama `purchases.subscriptionsv2.get`.

O app **reconhece** (acknowledge) a compra depois do OK do servidor. Se o app fechar antes, ele restaura e reconhece a compra na próxima abertura. A Play reembolsa uma compra que fica 3 dias sem ser reconhecida.

## 2. App Store Connect (bundle `com.aurea.aurea.3ATTUN44P8`)

**IMPORTANTE:** cada assinatura de R$ 5,00 precisa ficar no **seu PRÓPRIO grupo de assinatura**. Assinaturas do mesmo grupo se substituem: se dois efeitos ficarem no mesmo grupo, assinar o segundo cancela o primeiro. O AureaHyper também fica num grupo só dele. São 12 grupos, cada um com 1 assinatura.

Para cada linha da tabela de produtos:

1. App Store Connect → app Aurea → **Monetização → Assinaturas → Grupos de assinatura → "+"**.
2. Nome de referência do grupo: o da coluna "grupo de assinatura" (ex.: `Efeito Twitch`). Adicione uma localização do grupo (nome que o cliente vê, ex.: "Twitch").
3. Dentro desse grupo, **Criar assinatura**:
   - nome de referência: o nome da tabela;
   - **ID do produto**: exatamente o id da tabela (ex.: `aurea_fx_twitch`). Ele não pode ser reutilizado depois;
   - duração: **1 mês**;
   - preço: R$ 5,00 (AureaHyper: R$ 59,90) no Brasil, com os outros países pela tabela da Apple;
   - localização (pt-BR e en no mínimo): nome de exibição e descrição;
   - captura de tela da revisão: o paywall do app (folha "Recurso pago");
   - "Período de carência de cobrança" (Billing Grace Period): pode ligar. O Worker aceita o status 4 (carência).
4. Envie as 12 assinaturas junto com a próxima versão do app para revisão. Na primeira vez, a Apple só aprova assinaturas que vão junto com um build.

Em **Usuários e acesso → Integrações → Compra dentro do app** (In-App Purchase), confira que a chave do Worker (`APPLE_IAP_KEY_ID`) está ativa. É essa chave que dá acesso à **App Store Server API**. O Worker chama `/inApps/v1/transactions/{id}` e `/inApps/v1/subscriptions/{originalTransactionId}`, primeiro em produção e depois no sandbox.

O app finaliza a transação (`finish`) depois do OK do servidor. As renovações e as compras feitas fora do app chegam por `Transaction.updates`.

## 3. Segredos e variáveis do Worker (nada foi alterado)

São os mesmos segredos do Mural e do AureaOS. Nenhum segredo novo:

| segredo / var | para quê |
|---|---|
| `GOOGLE_PLAY_SERVICE_ACCOUNT` | JSON da conta de serviço com acesso à Play (ver permissões acima) |
| `APPLE_IAP_KEY_ID` | id da chave .p8 da App Store Server API |
| `APPLE_IAP_ISSUER_ID` | Issuer ID da App Store Connect |
| `APPLE_IAP_PRIVATE_KEY` | conteúdo da chave .p8 |
| `APPLE_BUNDLE_ID` | **tem que ser `com.aurea.aurea.3ATTUN44P8`**, o bundle id do app na App Store Connect (decisão do dono em 16/11). Se for diferente, toda compra do iOS volta como inválida. Confira o valor antes de abrir as vendas |
| `AUREAOS_PURCHASES_ENABLED` (var em `wrangler.toml`) | a chave do **beta fechado**. Com `"false"`, que é o valor de hoje, **nenhuma** compra é aceita: nem AureaOS nem assinaturas. O paywall mostra "As assinaturas ainda não estão disponíveis". Mude para `"true"` quando as lojas estiverem prontas |

## 4. Banco (D1) — migração 0012

`discovery/migrations/0012_entitlement_subscriptions.sql` é **aditiva**. Ela cria três colunas anuláveis em `entitlements` (`expires_at`, `store_ref`, `store_checked_at`) e um índice. As linhas antigas ficam com `expires_at` NULL, ou seja, não vencem.

Ordem (só o dono executa):

1. `npx wrangler d1 export aurea-contas --remote --output=backup-antes-0012.sql` (regra da casa: backup antes);
2. `npx wrangler d1 migrations apply aurea-contas --remote`;
3. depois o deploy do Worker.

Se o Worker novo subir antes da migração, a leitura dos direitos continua funcionando (nada vence) e a compra de assinatura responde 503 (falha fechada).

## 5. Como funciona (resumo técnico)

- **Compra**: o app abre a compra na loja e manda o comprovante ao Worker (`POST /api/entitlements/purchase {platform, product, token}`). O Android manda o purchaseToken; o iOS manda o id da transação. O Worker confere na loja e grava o produto com o vencimento. A mesma assinatura não vale em duas contas Aurea (409).
- **Consulta**: `GET /api/entitlements` devolve só o que vale agora (`products`), o vencimento de cada assinatura (`expires`) e o que está à venda (`purchasable`).
- **Renovação**: quando o vencimento gravado passa, a próxima consulta pergunta de novo à loja (no máximo 1 vez por hora por assinatura, até 35 dias depois do vencimento). Se renovou, o vencimento é atualizado. Se a loja estiver fora do ar, nada é liberado e nada é apagado.
- **Estados que liberam**: Play `ACTIVE`, `IN_GRACE_PERIOD` e `CANCELED` até `expiryTime`. `CANCELED` significa que a renovação foi desligada, mas o mês já pago continua valendo. Apple status 1 (ativa) e 4 (carência).
- **Offline**: o app guarda a última confirmação por 30 dias, mas uma assinatura conta só até 1 dia depois do seu vencimento, mesmo offline.
- **Restaurar compras**: há um botão no paywall. O app também restaura sozinho ao abrir e ao entrar na conta.
- Uma concessão do dono (admin, sem vencimento) nunca é rebaixada para assinatura.

## 6. O que falta (dono)

- [ ] Criar as 12 assinaturas na Play Console (passo 1).
- [ ] Criar os 12 grupos com 1 assinatura cada na App Store Connect e enviá-los com o próximo build (passo 2).
- [ ] Conferir `APPLE_BUNDLE_ID = com.aurea.aurea.3ATTUN44P8` e as permissões da conta de serviço da Play.
- [ ] Fazer o backup do D1, aplicar a migração 0012 e só então fazer o deploy do Worker (passo 4).
- [ ] Quando quiser vender: `AUREAOS_PURCHASES_ENABLED = "true"`. Isso também abre a venda do AureaOS.
- [ ] Testar nos aparelhos com testadores de licença (Play) e Sandbox (Apple): assinar um efeito, assinar o AureaHyper, cancelar, restaurar e renovar.
