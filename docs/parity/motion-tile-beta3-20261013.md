# Motion Tile — AUREA 0.1.8 BETA 3

## Relato e observação

A referência enviada é [o vídeo de 47 segundos](https://streamable.com/922t9j).
Na parte final, uma imagem assimétrica recebe Motion Tile, espelho ligado e
largura/altura/escala em 100%. O usuário modifica a rotação X de um plano 3D.
Em aproximadamente 45–46 s, o indicador muda de −101,2° para −88,4° e o contorno
do plano se altera; visualmente, a textura da prévia e duas faixas horizontais
continuam praticamente iguais. O vídeo não mostra a identificação da build.

Isso sugere uma prévia desatualizada ou repetição da imagem após sua projeção.
O vídeo sozinho não estabelece qual dessas hipóteses ocorre, nem identifica
o binário executado. Não foi usado como prova de uma falha no código atual.

## Referência Alight Motion pelo REA

O REA inventariou o APK fornecido, SHA-256
`4caeb6dea44e8f123b88833aef7b28dd54f4a48fb0cc6a9e5085ba4c1bbf4159`.
Evidência do inventário:
`ev_f6625097377a4f5c9c1b03f6dc96be2fa6d9e8f592d0a693369017ee83d2a31f`.
Os recursos selecionados e seus digests estão em
`build/reference/beta3-20261013/tile-resources/manifest.json`.

O recurso `assets/effects/tile.xml` define Tiles com conteúdo da camada,
sobredesenho, coordenadas locais normalizadas, espelho, deslocamento alternado,
eixo do deslocamento e ângulo. Essa análise estática sustenta a especificação
de uma repetição no domínio da camada que acompanha sua transformação. Ela
não comprova execução do renderer do Alight Motion. Seus controles também não
são idênticos ao Motion Tile existente no AUREA; os endereços dos parâmetros
salvos e seus keyframes foram preservados. Nenhum shader, código, ícone ou
textura externa foi incorporado ao produto.

## Código atual e regressões

O renderer compartilhado produz a textura da camada e seus efeitos antes de
desenhá-la como `ScenePlane` na cena 3D. O Motion Tile trabalha nesse domínio;
a projeção e a profundidade são aplicadas depois. Android e iOS enviam o gesto
de rotação para o mesmo motor, por `gestureKeyframe`.

A auditoria estática das duas bridges encontrou a publicação de comandos e
`request_render` nos dois caminhos. Um `KeyframeInsert` de transform que muda
o valor incrementa a revisão do modelo. A revisão entra na chave da prévia,
e o render drena os comandos antes de decidir reutilizar a imagem. Um grafo
segmentado anterior também é cancelado quando essa revisão muda. Não foi
encontrada uma condição concreta que exclua a rotação 3D desses mecanismos;
essa auditoria, isoladamente, não comprova apresentação nativa correta.

Foram acrescentados dois testes GPU em `engine/tests/MotionTileChainsGpu.inl`:

- Uma textura assimétrica própria, com os gestos reais de rotação X em
  −101,2°, −88,4° e −221,2°. Os pixels precisam mudar, uma captura repetida deve
  permanecer estável e salvar/reabrir deve preservar o resultado.
- Interseção independente de cada raio de tela com o plano. Uma região do
  plano atrás da câmera não pode produzir outra cópia do piso na composição.

Compilação MSVC Release e execução Vulkan NVIDIA: **2 testes, 38 verificações,
zero falhas**, incluindo o caminho final de gesto usado por Kotlin e Swift.
As duas mudanças de ângulo alteraram 144.385 e 144.458 pixels. A cobertura
medida foi 0,715, 0,531 e 1,000. Dos 4.032 e 7.392 pontos amostrados atrás da
câmera nos dois primeiros ângulos, nenhum exibiu repetição indevida.
Log: `build/validation/beta3-20261013/host/MotionTileBeta3.log`.

A suíte completa filtrada por `MotionTile` também passou: **57 testes,
45.263 verificações, zero falhas**. Inclui os dois casos novos acima, que não
devem ser somados novamente, além de espelho, fase, projetos antigos,
Transform, margens de blur, Squeeze, vídeo quadrado e janela sob perspectiva.
Log: `build/validation/beta3-20261013/host/MotionTile.log`.

As imagens próprias e o projeto de regressão estão em
`build/validation/beta3-20261013/host/build/reference/beta3-20261013/motion-tile-tests/`.
Essa bateria usa capturas novas do motor; por si só, não prova atualização da
superfície apresentada pela interface Android.

## Validação da prévia Android

Foi implementado
`android/app/src/androidTest/java/com/aurea/aurea/editor/MotionTilePreviewRotationDeviceTest.kt`.
Ele lê a `SurfaceView` apresentada com `PixelCopy`, sem chamar `captureFrame`
para renderizar uma imagem nova. Usa textura própria, projeto isolado de teste,
prévia parada no quadro zero e o gesto `EditorStore.setTransform` nos mesmos
ângulos. A sequência final mantém o grupo `beginGesture("rotação")` aberto
durante todos os três ângulos e todos os `PixelCopy`, fechando `endGesture`
somente ao final, como o dial durante um arrasto. A camada permanece selecionada durante toda a sequência; depois de
cada gesto não há seleção, seek, mudança de modo ou captura forçada que possa
acidentalmente atualizar uma prévia congelada. A amostra exclui barras e
sobreposições Compose e registra o estado de memória.

O teste também calcula uma homografia independente a partir dos quatro cantos
originais publicados pelo modelo. Compara as cores R/G da textura própria no
domínio projetado e espelhado, com tolerância de 12/255 e no máximo 5% de
amostras divergentes; exige mais de 500 amostras válidas. Regiões cuja pegada
de um pixel já mistura muitas cópias perto do horizonte não recebem uma
expectativa pontual inadequada. Raios suficientemente atrás da câmera não
podem mostrar textura. A imagem apresentada precisa corresponder ao ângulo
atual antes de iniciar o gesto seguinte, além de mudar efetivamente seus
pixels. Esse teste verifica domínio e atualização; sua exclusão explícita do
horizonte ainda não demonstra ausência de aliasing em todas as frequências.

Execução final Android: instrumentação compilada e **2 testes aprovados** em
7,978 s no emulador isolado `emulator-5580`, API 31, Vulkan SwiftShader, APK
intermediário `.uitest` 2176. O primeiro ativa a flag 3D explicitamente. O
segundo preserva a flag desativada antes e depois de todos os gestos; a
rotação deve acionar a projeção pelo caminho normal da interface. Ambos
passaram também com o grupo do gesto ainda aberto. Isso comprova apresentação
nesse cenário do APK de teste, sem afirmar validação da build final 2179.

Em ambos os casos, a `SurfaceView` mudou 36.284 e 36.554 de 36.864 pixels
normalizados nos dois primeiros giros. O terceiro mudou 36.627 pixels com a
flag explícita e 36.626 com a flag implícita. As comparações nos quatro ângulos usaram respectivamente
3.864, 1.932, 1.680 e 3.864 pontos, com zero divergências acima da tolerância.
Nenhum raio atrás da câmera exibiu textura, o erro médio R/G ficou entre
0,73 e 1,14/255 e `lowMemory=false` em todas as amostras.

Uma execução anterior em lote recebeu `ERROR_SOURCE_NO_DATA` no primeiro
`PixelCopy`, antes de apresentar qualquer frame ou testar rotação. O teste
agora repete somente esse estado de prontidão dentro do prazo já existente
de 8 s, sem forçar render e sem mudar limites de comparação. Nenhuma mudança
de produto foi atribuída a essa falha inicial da fixture.

Log final: `build/validation/beta3-20261013/android/device-motion-tile-open-gesture.log`.
PNGs e métricas finais, separados por flag 3D:
`build/validation/beta3-20261013/android/screenshots/open-gesture-com.aurea.aurea.uitest-emulator-5580-motion-tile/{explicit,implicit}/`.
SHA-256 da fixture final:
`1EDEFDF4A1A21DB927C3B9A9E224F3073620410D2818C034BA57AC9FCF0E4FEF`.
A imagem de −88,4° apresenta faixas de cor finas junto ao horizonte, a região
explicitamente excluída da comparação pontual. A aprovação desta bateria não
comprova ausência dessas faixas, nem execução em GPU Adreno ou iPhone físico.
Nenhum teste da interface iOS foi executado nesta máquina Windows.

Revalidação posterior: os mesmos dois casos de prévia receberam
`INSTRUMENTATION_STATUS_CODE: 0` no APK `.uitest` 2179. Registro:
`build/validation/beta3-20261013/android/device2179-export-motion-timeline-navigation.log`.
Esse lote também continha uma fixture de exportação ainda incorreta e um nome
de classe de timeline inválido, portanto o lote completo terminou com falhas;
não se descreve o conjunto como aprovado. As duas aprovações Motion Tile são
separadas desses erros e não ampliam o domínio visual do teste ou comprovam o
projeto original. Os erros de exportação/runner foram tratados em suas próprias
rodadas posteriores. Após informar que não sabia a build do vídeo, o usuário
orientou a avançar com as demais correções.

Ainda não houve nova alteração no efeito ou shader: o congelamento e as
repetições indevidas em regiões resolúveis do vídeo não se reproduziram nesses
cenários do código atual. As faixas junto ao horizonte exigem uma investigação
de amostragem separada; não foram descartadas como inexistentes pelo teste.

## Retomada: horizonte, cobertura e amostragem

Os resultados acima são históricos. A retomada de 13/10 ampliou o oráculo para
a região que aquela comparação pontual excluía. A build original do vídeo
continua desconhecida. A nova consulta ao Streamable não abriu a URL e o
browser do ambiente retornou `NoBrowserAvailable`/`Browser not available: iab`;
as observações anteriores permanecem referências, sem uma nova reprodução do
aplicativo original.

Foi reproduzida uma falha concreta no renderer atual com imagens próprias:
um plano branco opaco, nas rotações X −88,4° e −101,2°, perdeu respectivamente
170/25.670 e 2.210/35.700 amostras de raios válidos no frustum. Perto do
horizonte, a inversa dos cantos da composição não define um retângulo finito
inteiro diante da câmera; o fallback antigo limitava a parede a 24 vezes a
fonte. Raios válidos que cruzavam o plano além desse limite ficavam sem cor.

Uma segunda imagem própria, com linhas pretas/brancas de quatro texels,
reproduziu faixas de aliasing dentro da própria parede antiga. O oráculo
integrou 4.096 subamostras do pixel pela homografia inversa e pela reconstrução
bilinear da fonte. Todas as oito e quatorze linhas minificadas falharam antes,
com erro linear máximo de 0,5412 e 0,5640. A projeção amostrava uma textura
grande com filtro bilinear de um único nível, saltando repetidamente o conteúdo
de muitos ladrilhos. A reprodução separa esse problema do corte geométrico;
não estabelece que o binário desconhecido do vídeo tenha as mesmas causas.

O novo caminho compartilhado trata o Motion Tile terminal de um plano 3D como
repetição da fonte local. Resolve a interseção do raio no fragment shader,
rejeita pontos atrás da câmera/fora da profundidade e escreve a profundidade
física para cruzar os outros planos e modelos. Uma pirâmide filtrada de uma
única fonte e até oito amostras na direção de maior minificação substituem a
parede rasterizada. As texturas são RGBA linear pré-multiplicadas, para manter
a contribuição correta das áreas semitransparentes.

O caminho é elegível quando a última etapa do `EffectPlan` é um Motion Tile
único, no plano 3D, sem transformação final dobrada (`hasFold=false`). Efeitos
anteriores continuam passando pelo `EffectGraph` e sua saída vira a fonte do
ladrilho. Camadas 2D, Motion Tile com efeitos posteriores, planos sem esse
efeito e cadeias com a transformação final dobrada continuam no grafo
existente. Falha de construção da pirâmide também retorna ao caminho antigo.
Esta mudança não promete remover aliasing do horizonte nessas cadeias não
elegíveis. Os parâmetros salvos, índices, keyframes e gestos permanecem os
mesmos.

São no máximo 12 bindings de sampler, cada acesso escrito com índice constante
explícito. Não é necessária extensão de indexação não uniforme. A pirâmide
completa chega a 1×1; fontes acima de 2.048 texels conservam os onze níveis mais
finos e a média 1×1 no último binding. As dimensões obedecem ao limite de
textura do contexto existente. Os níveis reduzidos somam no máximo a área de
uma fonte mais 14 texels (aproximadamente um terço para fontes 2D grandes).
Não foi acrescentado cache persistente: as texturas temporárias reutilizam o
pool e o orçamento de recursos do FrameGraph; a pirâmide é construída a cada
frame elegível. Uma fonte já ajustada à caixa é emprestada como nível zero.

O primeiro AFTER passou **1 teste/24 verificações**: nenhuma amostra branca
sumiu e todas as 22 linhas integradas passaram, com erro máximo de 0,0260.
Logs e identidades anteriores/posteriores:
`build/validation/motion-tile-finish-20261013/horizon-{before,after}-*.{log,json}`.
As imagens e o log BEFORE foram preservados em
`build/reference/motion-tile-finish-20261013/horizon-before-own.zip`, SHA-256
`fbe254b3513ee294b70405918d2ea96c3af774fb43cc79f6288bf7fe91e265f6`,
inventariado pelo REA em
`ev_9d53f629822160541ab3e5709b6035cc74c6627c6b52bffdf0efc816cb5dbc78`.
O inventário liga bytes e integridade; a execução própria de teste Vulkan é
evidência complementar. A capacidade de execução nativa REA neste Windows
continua limitada pelo perfil Linux x64 do provedor, sem afirmar captura REA
de chamadas do driver.

As regressões verificam centro/fase/eixo, espelho, escala, janela, exposição e
desfoque anteriores, RGBA pré-multiplicado, custo/reutilização, iluminação e
profundidade física com um modelo triangular próprio. O alfa foi exercitado
com `transparent_background=true`: apenas pôr alfa zero na cor do fundo não
altera o modo opaco existente.

Uma regressão final de emenda sem espelho comprovou outro defeito: a redução
prévia da fonte de 128 para 16 texels fazia o footprint de oito pixels originais
aparecer como aproximadamente um texel intermediário. O shader classificava
essa minificação como magnificação e prendia a leitura à borda. O caso BEFORE
deu 1,0 contra a integral independente 0,5; o controle espelhado permaneceu
zero. O caminho novo usa a pegada na fonte lógica **e** na textura intermediária
para escolher a regra de emenda; o LOD continua medido nos texels intermediários.
O AFTER deu 0,5, sem afrouxar a tolerância 0,03. O nível da pirâmide integra
2×2, em vez do filtro 4×4 de um blur, e a emenda sem espelho usa sampler Repeat.
BEFORE preservado: `isotropic-seam-before-own.zip`, SHA-256
`91fe344e90f9a57a6e85d8c6337b425868d792f244d4797dd05a6bcdef646549`, REA
`ev_c23c05d7eb9baa3a464ac71a3765d3957fa89cd99f7db2cfd9e6316f1894a3a3`.

A rodada host final passou **64 testes/53.604 verificações**, incluindo sete
regressões novas. As linhas integradas perto do horizonte passaram com erro
linear máximo 0,0412/0,0372 nas duas rotações. O erro 0,0260 acima é o primeiro
AFTER intermediário, não a medição do filtro final. Log e identidade finais:
`build/validation/motion-tile-finish-20261013/seam-density-final-host-*`.

No Android API31 x86_64, Vulkan, passaram **4/4 testes em 13,371 s** sobre a
SurfaceView apresentada de verdade, com PixelCopy, fontes próprias, frame zero
pausado e gestos de rotação. Cobriram 3D explícito/implícito, cobertura branca
e faixas finas, sem exigir captura forçada do motor. APK imutável SHA-256
`b9aebd34b2b318c0841ff69f5d640db9c4ef896ac19103cbc4ac50518c08016c`, biblioteca
`f442fcb92d5a62e7511eaf2a0fdcee7e96b1d9c4edb50f8d954c324891c9da16`.

O primeiro PixelCopy falhou na classificação de uma linha ao horizonte:
bilinear de apresentação e redimensionamento misturavam a primeira linha
válida com amostras cujo centro já estava atrás. Os PNGs brutos mediram uma
SurfaceView 516×328, recorte útil 516×290 e suporte de três pixels de composição.
A fixture agora exige que os quatro cantos desse suporte estejam atrás para
afirmar pixel indevido; limites de cor, integral, cobertura e alfa foram mantidos.
BEFORE preservado `android-pixelcopy-before-own.zip`, SHA-256
`18b1ca0b8e97d5d8b8c16cdb709af54a68f064ba21f108e9a543420c2d23d663`, REA
`ev_a64f7c5c37399371acb6ec7bb1e1207cacd1b35bb9e1783da599bde60203efc6`.
A tentativa de instalar durante a recompilação da fixture foi descartada pelo
guard de identidade; a rodada válida instalou apenas após terminar o Gradle.

O AVD expõe OpenGL ES3.0; o backend GLES exige ES3.1 e recusou a inicialização.
Assim, execução nativa GLES está **indisponível por capacidade**, sem declarar
PASS nem falha do shader. As traduções GLSL/ESSL/MSL verificaram bindings
constantes0..11, UBO752 bytes/push112 e MRT+profundidade. Tradução MSL não
comprova execução iOS. Este emulador também não comprova compatibilidade com
celulares OPPO/Vivo/Realme ou causa do vídeo original desconhecido.

Manifesto: `build/validation/motion-tile-finish-20261013/final-manifest.json`.
Capturas/logs AFTER: `motion-tile-final-own.zip`, SHA-256
`de87c97ed87d0c11f62e915af30b56524165209ebdf93c86c81e9e3441712f32`, REA
`ev_8c476288f73d3a5aa75ec490c4bfcef9c64a79087e72c61ae3d191807272ea36`.
Pacote runtime REA `ev_bb110c4f35b1f444c030a61f09844e87a881a787bf11fd2b760d9e0f95c2b9ab`;
logcat final limitado `ev_ff1acc5cd98435838d84b29c1c7e44e83766782954bd8ad988051007a9fef32a`.
O APK próprio imutável também foi inventariado pelo REA:
`ev_4e3bf2b99496e86612bb76fa93b0cf60799a4663cffd0dd8cb9de8f533101fc0`.
