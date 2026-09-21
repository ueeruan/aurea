# FrameGraph e EffectGraph

Estado: **implementado** (Fase 2). Código: `engine/include/aurea/render/FrameGraph.hpp`,
`src/render/FrameGraph.cpp`, `include/aurea/effects/EffectGraph.hpp`,
`src/effects/EffectGraph.cpp`.

## FrameGraph

Um frame é declarado como passes que leem e escrevem texturas lógicas. O grafo
decide ordem, dependências, alvos físicos, tempo de vida, reuso e barreiras; os
passes só gravam comandos.

```cpp
FGTexture t = graph.create_texture("blur-h", desc);
u32 p = graph.add_raster_pass("blur-h", PassStage::Effects, t, LoadOp::DontCare, clear,
                              [=](PassContext& pc) { pc.cmds.bind_texture(0, pc.texture(src), s); … });
graph.read(p, src);
graph.set_output(finalTex, ResourceState::Present);
graph.compile(pool);
graph.execute(cmds, timers);
graph.release(pool);
```

- **Passes**: raster, compute e transfer; `read`, `write_storage`,
  `copy_source/destination`, `mark_side_effect`. O callback é um
  `InplaceFunction<void(PassContext&), 256>`: sem alocação de heap por passe.
- **Ordenação**: arestas por versionamento SSA — cada escrita cria uma versão; um
  leitor depende do último escritor ANTES dele na declaração (ou do primeiro
  depois, se não houver). Kahn com a ordem de declaração como desempate. (A regra
  anterior, WAR por ordem de declaração, criava ciclo falso quando o leitor era
  declarado antes do produtor.)
- **Culling**: só sobrevive o que alcança uma saída ou tem efeito colateral.
- **Tempo de vida e reuso**: texturas lógicas com descrições compatíveis
  (`TextureDesc::compatible`) reusam o mesmo alvo físico dentro do frame
  (aliasing); entre frames, o `TransientTexturePool` guarda os alvos por 120
  frames ociosos — em regime o playback não cria textura nenhuma (há teste).
- **Barreiras**: planejadas no `compile` a partir dos estados (`ResourceState`)
  de uso; a primeira escrita de um alvo descarta o conteúdo anterior. O grafo
  abre e fecha as render passes.
- **Diagnóstico**: `stats()` (passes executados/cortados, texturas físicas,
  aliasadas, criadas), `order()`, `physical_slot()`, `barriers_before()`,
  `dump()`; timers de GPU por estágio (conversão de cor, efeitos, blur, glow,
  composição, saída).

## EffectGraph

Transforma a pilha de efeitos de UMA layer em passes do FrameGraph.

- **plan** (sob o lock do modelo): avalia os parâmetros no instante local
  (constante ou keyframe; expressão cai na constante por enquanto), descarta
  efeitos identidade e desconhecidos, funde efeitos por pixel e calcula margens e
  região visível. Resultado: `EffectPlan` com estágios `FusedColor` ou `Single`.
- **build** (sem lock): emite os passes.

Regras do plano:

- **Fusão**: efeitos `PerPixel` consecutivos viram UM passe
  (`effects/color_stack.frag`), até 12 operações e no máximo uma LUT de Curvas
  por passe.
- **Transform dobrado**: o efeito Transformar como último da pilha vira parte da
  matriz da composição — nenhuma textura extra.
- **Margens**: efeitos de vizinhança (blur, glow, nitidez) propagam para trás a
  margem que precisam; a região é recortada ao que aparece na composição.
- **Gaussian**: redução em pirâmide enquanto sigma > 8 texels, pesos
  bilineares em pares.
- **Motion Tile** (único efeito portado do Aurea antigo): cobertura calculada
  pelas bordas da composição na matriz inversa, ladrilho central do mesmo tamanho
  e no mesmo lugar, espelho, fase, borda — com testes de regressão para os bugs
  antigos ("zoom para baixo", "não ladrilha no preview", "muda a posição da
  layer").

Parâmetros de efeito: `ParameterRegistry` por tipo (Float, Int, Bool, Color,
Point2D/3D, Angle, Enum, Curve, Gradient, referências), cada um com constante,
trilha de keyframes (chave = id estável do efeito + `param*4 + componente`) ou
expressão. Efeitos embutidos (12): Transformar, Exposição, Brilho e contraste,
Saturação, Tingir, Matriz de cor, Níveis, Curvas, Desfoque gaussiano, Nitidez,
Brilho (glow), Motion Tile.

## Testes

`engine/tests/test_render.cpp` (ordem SSA, culling, reuso, barreiras, fusão,
dobra do transform, margens, geometria do Motion Tile) e os golden frames de
`test_gpu.cpp`.
