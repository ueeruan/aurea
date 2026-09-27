# Timeline: gestos e duração — 26/09/2026

Alteração local em Android e iOS; ainda não distribuída em APK/IPA.

## Comportamento

- Arrastar horizontalmente o corpo de uma camada não selecionada seleciona e move, sem abrir o dock. Tocar e soltar continua abrindo as opções.
- Arrastar verticalmente o corpo rola a lista, inclusive após uma pausa com o dedo. Segurar e arrastar o cabeçalho continua reordenando camadas.
- Segurar uma camada para selecionar não abre suas configurações. Arrastar keyframes de outra camada também não abre o dock.
- As alças de início/fim continuam disponíveis com o painel de propriedades aberto.
- A vista e a rolagem automática podem ultrapassar o fim atual. Mover/aparar camadas faz o motor aumentar a composição. Não há mais bloqueio de navegação pela duração inicial.
- Vídeos/áudios continuam respeitando o conteúdo disponível na mídia. A mudança não inventa frames nem prolonga automaticamente uma fonte esgotada.
- A posição visual acompanha o dedo com fração de frame; o motor e as edições continuam alinhados a frames inteiros.
- No Android, a geometria das linhas fica em cache e a busca da linha sob o dedo usa busca binária. O enquadramento automático inicial não volta a mudar o zoom quando uma edição ultrapassa vinte segundos.
- No iOS, movimentos são agrupados por quadro da interface, com aplicação da posição final ao soltar. Miniaturas e formas de onda atualizam no máximo uma vez por tick, e a inércia usa o tempo efetivamente decorrido.
- Detectar batidas removido do menu de áudio, menu da timeline e busca de comandos em ambas as plataformas. Marcadores manuais e marcadores já salvos permanecem.

## Verificação

- Android 12/API31, `Aurea_API31_Prompt03`, pacote descartável `com.aurea.aurea.uitest`: **4 testes instrumentados, zero falhas**.
  - Lista com 24 camadas: rolar para baixo e voltar, sem reordenar nem abrir opções.
  - Arrastar camada não selecionada, manter altura da timeline, desfazer e confirmar que um toque real ainda abre o dock.
  - Estender a camada além do fim, verificar crescimento da composição e desfazer ambos.
  - Repetir extensão/desfazer com Transformar aberto.
- **124 testes JVM**, zero falhas.
- Motor compartilhado, filtro `ClipTime`: **19 testes, 1.206 verificações**, zero falhas.
- iOS: checagem estática de APIs/chaves sem problemas; sete recursos compartilhados idênticos. Não houve execução nativa iOS nesta máquina Windows.
- Logs: `build/prompt04/timeline-android-final.log`, `build/prompt04/timeline-core.log`; resultados instrumentados em `build/android/app/outputs/androidTest-results/connected/uiTest/`.

Os testes verificam os gestos e o estado do projeto. Ainda é necessário medir a fluidez com mídia e efeitos em aparelhos físicos; estes resultados não são uma medição de FPS do Samsung A51 5G ou do iPhone.
