# Contorno, máscaras, sombra e fidelidade de pré-composições

Registro desta rodada, com artefato intermediário. As compilações iOS,
correções adicionais e validações posteriores estão em
[MOTION_TILE_STRESS_2026-10-05.md](MOTION_TILE_STRESS_2026-10-05.md).

Solicitação: quatro relatos visuais enviados pelo usuário. Na comparação entre versões, o usuário esclareceu que camadas/objetos somem ou mudam; não se trata apenas de atraso na reprodução. As imagens são referências de sintomas, sem projeto original, versões identificadas ou quadros equivalentes para comparação.

As alterações anteriores do checkout foram preservadas. Evidências novas ficam em `build/reference/community-fixes-20261005/`.

## Implementação compartilhada

- **Contorno de texto:** alterar a largura compensava incorretamente o pivô do texto. A nova margem agora desloca as coordenadas da âncora e seus valores animados pelo mesmo delta, conservando âncoras manuais, tempos, interpolação, tangentes e componente Z. A regra fica no comando C++ usado por Android e iOS.
- **Máscaras em pré-composições:** o alvo filho era descrito pelas dimensões calculadas a partir da escala/projeção do pai, embora sua textura tivesse a resolução da composição. O processamento de uma máscara neutra reamostrava a imagem e alterava bordas/detalhes. Agora usa as dimensões reais da textura.
- **Cópias de uma pré-composição:** a identificação anterior dependia da composição filha, sem distinguir suas instâncias. Dois vídeos em tempos diferentes podiam compartilhar o decoder e os planos de textura. O namespace agora usa o caminho completo da instância, incluindo gerações dos IDs, e permanece estável entre quadros e buscas na timeline.
- **Drop Shadow:** a expansão da região repetia os pixels opacos da borda original sobre a sombra. A amostragem do clipe usa borda transparente. O novo parâmetro booleano “Só a sombra” ocupa o slot 5, sem renumerar os cinco existentes. Opacidade zero com isolamento produz transparência. Android e Swift têm o controle, tradução nos sete idiomas e identificação/estado acessíveis.

## Validação

Android arm64 (release, UI test e instrumentação) compilou. Os 362 testes JVM passaram, sem falhas ou erros. Não confundir testes do motor compartilhado ou auditorias de fonte com execução das interfaces nativas.

Os cenários preparados verificam contorno com pivô manual/animado, diferentes escalas e perspectiva; máscara neutra em pré-comp projetada; vídeo duplicado com tempos independentes; conteúdo em resoluções 1, 1/2 e 1/4 com e sem cache; sombra sobre alfa de vídeo/imagem, recorte, isolamento, transparência e persistência.

No computador, o motor Vulkan compilou e passaram os filtros TextOutline (4 testes/496 verificações na compilação final), DropShadow (5/71740), MaskEdges (1/24), PrecompInstances (2/78), PreviewFidelity (1/76), PreviewBuffer (13/304), Precompose (2/25) e SceneCuts (7/178). Filtros podem se sobrepor; não somar essas contagens como casos únicos. Os 18 cenários do novo teste de contorno tiveram zero pixels brancos deslocados/cobertos pelo critério do teste. Cada objeto permaneceu visível nas três resoluções de prévia; a imagem em cache foi idêntica à renderização direta na mesma resolução. A proteção final também rejeita comandos de contorno em camadas não textuais antes de alterar histórico ou estado.

No moto g52, contorno passou em seis cenários com perspectiva, âncora manual/animada e três escalas. A máscara neutra não alterou nenhum nível de pixel em pré-comps 2D/3D. Cópias de vídeo com deslocamento de 30 quadros mantiveram diferença média zero em relação às capturas individuais nos três seeks e após reabrir. A iluminação agrupada manteve luminância constante nas dez amostras de reprodução; câmera e HDR respeitaram os cortes.

A rodada adicional de seis testes Android passou em 60,864 s: buffer, contorno básico, reprodução/busca de vídeo, Depth/Rotobrush, Motion Tile e exportação. O teste novo de sombra passou nas verificações iniciais de imagem, mas sua navegação precisou ser ajustada: o botão usa descrição acessível, a lista é virtualizada e o cabeçalho é identificado pelo tipo do efeito, não pelo ID da instância. A hierarquia real e a captura revelaram a divergência de ID. O harness final monta o painel de produção sobre o editor, rola a lista e toca o interruptor real; passou em 8,43 s, incluindo acessibilidade, alfa, isolamento, opacidade zero e persistência ao reabrir (`android-shadow-acceptance.log`).

Ao todo, **13 casos Android distintos passaram ao longo das rodadas**. As primeiras falhas de navegação do teste foram preservadas nos logs, sem contá-las como execuções aprovadas. O contorno foi repetido na compilação final após a proteção do comando.

iOS: alterações implementadas. Treze auditorias de fonte/ponte passaram. Não há Xcode/Swift nem aparelho iOS neste ambiente; não houve compilação Swift/Metal ou validação em iPad/iPhone.

## Entrega

Versão final instalada com sucesso no moto g52 em **05/10/2026 às 00:02:53**, preservando os dados de `com.aurea.aurea`. O projeto existente `Teste-AUREA` permaneceu na biblioteca. A instalação de testes foi encerrada e `stay_on_while_plugged_in` foi restaurado para `0`; fonte, densidade e alto contraste do usuário foram mantidos.

APK: `build/reference/community-fixes-20261005/AUREA-2139-contorno-mascaras-preview-arm64.apk`, 62.713.045 bytes, versão 2139 / 2.0.0-beta2.

SHA-256: `A5A0DCC7F872E381A9F5A0DFBF8009290C90AA32C7769426CF5A0F5D2C8F039E`.

As causas reproduzíveis acima foram corrigidas e verificadas. Sem o projeto original e as versões da comparação, não é possível afirmar que todo artefato específico das imagens do iPad foi reproduzido.

## Referências de comportamento

- [Adobe: efeitos de perspectiva, Drop Shadow e Shadow Only](https://helpx.adobe.com/nz/after-effects/using/perspective-effects.html).
- [Adobe: canais alfa, máscaras e mattes](https://helpx.adobe.com/after-effects/desktop/work-with-transparency-and-compositing/work-with-alpha-channels-and-masks/alpha-channels-masks-mattes.html).
- [Adobe: prévias e cache](https://helpx.adobe.com/after-effects/desktop/view-and-preview/preview-video-and-audio/previewing.html).

As referências orientam o comportamento; nenhum código, shader ou asset extraído de outro aplicativo foi incorporado. Os testes usam conteúdo procedural ou as mídias de teste do próprio repositório.
