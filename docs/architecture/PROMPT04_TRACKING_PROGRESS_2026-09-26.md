# Prompt 4 — trabalho em andamento

O pedido de correção de timeline recebeu prioridade. O tracking ainda não deve ser anunciado como pronto nem equivalente ao After Effects.

## Implementado nesta revisão

Melhorias de pirâmide/Lucas–Kanade, distribuição espacial de features, normalização Hartley do ajuste essencial, hipóteses robustas de pose, ajuste conjunto periódico e continuação com focal refinada. Falha de inicialização translacional não é mais tratada como prova de câmera em tripé. Um solve parcial não é aceito como completo.

Cache portátil na camada, formato de timeline 30, com observações esparsas no disco, limites de memória/leitura, invalidação por sessão e origem de camadas geradas. Cancelamento sinaliza a worker sem aguardar na interface. Seleção por IDs de features, overlay de reprojeção, multisseleção, filtro/tamanho de pontos e criação de Camera/Null/Shape/Text/Solid foram ligados às duas interfaces. O projeto de origem e o tempo são verificados antes de aplicar o solve.

A câmera acompanha o enquadramento de vídeos com translação, rotação e escala não uniforme. Reutilizar uma análise não duplica a câmera nem substitui seus keyframes manuais. Há seleção de modelo Auto/Free/Tripod, FOV informado, exclusão de features com nova resolução, alvo de plano arrastável, definição de origem/chão/escala e colocação de um modelo 3D já importado. A calibração usa a base da hierarquia para preservar a projeção. O ambiente e as interfaces ainda precisam de aceitação nativa conjunta.

Tracking de um/dois pontos usa NCC com busca prevista e tentativa de recuperação. Planar, Corner Pin e estabilizador usam consensos robustos de translação/similaridade/homografia. O estabilizador oferece suavização/lock e crop nenhum/estático/dinâmico com zoom limitado; a correção usa Corner Pin e preserva os keyframes de Transform. Aplicar novamente atualiza o efeito criado, sem empilhar cópias. Os resultados e o mapeamento temporal ficam no projeto. As duas interfaces têm análise em worker, cancelamento, restauração de cache, aplicação e desfazer.

A leitura estrita confirma v30 e restaura a análise e a calibração. O teste não aceita recuperação silenciosa de um backup como prova do round trip. A identidade dos metadados/caminho invalida a análise após troca da origem; não é um hash completo do conteúdo de vídeo. Alterações de tempo são verificadas também no caminho rápido de restauração, calibração, refinamento e colocação de modelos. Desfazer/refazer restaura a referência do cache nas interfaces. Proxies de análise limitam as duas dimensões. Aplicar tracking a um destino preserva sua animação existente; tracking de um ponto não cria trilhas vazias de escala/rotação. Reaplicar estabilização reutiliza seu efeito mesmo quando a pilha está no limite.

Validação final desta revisão: `build/effects-packages/tracking-final.log`, 10 entradas/173 verificações; `motion-final.log`, 11 entradas/1.965 verificações. Zero falhas; entradas de benchmark sem opt-in são puladas. A validação inclui substituição de origem, alteração de tempo e animação já existente no destino.

## Evidência existente

- `build/prompt04/tracking-scene-tests.log`: filtro Tracking, 10 entradas, 166 verificações, zero falhas; inclui benchmarks pulados sem opt-in. O teste sintético de vídeo resolve 90/90 frames, RMS 0,305 px, mediana de pinning 0,58 px e p90 1,78 px. Salvar/reabrir/reutilizar câmera, preservar animação manual, alvo e invariância da projeção após origem/chão/escala passam.
- `build/prompt04/motion-scene-tests.log`: MotionGeometry, 10 entradas, 1953 verificações, zero falhas. Cobre consensos, perda de pontos, 300 frames de suavização, crop limitado, cache, aplicação, reaplicação e desfazer. O benchmark de mídia real é opt-in.
- `build/prompt04/corner-pin-tests.log`: render GPU real, 12 verificações, zero falhas; conteúdo e keyframes do Corner Pin sem mover a camada.
- TUM desk final: 300/300 frames, RMS 0,8266 px, FOV 44,9121°, confiança 0,4049; extração 12,121 s e solve 11,340 s no computador (`desk-final-lk.log`). TUM xyz final: 600/600, RMS 0,7840 px, FOV 50,1818°, confiança 0,4986; extração 25,228 s e solve 36,419 s (`xyz-final-lk.log`). Não medem drift independente contra ground truth nem desempenho de celular.
- Motion sobre xyz: 600/600 frames, erro médio 0,7586 px, jitter de aceleração 2,9263 para 0,8032 px; processamento geométrico 1,808 s no computador, sem extração (`motion-real-xyz.log`).
- `build/prompt04/tracking-stabilizer-real-20s.mp4` é uma visualização offline de poses/crop, não captura do app. Resíduo do ponto de apoio: mediana 0,795 px, p90 1,419 px em 298 observações. O ponto deixa de ser observado na segunda metade; isso não comprova pinning durante os 600 frames.
- Android: testes anteriores de timeline (4) e edição de texto/teclado (2) passaram. A nova bateria integrada de tracking parou na compilação do teste por acesso a API privada; o teste foi ajustado para importar pela API pública, aguardando execução. Não foi executada no pacote que contém os projetos do usuário.
- iOS: checks estáticos API/bridge, recursos compartilhados e contrato de parâmetros aprovados. Não substituem compilação Xcode ou execução nativa.

## Pendências relevantes

Calibração de lente/distorção e intrínsecos completos; modelo Mostly Flat; validação de drift com timestamps corretos; aceitação visual nativa de null/shape/cubo/GLB em 300 frames/20 s; medições nos aparelhos físicos A51 5G e iPhone. Falta parar e retomar com resultado parcial, correção manual de trajetória, offset de attach point e aplicação independente de propriedades. O planar para quando perde a trajetória; qualquer frame perdido atualmente impede aplicação. Não há warp avançado/rolling shutter. Trocar bytes de um arquivo mantendo caminho e metadados exige uma identidade de conteúdo completa, ainda não calculada. O alvo e a colocação de GLB ainda precisam de teste da interação nativa.

Arquivos reais e CSVs de análise ficam em `build/prompt04/`. O prompt original está no anexo `4f5ebb22-07de-4d7a-be18-1d0f48a7593b/Texto colado.txt`. Nenhum IPA/APK de distribuição foi gerado ou publicado por esta revisão.
