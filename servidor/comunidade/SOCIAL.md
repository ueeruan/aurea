# Comunidade social

O Worker usa D1 (`SOCIAL`) para perfis, hashes de credenciais, seguidores,
curtidas, mensagens, bloqueios, denúncias e índice paginado de posts.
O KV anterior continua servindo mídias e compatibilidade com versões instaladas.

## Contas e segurança

- Cadastro obrigatório no cliente; código aleatório de 48 caracteres por conta.
- Código apenas no cabeçalho Authorization; nunca em perfis, feed ou mensagens.
- Contas anteriores são importadas com o mesmo ID e código. Apelidos conflitantes
  ou reservados recebem um identificador neutro e podem ser editados.
- Criador e oficial são papéis atribuídos no servidor; PATCH de perfil não
  permite alterar verificação ou privilégios. Nomes semelhantes são normalizados.
- O bootstrap do criador exige o hash guardado no segredo `OWNER_CODE_HASH`.
- `@ruanzitwo` é o criador; `@aurea` é a conta oficial, com credencial separada.
- Exclusão revoga acesso, oculta perfil/posts e remove seguidores e conversas.
  Dados históricos de posts/mídias no KV não são purgados por esse atalho.
- Mensagens são privadas por autorização dos participantes, **sem criptografia
  ponta a ponta**. A tela consulta novas mensagens a cada 4 s enquanto visível.

## Operação local

`social-admin.mjs` recebe JSON por stdin. Nunca passe o segredo nos argumentos
da linha de comando nem o inclua em arquivos versionados. Pode usar `codeFile`
apontando um arquivo privado, no lugar de `code`.

Ações: `check`, `post` (`text` e `image` URL opcional), `avatar` (`file` PNG),
`notice` (`text`), `verify` (`userId`, `verified`), `migrate`.
`bootstrap` grava a credencial oficial uma única vez no caminho `output` e não
a imprime. O acesso oficial local está em `tmp/aurea-official-access.txt`,
ignorado pelo Git. Faça backup privado; não envie esse arquivo aos testadores.

O criador também pode verificar/desverificar pessoas pelo menu do perfil no app.
Não foram publicados posts de exemplo nas contas reais.

## Deploy e testes

1. `node social.test.mjs` e `node teste.mjs`.
2. `npx wrangler d1 execute aurea-social --remote --file social.sql`.
3. `npx wrangler deploy`.
4. Bootstrap autorizado, migração idempotente e verificação de `/estatisticas`.

Flutter: `flutter test --no-pub test/social_ui_test.dart
test/community_account_ui_test.dart test/community_repost_reply_test.dart
test/cadastro_obrigatorio_test.dart`.

## Pendência externa

R2 não está habilitado na conta Cloudflare (erro 10042 em 19/09/2026).
Fotos até 2 MB e arquivos de projeto usam KV; publicação de vídeo retorna um
erro claro até habilitar R2 e configurar o binding `ARQUIVOS`. Nenhuma cobrança
ou assinatura foi ativada. Esta entrega não gera APK nem IPA.
