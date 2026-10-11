# Texto e otimizações 3D — 10/10/2026

Pedido: restaurar o botão de opções ao tocar no texto e reduzir o custo do app, principalmente em 3D. Esta rodada modifica o motor C++ compartilhado e as interfaces Android/Kotlin e iOS/Swift. O workspace já continha outras alterações; este relatório descreve somente esta rodada.

## Implementação

- **Texto:** a barra contextual mostra `Editar texto` e `Opções de texto` primeiro, tanto em texto 2D quanto em texto 3D. O filtro da barra renovada havia removido a segunda opção; agora preserva ambas e a entrada de presets. Android e iOS mantêm os rótulos e a acessibilidade existentes.
- **Pose estática 3D:** modelos sem deformadores, clipes ou alterações de materiais compartilham a pose imutável entre camadas e quadros. Transformações das camadas continuam independentes. Alterar o asset gera outra entrada; animação e deformadores continuam avaliados no instante correto, inclusive no export.
- **Memória:** o cache de poses limita o payload a 8 MiB e 64 entradas, usa descarte LRU e referências fracas ao asset. Não mantém a geometria de um modelo excluído. Troca de projeto, reinicialização e pressão de memória liberam o cache. O workspace retido limita-se a hierarquias de até 32.768 nós; modelos maiores usam armazenamento temporário.
- **Animação:** buffers temporários reutilizados e composição TRS direta evitam alocações e multiplicações gerais de matrizes a cada avaliação.
- **Preparação de desenhos:** buscas de materiais e blocos usam oito entradas locais sem alocação, com hashing da chave completa para cenas maiores. Morphs usam busca binária. Instancing usa listas planas e verifica a chave completa antes de agrupar. Cenas com um único desenho pulam a construção dos grupos.
- **Uploads:** poses compartilhadas reutilizam juntas e dados de morph dentro do quadro, mantendo offsets, materiais, ambientes e transformações independentes.
- **GPU:** sem céu, piso ou malha iluminada, os shaders de malhas unlit, planos e partículas escrevem RGB preto no alvo HDR. Nesse caso o bloom é omitido; cores, tone map e profundidade de campo continuam no caminho normal. Cenas iluminadas e acumulação temporal conservam o bloom.
- **Interfaces:** indicadores de buffer e estado auxiliar são consultados no máximo a cada 200 ms, ou imediatamente quando a revisão do modelo muda. A geometria e o estado principal do preview continuam atualizados normalmente.

Não houve redução global de qualidade de imagem, resolução ou amostras de exportação.

## Medições antes/depois

Mesma máquina Windows, build Release e cenas idênticas. CPU medida com backend simulado; GPU com Vulkan do host. Aquecimento antes das amostras. Logs em `build/validation/optimization-20261010-*`.

| Cenário | Antes | Final |
| --- | ---: | ---: |
| Preparação CPU: 4.096 nós, oito instâncias, p50 | 3,0410 ms | 0,0050 ms |
| Mesmo cenário, p95 | 4,5114 ms | 0,0051 ms |
| Alocações por preparação | 64 | 8 |
| Pose animada: 4.096 nós, p50 | 0,2537 ms | 0,1101 ms |
| Alocações por avaliação de pose com buffers aquecidos | 5 | 0 |
| Cena 1080×1920 com imagem, Deep Glow, partículas e texto 3D: GPU | 1,70 ms | 1,66 ms |
| Mesma cena: tempo total mediano | 2,01 ms | 1,93 ms |

O maior ganho elimina trabalho repetido da hierarquia na CPU. A cena combinada continua dominada pelo processamento de pixels; sua melhora é pequena. Esses números não são FPS de Android/iOS nem uma promessa de aceleração equivalente em todos os celulares.

## Compilação

- Motor C++ host Release: compilado.
- Android x86_64 `uiTest` e instrumentação: compilados; **391 testes unitários, zero falhas**.
- Android Release **arm64-v8a e armeabi-v7a**: compilados, versão existente 0.0.9 / 2150. APKs em `output/optimization-20261010/`, com arquitetura conferida e assinaturas v2/v3 verificadas. Checksums em `SHA256SUMS.txt` na mesma pasta.
- iOS: verificações de API Swift/ponte, projeto Xcode e regra de layout sem problemas. **Não compilado:** este host Windows não possui Xcode/SDK iOS; auditoria estática não comprova compilação nativa.

## Validação em execução

- Emulador Android API 35, app isolado `.uitest`, com o motor final: **quatro testes aprovados** de toque em texto 2D/3D, abertura do painel, preview parado e estabilidade de ambiente. Não instala sobre o app principal do usuário. Log final: `build/validation/optimization-20261010-device-delivery.log`.
- Motor final: **72 testes de Scene3D, 51 de Text3D, 35 de MotionBlur e cinco de Optimization**, todos sem falhas. Os grupos podem compartilhar casos, portanto não devem ser somados como testes únicos.
- Cache: comparação de matrizes, busca em instantes fora de ordem, materiais editados, substituição de asset, limite de memória, exclusão e sobrevivência de snapshots após trim.
- GPU: unlit com bloom ligado/desligado produz pixels idênticos e quatro passes em ambos; iluminação, HDR emissivo, sombras, morph, skin, DOF, instancing e exportação passaram nos grupos acima.
- **Celulares físicos e iPhone/iPad:** não validados; nenhum aparelho físico conectado. O teste de emulador não comprova desempenho térmico ou fluidez nesses aparelhos.

## Resultado da suíte geral e pendências

A rodada geral anterior ao último ajuste de materiais/bloom executou **1.654 testes e 8.209.659 verificações, com 50 verificações falhando em seis casos**. Ela não está aprovada. Investigação:

- Proxy de vídeo: passou isoladamente, usando `engine/` como diretório de execução; a invocação geral fazia um caminho relativo sair da raiz do workspace.
- Fechamento de decoders de miniaturas: passou isoladamente. A execução geral ocorreu durante compilação pesada; sensibilidade ao agendamento permanece uma hipótese, não uma conclusão comprovada.
- `BoundedCommandsGpu`: 12 expectativas de uma única submissão falham porque o backend já submete e aguarda prefixos separadamente. O comportamento existente foi preservado; outros dois casos do grupo passaram.
- Efeito de catálogo `aurea.move.path`: não mudou a imagem no cenário geral; pendente revisar a configuração da guia do teste.
- Fuzz de parâmetros extremos: cinco quadros recusados em 1.056; pendente diagnóstico por efeito.
- Projeto extremo: 30 verificações de render falham também isoladamente; não está validado como cenário suportado por esta rodada.

Os testes dirigidos da implementação final passaram. As pendências gerais acima não devem ser apresentadas como resolvidas nem atribuídas com certeza a alterações anteriores sem um baseline separado.

## Referências externas

Princípios usados para especificar o trabalho: [Android — Optimization tips](https://developer.android.com/games/optimize/optimization-tips), [Apple — Persistent objects](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/PersistentObjects.html) e [Apple — Triple buffering](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/TripleBuffering.html). Nenhum código, shader ou asset de outro aplicativo foi incorporado.
