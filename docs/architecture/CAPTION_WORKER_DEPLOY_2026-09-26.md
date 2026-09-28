# Publicação do catálogo de presets

Worker: `aurea-ai-discovery`.
Versão publicada: `0c21697b-d939-4c3b-b88f-19939a9e3784`.
Endpoint: https://aurea-ai-discovery.aureaapp.workers.dev/api/captions/presets

Adicionados Durable Object SQLite `CaptionCommunity` e migração `v2-caption-community`.
Os presets são declarativos: não carregam scripts, expressões ou arquivos remotos.
As rotas de áudio/transcrição não fazem parte deste serviço.

Verificações antes da publicação:

- Cinco testes unitários do validador passaram.
- Integração no Wrangler local passou: sessão, autenticação, publicação, versão,
  pesquisa, download, curtidas sem duplicação, proteção de autoria, entrada inválida,
  preview e rota `/server` existente.
- Preset real capturado pelo motor C++ foi aceito pelo mesmo validador.
- `wrangler deploy --dry-run` passou.
- Integração repetida após corrigir o stream de uploads recusados e os contadores
  retornados ao atualizar um preset; sem erros de stream no log local.

Verificação de produção: `/api/captions/presets` HTTP 200, catálogo vazio;
`/server` HTTP 200 e `online: true`. Nenhum preset de teste foi publicado em produção.

Limites: autenticação por token de instalação; registro limitado por IP e globalmente;
envio limitado por usuário/IP/global, tamanho de 128 KiB e até 20 versões por preset.
O thumbnail atual é uma cartela SVG com o título; não representa o estilo renderizado.
A biblioteca iOS e a aceitação de ponta a ponta no app ainda precisam ser concluídas.
