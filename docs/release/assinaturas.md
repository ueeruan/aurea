# Assinaturas do Aurea (1.0.9, 25/11/2026)

Decisão do dono para a 1.0.9:

> assinatura ANUAL R$150, MENSAL R$30, SEMANAL R$7 … não deve ter mais efeitos avulsos, e quem já comprou continua com o mesmo preço até renovar.

- **AureaHyper tem três planos à venda, e todos liberam TUDO o que é pago**: cada efeito pago, o Texto 3D, as 3 ferramentas Hyper (Upscale, Motion, Clean) e o Flow Retime › Neural (IA).
  - **Semanal: R$ 7/semana** (`aurea_hyper_semanal`, produto NOVO);
  - **Mensal: R$ 30/mês** (`aurea_hyper_mensal`, volta a ser vendido; antes estava só como legado);
  - **Anual: R$ 150/ano** (`aurea_hyper_anual`, já existe; é o plano destacado como "Melhor valor").
- **Não existem mais assinaturas avulsas** (efeitos pagos e Texto 3D). Elas já tinham saído do paywall na 1.0.2.1. Quem tem uma continua com o acesso dela, e o app continua reconhecendo e restaurando esses produtos.
- **Quem já assina mantém o preço até renovar.** Isso é feito na loja (coorte de preço legado na Play e "manter o preço atual" na Apple). No código, basta reconhecer todos os ids, o que já acontece.
- O produto antigo `aureahyper_lifetime` (concedido conta a conta pelo dono no servidor) **continua liberando tudo**.
- O AureaOS (`aurea_aureaos`, compra única do AureaMCP) **não faz parte** disto e não muda.

Os preços ficam nas lojas. O app mostra o preço localizado da loja. Enquanto a loja não responde, ele mostra os textos padrão "R$ 7,00", "R$ 30,00" e "R$ 150,00" (`assinatura_preco_semanal`, `assinatura_preco_mensal` e `assinatura_preco_hyper`).

## Como o paywall fica (Android e iOS iguais)

Aparece na folha "Recurso pago" (efeito pago, Texto 3D, opção Neural) e no painel bloqueado do AureaHyper. Tem uma linha "Escolha o seu plano do AureaHyper. Todos liberam tudo." e três cartões, nesta ordem:

| cartão | texto | tag de teste / accessibilityIdentifier |
|---|---|---|
| Semanal | `<preço>/semana` | `assinatura.semanal` |
| Mensal | `<preço>/mês` | `assinatura.mensal` |
| Anual (destacado, selo "Melhor valor") | `<preço>/ano` e, só com os preços da loja carregados, "Economize N%" | `assinatura.hyper` (o selo de economia: `assinatura.economia`) |

Depois vêm "Restaurar compras" (`assinatura.restaurar`), o aviso (`assinatura.aviso`) e o texto de renovação ("Renova automaticamente ao fim de cada período (semana, mês ou ano)…").

- **"Economize N%"**: compara o anual com um ano do plano curto **mais barato** (semanal × 52 ou mensal × 12), na mesma moeda, com arredondamento para baixo. A conta fica no motor (`hyper::hyper_annual_savings_percent`). Com R$ 7, R$ 30 e R$ 150, o selo mostra **58%** (1 − 150/360). Com R$ 149,90 também dá 58%. Sem os preços da loja, o selo não aparece.
- Cada cartão só fica ativo quando o servidor diz que o produto está à venda (`purchasable` do `GET /api/entitlements`). Com o beta fechado, nenhum fica ativo e aparece "As assinaturas ainda não estão disponíveis".
- **Play**: cada plano é um produto de assinatura próprio. O app escolhe o plano básico cujo id é o fim do id do produto (`semanal`, `mensal` ou `anual`). Se não houver plano básico com esse id, usa qualquer plano básico sem oferta promocional.
- **App Store**: os três ficam no mesmo grupo (22585277). Trocar de plano é upgrade ou downgrade dentro do grupo, não uma segunda assinatura.

## Produtos (os mesmos ids na Google Play e na App Store)

| id do produto | à venda? | preço | grupo (App Store) | o que libera |
|---|---|---|---|---|
| `aurea_hyper_semanal` | **sim (novo)** | R$ 7/semana | AureaHyper (22585277) | tudo o que é pago |
| `aurea_hyper_mensal` | **sim (volta)** | R$ 30/mês | AureaHyper (22585277) | tudo o que é pago |
| `aurea_hyper_anual` | **sim** | R$ 150/ano | AureaHyper (22585277) | tudo o que é pago |
| `aurea_fx_cartoon_moblur`, `aurea_fx_twitch`, `aurea_fx_signal`, `aurea_fx_glitchify`, `aurea_fx_extrude`, `aurea_fx_sharpen`, `aurea_fx_find_edges`, `aurea_fx_wave_warp`, `aurea_fx_block_dissolve`, `aurea_fx_radial_wipe`, `aurea_fx_headbop` | não (legado) | quem tem mantém | um grupo para cada | o próprio efeito (e a versão antiga dele) |
| `aurea_texto3d` | não (legado) | quem tem mantém | `Texto 3D` | criar Texto 3D |
| `aureahyper_lifetime` | não (concessão do dono no servidor) | — | — | tudo, para sempre |
| `aurea_aureaos` | compra única (AureaMCP) | — | — | fora desta mudança |

A tabela vem do motor (`engine/src/hyper/Hyper.cpp`):

- `hyper::hyper_plans_on_sale()` lista o que está à venda: semanal, mensal e anual;
- `hyper::subscription_product_ids()` lista tudo o que o app reconhece para restaurar: os 3 planos e os 12 avulsos legados, 15 ao todo;
- `hyper::product_unlocks_everything` e `hyper::has_access` dizem o que cada produto libera.

O Worker tem a mesma lista em `discovery/entitlements.js` (`HYPER_PLANOS` e `ASSINATURAS`), e os testes conferem a contagem dos dois lados. Um texto 3D ou um efeito pago que já está no projeto continua abrindo, editando e exportando sem a assinatura. A assinatura só é pedida para criar um novo.

## Checklist do lançamento (o líder, na hora de lançar)

### 1. Google Play Console (pacote `com.aurea.editor`)

1. **Anual** (`aurea_hyper_anual`) → plano básico → preço **R$ 150,00**.
   - Ao salvar, escolha manter os assinantes atuais no preço antigo (**coorte de preço legado**). É o padrão da Play: ninguém é migrado sem você pedir.
   - **Não** use "Migrar assinantes do preço legado". Pela decisão do dono, quem já assina continua com o preço até renovar.
2. **Mensal** (`aurea_hyper_mensal`) → plano básico → preço **R$ 30,00**. Confira se o plano básico está **ativo**, porque desde a 1.0.2.1 o mensal estava fora do paywall.
   - Quem assina o mensal hoje paga R$ 59,90. **Decisão pendente do dono**: migrar a coorte legada para R$ 30 (recomendado: é uma redução, a Play aplica na próxima renovação e avisa o assinante) ou deixar no preço antigo, ao pé da letra da regra "mesmo preço até renovar".
3. **Semanal (novo)**: Monetizar → Produtos → Assinaturas → **Criar assinatura**.
   - ID do produto: `aurea_hyper_semanal` (não pode ser trocado depois). Nome: "AureaHyper Semanal".
   - Plano básico: ID `semanal`, renovação automática, período **1 semana**, preço **R$ 7,00**. Ative o plano.
   - Sem ofertas promocionais. O app usa a oferta do plano básico.
4. Ids dos planos básicos esperados pelo app: `semanal`, `mensal` e `anual`. Se o anual ou o mensal já existirem com outro id de plano básico, nada quebra: o app usa o plano básico sem oferta que existir.
5. **Desative a venda** dos avulsos (os 11 `aurea_fx_*` e o `aurea_texto3d`).
   - Desative os planos básicos para novos compradores **sem encerrar** as assinaturas existentes. Não use "excluir" nem "encerrar assinantes".
   - Quem tem continua renovando no preço que paga.

A conta de serviço (`GOOGLE_PLAY_SERVICE_ACCOUNT`) já tem as permissões ("Ver dados financeiros…" e "Gerenciar pedidos e assinaturas"). O Worker não muda nada por causa do período semanal, porque o vencimento vem da loja (`expiryTime`).

### 2. App Store Connect (bundle `com.aurea.aurea.3ATTUN44P8`)

1. Grupo de assinatura **AureaHyper (22585277)** → **Criar assinatura**.
   - Nome de referência: "AureaHyper Semanal". ID do produto: `aurea_hyper_semanal`. Duração: **1 semana**.
   - Localização (pt-BR e en no mínimo): nome de exibição e descrição.
   - Captura da revisão: o paywall novo, com os três cartões.
2. **Níveis do grupo** (do mais alto ao mais baixo): 1 = `aurea_hyper_anual`, 2 = `aurea_hyper_mensal`, 3 = `aurea_hyper_semanal`.
   - Assim, ir do semanal ou do mensal para o anual é upgrade imediato, com reembolso proporcional da Apple.
   - Voltar do anual para um plano menor é downgrade e vale na renovação.
3. **Preços (Brasil)**: anual **R$ 150**, mensal **R$ 30**, semanal **R$ 7**.
   - A Apple só aceita os pontos de preço da tabela dela. No real, eles costumam terminar em ",90" (R$ 6,90 ou 7,90, R$ 29,90 e R$ 149,90). **Não confirmei daqui se existem R$ 7,00, R$ 30,00 e R$ 150,00 exatos.** Abra a lista de pontos de preço em BRL no App Store Connect.
   - Se o valor exato não existir, use o mais próximo sem passar do pedido: **R$ 149,90**, **R$ 29,90** e **R$ 6,90**. Anote aqui qual ficou.
   - O app mostra o `displayPrice` da loja, e o selo "Economize N%" é calculado com os preços reais (dá 58% nos dois casos).
4. **Anual sobe** (R$ 100 → R$ 150). Na tela de mudança de preço, escolha **"Manter o preço atual para os assinantes existentes"** (preserve current price).
5. **Mensal desce** (R$ 59,90 → R$ 30). Na Apple, a redução vale para os assinantes atuais na próxima renovação. Confira na tela, porque é a mesma decisão pendente do item 1.2.
6. **Tire de venda** os 11 efeitos avulsos e o Texto 3D. Em cada assinatura, use "Remover da venda" ou desmarque a disponibilidade em todos os países. **Não exclua** nada: quem assina continua renovando.
7. Envie a `aurea_hyper_semanal` junto com o build da 1.0.9 para a revisão. Uma assinatura nova só é aprovada junto com um build.

O app finaliza a transação (`finish`) depois do OK do servidor. As renovações e as trocas de plano feitas fora do app chegam por `Transaction.updates`.

### 3. Worker (depois do backup do D1)

1. `npx wrangler d1 export aurea-contas --remote --output=backup-antes-109.sql`. É a regra da casa. A 1.0.9 **não tem migração nova**.
2. Compare as vars (`wrangler.toml`) com as de produção e faça o deploy. O deploy é necessário: sem o Worker novo, `aurea_hyper_semanal` volta 400 (`produto_desconhecido`) e não aparece em `purchasable`.
3. `AUREAOS_PURCHASES_ENABLED` continua sendo a chave do beta fechado. Com `"false"`, nenhuma compra é aceita.

### 4. Teste nos aparelhos (testadores de licença na Play e Sandbox na Apple)

- [ ] Os três cartões mostram o preço da loja, e o anual mostra "Economize 58%".
- [ ] Assinar o semanal libera tudo. A renovação semanal aparece (no sandbox, uma semana = poucos minutos).
- [ ] Trocar de plano: semanal → anual (upgrade) e anual → mensal (downgrade na renovação).
- [ ] "Restaurar compras" com uma conta que tem um avulso legado: o efeito continua liberado.
- [ ] Conta com o anual a R$ 100 (ou o mensal a R$ 59,90): o preço depois da mudança é o escolhido nos itens 1.1, 1.2 e 2.4.

## Segredos e variáveis do Worker (nada muda na 1.0.9)

| segredo / var | para quê |
|---|---|
| `GOOGLE_PLAY_SERVICE_ACCOUNT` | JSON da conta de serviço com acesso à Play |
| `APPLE_IAP_KEY_ID`, `APPLE_IAP_ISSUER_ID`, `APPLE_IAP_PRIVATE_KEY` | chave da App Store Server API |
| `APPLE_BUNDLE_ID` | tem que ser `com.aurea.aurea.3ATTUN44P8` |
| `AUREAOS_PURCHASES_ENABLED` | a chave do beta fechado (`"true"` abre a venda do AureaOS e das assinaturas) |

## Como funciona (resumo técnico)

- **Compra**: o app abre a compra na loja e manda o comprovante ao Worker (`POST /api/entitlements/purchase {platform, product, token}`). O Android manda o purchaseToken; o iOS manda o id da transação. O Worker confere na loja e grava o produto com o vencimento. A mesma assinatura não vale em duas contas Aurea (409).
- **Consulta**: `GET /api/entitlements` devolve três coisas:
  - o que vale agora (`products`);
  - o vencimento de cada assinatura (`expires`);
  - o que a loja aceita (`purchasable`, que também inclui os avulsos legados, para restaurar).

  O app só mostra os 3 planos.
- **Renovação**: quando o vencimento gravado passa, a próxima consulta pergunta de novo à loja (no máximo 1 vez por hora por assinatura, até 35 dias depois do vencimento). Isso vale igual para semana, mês e ano.
- **Estados que liberam**: Play `ACTIVE`, `IN_GRACE_PERIOD` e `CANCELED` até `expiryTime`. Apple status 1 (ativa) e 4 (carência).
- **Offline**: o app guarda a última confirmação por 30 dias, mas uma assinatura conta só até 1 dia depois do seu vencimento, mesmo offline.
- **Restaurar compras**: há um botão no paywall. O app também restaura sozinho ao abrir e ao entrar na conta. Reconhece os 15 ids.
- Uma concessão do dono (admin, sem vencimento) nunca é rebaixada para assinatura.

## Histórico

- 16/11/2026: assinaturas mensais. AureaHyper a R$ 59,90/mês e cada efeito pago ou o Texto 3D a R$ 5,00/mês. Migração 0012 (vencimento no D1).
- 19/11/2026 (1.0.2.1): só o AureaHyper ANUAL (R$ 100/ano) à venda. O mensal e os avulsos passaram a ser só reconhecidos.
- 25/11/2026 (1.0.9): semanal R$ 7, mensal R$ 30 e anual R$ 150, todos liberando tudo. Os avulsos continuam fora de venda.
