# Aurea 2116

## Notas de atualização

- Android e iOS: segurar e arrastar uma camada na timeline mantém os controles fechados. Um toque intencional na camada abre suas opções.
- Modelos 3D: corrigida a resolução dos arquivos antigos em `files/modelos` no Android. Novas importações ficam em `files/projetos/modelos` e são gravadas como caminhos relativos ao diretório de documentos.
- Notas atualizadas dentro do aplicativo nas duas plataformas.

## Causa e validação

O motor Android recebe `files/projetos` como diretório de documentos. Os modelos antigos eram copiados para `files/modelos`, mas a reabertura buscava um arquivo relativo dentro de `files/projetos`. A resolução agora reconhece esse diretório antigo, com verificação de contenção, e continua aceitando os arquivos acompanhantes de projetos portáveis.

- GLB sintético e FBX: 74 verificações passaram. Incluem três ciclos de salvar/reabrir com caminhos antigos, nova importação com caminho relativo e dois ciclos depois de mover Documents e tornar o diretório original indisponível.
- Regressões de caminhos HDRI: 42 verificações passaram. O teste de symlink não foi executado porque o host não permitiu sua criação.
- Android `compileReleaseKotlin`: passou.
- Contratos Swift, projeto Xcode e recursos compartilhados: passaram.
- Adicionado teste de UI no iOS que segura e move uma camada inicialmente sem seleção, exige opções fechadas e tamanho da timeline preservado, depois toca para exigir que os controles abram.

Os dois arquivos específicos do relato (floresta GLB e Skydome FBX) não foram fornecidos. O cenário foi reproduzido com arquivos GLB/FBX de teste; confirmação com esses projetos e aparelhos físicos continua pendente.
