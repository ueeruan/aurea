# Novidades de abertura e importação de presets — 25/09/2026

## Alterações desta rodada

- Android e iOS: painel rolável de novidades na Home. A edição das notas e o
  build instalado identificam o aviso; fechar persiste a leitura. O botão da
  Home permite consultar novamente. Catálogo Android gera os textos Swift.
- AM: a interface recusa conversões que reportam efeitos ignorados ou avisos
  de perda, antes de salvar ou aplicar. Isso não comprova equivalência visual
  dos mapeamentos inferidos: presets reais ainda precisam ser comparados.
- Corrigido laço infinito ao importar títulos longos duplicados: reservar o
  espaço do sufixo antes de truncar, nas duas plataformas.
- Leitura dos arquivos fora da interface, com limite de 64 MiB antes de
  entregar ao conversor. Leitura interrompida, inválida ou seleção alterada
  resulta em aviso. iOS cancela a aplicação pendente ao sair do painel.
  A conversão e a aplicação ainda executam na thread da interface; esta
  alteração não promete tempo constante para XMLs complexos.

## Evidência nativa encontrada

[Run iOS 36203157530](https://github.com/ueeruan/aurea/actions/runs/36203157530),
commit `7bdb5477`, anterior às alterações desta rodada:

- **8/8 testes de gestos passaram**, zero falhas, 236,18 segundos.
- Inclui `testDockMoveVideoAtTwoSecondsKeepsAppResponsiveAndPreservesDuration`
  e `testAddingEffectClosesBrowserAndShowsAppliedCard` (inclui desfazer).
- A captura de `home` excedeu 90 segundos. As outras 24 cenas reportaram
  captura; o run geral foi reprovado. Não confundir aprovação de gestos com
  aprovação de toda a matriz.
- Logs consultados em `build/ios-2111-full.log` e `build/ios-2111-failed.log`.
- Não valida o novo painel de novidades nem a nova leitura assíncrona no iOS.

## Validação local

- Compilação Kotlin e **109 testes JVM, zero falhas/erros**. Inclui regressões
  de 110 nomes duplicados longos (mudança de largura do sufixo), preservação
  de bytes no limite e rejeição de arquivo acima do limite.
- APK debug x86_64 gerado.
- APK release ARM64 gerado e conferido (ABI e texto final das notas no pacote):
  `build/releases/2111/aurea-2111-updates-android-64bit.apk`.
  Build final em `build/release-notes-arm64-final.log`. Não publicado.
- Verificadores iOS de API Swift, tipos, projeto Xcode, escopo, recursos
  compartilhados e contrato de efeitos sem problemas. Não são um build Swift.
- Importador AM no host: 7 testes, 112 verificações, zero falhas.
- Suíte completa do host: **735 testes, 4.558.847 verificações, zero falhas**
  (`build/release-notes-host.log`). Motor C++ não alterado nesta rodada.

## Limitações preservadas

Não foi implementado upscale GPU/temporal/fotográfico nesta rodada. Não há
aceitação ampla de tracking, estabilização, optical flow, motion blur,
partículas ou 3D/PBR nas duas plataformas, nem medição de fluidez sustentada
em edits reais de futebol, filmes e AMV. Samsung, vídeo enorme ao trocar
qualidade e falha Unity/WebView pós-export continuam sem correção comprovada.
As mudanças de UX e render anteriores permanecem no código; não representam
garantia de preview em tempo real para qualquer mídia, efeito e aparelho.
