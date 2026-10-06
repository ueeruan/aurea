# Correções de edição e validação no moto g52

Registro da primeira rodada, com artefato intermediário. As compilações iOS,
correções adicionais e validações posteriores estão em
[MOTION_TILE_STRESS_2026-10-05.md](MOTION_TILE_STRESS_2026-10-05.md).

Solicitação: corrigir buffering, depth map, rotobrush, câmera após corte, fundo HDR após corte, iluminação em pré-composições e bordas do Motion Tile; compactar a edição nas interfaces Android e iOS.

O checkout já continha alterações extensas. Elas foram preservadas. Evidências desta sessão: `build/reference/device-fixes-20261005/`.

## Implementação

- O motor mantém o estado de quadro incompleto quando um recurso assíncrono ainda está sendo preparado. O aviso de conclusão agora redesenha a prévia pausada mesmo depois da janela de tentativas imediatas. A primeira execução no Android reproduziu a prévia de profundidade parada por 90 segundos; esse caso motivou a correção, sem desativar Vulkan arbitrariamente.
- O play prepara aproximadamente 180 ms de quadros, considerando a velocidade e a capacidade disponível. A preparação pode iniciar com um buffer parcial após 350 ms; sem quadros prontos, a espera é limitada a 1,2 s. Quadros incompletos não são declarados prontos. O custo do preparo alimenta a qualidade automática.
- Depth map de imagem e rotobrush trabalham fora da thread de renderização. A fila preserva a vez de cada fonte e substitui pedidos antigos somente da mesma fonte. Falhas de memória/criação de worker são tratadas.
- U2Net-P já existente no projeto passou a ser embutido no motor; o rotobrush não precisa baixar pesos ao reinstalar ou reabrir projetos. Continua sendo o mesmo modelo de recorte, sem promessa de segmentação de qualquer objeto ou precisão perfeita em cabelos e movimentos rápidos.
- Android e iOS mostram o estado real de processamento/falha da análise local. O status não depende da existência de cache de preview.
- A câmera externa só é herdada por uma pré-composição enquanto está ativa. Após seu fim exclusivo, a câmera interna pode assumir.
- O fundo HDR ganhou intervalo explícito `[início, fim)`, persistência e controles nas duas interfaces, incluindo “Usar duração da camada”. Projetos antigos continuam com duração global por padrão. O intervalo controla o fundo; a iluminação continua sendo uma configuração da composição.
- Ambientes de pré-composições guardam mapas de iluminação separados, preservados durante a submissão do quadro. Agrupar conserva ambiente, sombras e qualidade da cena, sem duplicar pós-processamento. Quando a cena 3D inteira é movida, também conserva chão e reflexos; grupos parciais não recebem um segundo chão. A memória dos ambientes entra na contabilização, com limite do cache ocioso.
- Motion Tile ganhou escala uniforme animável sem renumerar parâmetros antigos. A região visível deixa de ser cortada pelo limite artificial de 24 vezes a fonte; a densidade do alvo acompanha a projeção e o limite de textura.
- Android e iOS têm topo e doca menores, menos superfícies decorativas, painéis com rolagem e adaptação à fonte. Áreas de toque continuam acessíveis. Rótulos visíveis curtos evitam reticências com fonte maior; leitores de tela mantêm os nomes completos.
- Criação, leitura e descarte da textura de captura são sincronizados com o renderizador. Antes, a captura podia acessar a fila GPU ao mesmo tempo que a prévia e devolver zero bytes de forma intermitente.
- Capturas estáticas agora esperam o resultado exato da IA, inclusive após reabrir, liberando os locks enquanto os workers processam. Elas fixam composição/quadro e cancelam se o projeto ou a GPU mudar. As prévias continuam assíncronas. A exportação MP4 já usava qualidade final; não dependia desse conserto de captura.
- Conta-gotas, capas e capturas de diagnóstico usam processamento fora da interface. Pedidos atrasados são descartados ao trocar projeto/painel. No iOS, o encerramento drena as capturas antes de liberar o motor, sem aguardar inferência na main thread.

## Compilação

- Android arm64: UI test, instrumentação e release compilaram; 362 testes JVM em 67 suítes passaram, sem falhas ou erros. Conferência final: `android-build-delivery.log`.
- Motor compartilhado Windows: passaram os grupos PreviewBuffer (13), SceneCuts (7), MotionTile (41), PanoramaRange (2), Depth (23), ForegroundAI (4), Rotobrush (5), Capture (7) e Offscreen (3), sem falhas. Filtros podem incluir o mesmo teste; esses números não devem ser somados como uma contagem de casos únicos. Logs finais: `host-*-acceptance.log`.
- As regressões de captura verificaram inferência fria, pixels idênticos após reabrir, saída sem aguardar a inferência bloqueada e rejeição de um alvo GPU antigo mesmo quando o backend reutiliza o mesmo ponteiro. O ensaio de saída durante espera de vídeo mediu 0,1 ms no computador.
- iOS: implementado; 13 auditorias de fonte/ponte passaram. Sem `swiftc`/Xcode neste host, o harness Swift de concorrência foi explicitamente ignorado. Não houve compilação Swift/Metal.

## Aparelho físico

Moto g52, Android 13, 1080 × 2400, aproximadamente 4 GB de RAM. Preferências preservadas: fonte 130%, densidade configurada 446. A execução usa `com.aurea.aurea.uitest` para não misturar dados de testes com os projetos da instalação principal.

Reprodução/seek de vídeo e contorno de texto passaram. O teste de startup exige avanço real da timeline, evitando medir apenas a resposta otimista da interface: a rodada medida iniciou em 111 ms, no cenário do teste.

A prévia real de Depth ficou pronta em aproximadamente 5,2–7,3 s nas rodadas observadas; Rotobrush em aproximadamente 5,7 s. O resultado foi observado na tela antes de chamar captura de exportação. O teste antigo esperava uma faixa de cache que permanecia vazia mesmo com a imagem correta; esse critério foi substituído por verificação dos pixels apresentados e estado real da análise.

A iluminação agrupada teve diferença máxima de 1 nível antes/depois e luminância idêntica nas dez amostras durante reprodução. Motion Tile passou nos seis casos combinando escalas de 1%, 3% e 50% com posição central/distante: zero pixels escuros, inclusive nas bordas. A exportação MP4 de quatro quadros com recorte passou, sem fallback de quadro, após salvar e reabrir o projeto.

Rodada final: **9 testes passaram em 82,549 s**, sem falhas (`android-device-acceptance.log`). Inclui buffer, vídeo/seek, contorno de texto, Depth e Rotobrush offline, reabertura, intervalo HDR, iluminação agrupada, três repetições completas dos cortes de câmera, Motion Tile e exportação MP4. Depois de reabrir o recorte, o centro manteve RGBA `[220, 40, 30, 255]` e o canto ficou `[0, 0, 0, 255]`. Evidências visuais: `screenshots-final/`; métricas: `device-acceptance-metrics.log`.

Esses ensaios não comprovam IA em tempo real nem qualidade de recorte em todo tipo de cabelo, movimento ou vídeo longo.

Não houve validação em iPhone. Auditorias estáticas e testes do motor no computador não comprovam execução nativa iOS.

## Instalação e entrega

Atualização instalada com sucesso em `com.aurea.aurea` no moto g52, em 04/10/2026 às 23:36 (horário local), preservando os dados. O projeto `Teste-AUREA` abriu na instalação principal; a migração para o formato 42 guardou uma cópia do original. Captura da edição instalada: `dock-final.png`. Fonte e densidade foram mantidas; `stay_on_while_plugged_in` voltou ao valor original `0`. A instalação de testes foi encerrada.

APK: `build/reference/device-fixes-20261005/AUREA-2139-correcoes-arm64.apk` (versão 2139 / 2.0.0-beta2). O arquivo entregue é idêntico ao APK release da compilação final.

SHA-256: `AADD5894F7D035AC9E1EE3FEE96DA42E93EBC3A781F75DD2F49FCC10DCFDF699`.

## Referências de comportamento

- [Adobe: prévia e cache](https://helpx.adobe.com/after-effects/desktop/view-and-preview/preview-video-and-audio/previewing.html).
- [Adobe: Motion Tile, repetição e bordas espelhadas](https://helpx.adobe.com/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/stylize-effects.html).
- [Android: acessibilidade e áreas de toque](https://developer.android.com/guide/topics/ui/accessibility/views/apps-views).

As referências especificam comportamento. Nenhum código, shader ou asset extraído de outro aplicativo foi incorporado.
