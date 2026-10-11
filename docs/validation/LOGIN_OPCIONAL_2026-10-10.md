# Login opcional — 10/10/2026

## Implementação

- Android e iOS abrem os projetos e o editor independentemente da sessão.
- O menu oferece Entrar quando não há conta; o formulário pode ser fechado.
- Sair, excluir a conta ou expirar a sessão não substitui o app pela tela de login.
- Sair e excluir só aparecem para quem está conectado. A autenticação dos serviços de conta continua real; não há sessão fictícia para liberar o app.
- Textos de entrada e saída atualizados em português, inglês, espanhol, russo, hindi, árabe e indonésio nas duas plataformas.
- Removida a conta fictícia usada nas capturas Debug do iOS, pois a raiz já aceita uso sem sessão.
- Acesso ao app é navegação nativa; não altera regras de edição no C++.

## Compilação e verificação

- Android: `compileUiTestKotlin`, `compileUiTestAndroidTestKotlin` e `testUiTestUnitTest` concluídos com sucesso. Log em `build/validation/optional-login-20261010-android.log`.
- Novo teste instrumentado compila e cobre abertura da raiz sem token, fechamento do login opcional e acesso à criação de projeto.
- iOS: auditorias de API Swift/ObjC, tipos, catálogo, projeto Xcode, layout e ciclo de vida passaram. Compilação Xcode não executada neste host Windows.

## Execução nativa e distribuição

- O novo teste instrumentado não foi executado em emulador/aparelho nesta rodada; a compilação não comprova essa execução.
- iOS não executado em simulador ou aparelho nesta rodada.
- Nenhum APK ou IPA gerado; nenhuma publicação ou mudança de versão neste trabalho, conforme a instrução mais recente do usuário.
