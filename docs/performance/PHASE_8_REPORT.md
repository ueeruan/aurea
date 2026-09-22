## 8E — Sistemas pesados (texto, vetor, máscara, 3D, partículas, optical flow, tracking, efeitos)

### Ambiente (o que estes números são e o que NÃO são)

| | |
|---|---|
| Máquina | PC de mesa: AMD Ryzen 5 5500, NVIDIA GeForce RTX 3050 (Vulkan), Windows 11 Pro 22631 |
| Build | Release, MSVC (`aurea_tests.exe`: motor real, backend Vulkan real, validação desligada na medição) |
| Projeto / alvo | 1080p (1920×1080) fora da tela, RGBA16F; preview = mesma resolução com a escala pesada indicada (0,5 / 0,25) — a resolução do preview (1/2, 1/4) é da 8C |
| FPS / quadros perdidos | não se aplica: render fora da tela, sem present (GPU e CPU por quadro abaixo) |
| GPU | timestamp por passe, **mediana** de 8 quadros em regime (o anel de 2 em voo devolve o quadro de 2 atrás, com o mesmo conteúdo) |
| CPU | `prepare` + gravação do FrameGraph, mediana. O host estava carregado pelos builds das outras frentes: CPU tem ruído de ±30% |
| A/B | o **mesmo** `tests/test_heavy.cpp` compilado sobre a base (`be24f9c`) e sobre esta frente, rodado em sequência (`AUREA_HEAVY_V2` por `__has_include`) |
| Aparelho | **nenhum celular medido** (sem aparelho na bancada; emulador não vale para performance — SPEC "Limites"). iOS/Metal não testável aqui |

Reproduzir: `AUREA_BENCH=1 aurea_tests.exe Heavy`. Golden dos efeitos:
`AUREA_HEAVY_DUMP=<pasta>` grava a saída de EXPORT de cada efeito;
`AUREA_HEAVY_REF=<pasta>` compara (`AUREA_HEAVY_FULLRES=1` em 1080p,
`AUREA_HEAVY_ONLY=<chave>` filtra).

### O botão de qualidade dos sistemas pesados (para o AUTO da 8C)

`render/HeavyQuality.hpp`: um bloco por sistema em vez de um número só.
`RenderSettings::heavy` (+ `heavyExplicit`) define cada botão; sem ele,
`HeavyQuality::from_scale(heavyScale, previewDenominator)` desce a escada.
**Export (`finalQuality`) ignora os dois e usa `full()`** (teste
`Heavy.QualityLadderAndExportIsAlwaysFull`; e no 3D o export pedido durante o
calor crítico continua com mapa 2048).

| Botão | Cheio (export) | Escala 0,5 | Escala 0,25 | Onde |
|---|---|---|---|---|
| `particles` (fração dos slots) | 1 | 0,5 | 0,25 | partículas |
| `flow` (base da pirâmide ×384 px) | 1 | 0,5 | 0,25 | optical flow |
| `flowBlurSamples` | 16 | 8 | 4 | desfoque vetorial |
| `shadowMapSize` (também ÷ preview 1/2, 1/4) | 2048 | 1024 | 512 | 3D |
| `shadowFilter` | PCF 6×6 | PCF 2×2 bilinear | PCF 2×2 bilinear | 3D |
| `lodBias` (tamanho na tela ×) | 1 | 0,75 | 0,5 | 3D |
| `effects` (amostras dos caros) | 1 | 0,5 | 0,25 | Desfoque de lente, Raios |

Contadores para o HUD (8A): `Renderer::heavy_stats()` (`HeavyStats`) —
uploads/bytes do atlas, glifos rasterizados, atlas refeitos, retriangulações e
acertos do vetor, rasterizações e acertos de máscara, flow calculado/acerto/
despejo/bytes, e do último quadro: slots de partícula, desenhos 3D, desenhos
de sombra, desenhos instanciados, primitivas visíveis/recortadas, triângulos e
tamanho do mapa de sombra.

### 1. Texto, vetor e máscara (§64–67)

| Medição (motor inteiro, `Heavy.*`) | Antes | Depois |
|---|---|---|
| Texto com animador (máquina de escrever), 45 quadros em regime: uploads do atlas / glifos rasterizados / atlas refeitos | sem contador (o código só subia com glifo novo) | **0 / 0 / 0** |
| Vetor parado com a camada andando 20 quadros: retriangulações / acertos | sem contador | **0 / 20** |
| Vetor com morph FORA do intervalo dos keys (antes do 1º, depois do último) | retriangulava todo quadro (o quadro entrava no hash) | hash estável → 0 (`Heavy.VectorMorphHashIsStableOutsideTheKeyRange`) |
| Máscara parada com a camada andando 20 quadros: rasterizações / acertos | sem contador | **0 / 20** |
| Fontes de reserva no 1º caractere ausente | TODAS as 12 candidatas de uma vez (no Android inclui a CJK, a maior da lista) | uma a uma, pela escrita do caractere; para na 1ª que tem o glifo |

- O atlas SDF é por glifo na base de 64 px (escala não re-rasteriza); o
  buffer de glifos do quadro é um anel mapeado (sem textura). Glifo NOVO
  (digitar) ainda sobe o atlas inteiro (4 MB, R8 2048²) — upload parcial
  precisa de API de sub-região no backend (fora da posse da 8E).
- No host a fonte padrão já cobre árabe/hebraico, então o teste de reservas
  carrega 0; o ganho de RAM das reservas só aparece no Android (não medido
  aqui). `FontManager` guarda só as fontes realmente usadas (a lista de fontes
  lê só o cabeçalho de cada arquivo).

### 2. 3D (§68–75)

Capacete (DamagedHelmet: 15.452 triângulos, 5 texturas 2048²), 1080p.
"Crítico" = escala pesada 0,25 (calor crítico/AUTO).

| Cena | Modo | Desenhos cor | Desenhos sombra | Triângulos | Mapa | GPU sombra | GPU cor 3D | GPU total |
|---|---|---|---|---|---|---|---|---|
| 1 capacete | export — antes | 1 | 1 | 15.452 | 2048 | 0,047 | 0,263 | 0,367 |
| 1 capacete | export — depois | 1 | 1 | 15.452 | 2048 | 0,047 | 0,264 | 0,367 |
| 1 capacete | crítico — antes | 1 | 1 | 15.452 | 2048 | 0,047 | 0,222 | 0,325 |
| 1 capacete | crítico — depois | 1 | 1 | 15.452 | **512** | **0,011** | **0,161** | **0,227 (−30%)** |
| 25 capacetes | export — antes | 25 | 25 | 386.300 | 2048 | 0,135 | 0,906 | 1,095 |
| 25 capacetes | export — depois | **1** (instanciado) | **1** | 386.300 | 2048 | 0,134 | 0,868 | 1,058 |
| 25 capacetes | crítico — antes | 25 | 25 | 386.300 | 2048 | 0,135 | 0,902 | 1,092 |
| 25 capacetes | crítico — depois | **1** | **1** | **193.125** | **512** | 0,152 | **0,614** | **0,823 (−25%)** |

(ms de GPU; CPU do quadro 0,03–0,08 ms nos dois.)

- **Instancing**: malha + primitiva + LOD + material + pipeline iguais (sem
  skin/morph) viram UM desenho com N instâncias; matrizes num SSBO do quadro
  (duas mat4 por instância na cor, uma na sombra). Transparentes ficam fora
  (ordem por profundidade). Quadro instanciado × modelos separados:
  **diferença máxima 0/255** (`Heavy.Scene3DInstancingCullingAndShadowKnob`).
  Na RTX 3050 o ganho de GPU é pequeno (−3%); o que cai é o nº de chamadas de
  desenho (25 → 1), que é custo de driver/CPU no celular — não medido aqui.
- **Frustum culling**: contado (`visiblePrimitives`/`culledPrimitives`) e
  testado: caixa movida para fora da tela = 1 recortada, visíveis 4 → 3. As
  contas do 3D agora são do QUADRO inteiro (antes o `stats_` zerava a cada
  cena/subquadro de desfoque e mostrava só o último).
- **LOD sem popping**: histerese de ±15% em volta dos limiares (60/160 px) no
  preview — um tamanho oscilando em cima do limiar não troca a malha a cada
  quadro. O export escolhe sem histórico (o mesmo quadro sai igual sozinho ou
  em série). `lodBias` do preview crítico: 386.300 → 193.125 triângulos.
- **Sombra reduzível**: mapa 2048 → 512 e PCF 36 → 4 amostras no crítico
  (sombra 0,047 → 0,011 ms num capacete). Com 25 capacetes a sombra é limitada
  por vértice (0,13–0,15 ms nos dois tamanhos).
- **Deduplicação**: um `GpuModel` por asset — 1 e 25 capacetes ocupam os
  mesmos **107,7 MB** de GPU (malha, texturas, materiais; a animação é do
  asset compartilhado, só as matrizes avaliadas são por instância).
- **Mips / 4K**: textura acima de 2048 é reduzida NO IMPORT
  (`kModelTextureCap`, já existia) e a GPU gera os mips. Streaming de mips
  **não foi feito**. Achado para a 8B: o `SceneAsset` mantém as texturas
  decodificadas na RAM depois do upload — **80 MB** só no capacete — porque o
  modelo é re-subido após 240 quadros sem uso; soltar exige reler do arquivo
  (Engine, fora da posse da 8E).

### 3. Partículas (§76–79)

Emissor de 1600×900, tamanho 4→1 px, vida 4 s, instante 6 s (regime), 1080p.

| Partículas | Blend | GPU export antes | GPU export depois | GPU preview 0,5 antes | depois | CPU |
|---|---|---|---|---|---|---|
| 10k | aditivo | 0,086 | 0,090 | 0,080 | 0,085 | 0,02 |
| 100k | aditivo | 0,289 | 0,287 | 0,223 | 0,232 | 0,02 |
| 500k | aditivo | 1,132 | 1,105 | 0,823 | 0,867 | 0,03 |
| 1M | aditivo | 2,043 | **1,979** | 1,445 | 1,533 | 0,03 |
| 1M | normal | 2,057 | **1,974** | 1,439 | 1,522 | 0,02 |

- Partículas são ANALÍTICAS (shader resolve pela semente, slot, geração e
  tempo): **não há buffer de simulação** — nada a redimensionar; o slot morto
  é reaproveitado por construção (nasce de novo a cada `slots/taxa` s);
  **nenhuma ordenação** (2D sem profundidade; aditivo é comutativo e o
  "normal" desenha na ordem estável dos slots). CPU 0,02–0,03 ms em qualquer
  contagem (só o bloco de parâmetros).
- Quad indexado (4 execuções do vertex shader por partícula em vez de 6):
  −3% a −4% em 1M no export. O preview 0,5 metade dos slots já existia; a
  diferença de ±6% entre as colunas de preview está dentro do ruído do host.
- 1M partículas = ~2 ms na RTX 3050: não é gargalo no host; no celular (10–20×
  mais lento) o botão `particles` do AUTO é o que segura.

### 4. Optical flow (§80–82)

Vídeo sintético 1080p, velocidade 20% (quadro intermediário por movimento de
pixels) + desfoque vetorial, cache desligado (todo quadro calcula). ms de GPU:

| Modo | luma | pirâmide | LK (estimativa) | deformação (interpolação) | desfoque vetorial antes → depois | GPU total antes → depois |
|---|---|---|---|---|---|---|
| export | 0,081 | 0,023 | 0,116 | 0,117 | 0,281 → 0,281 | 1,863 → 1,868 |
| preview 1,0 | 0,082 | 0,023 | 0,118 | 0,116 | 0,268 → 0,269 | 1,838 → 1,833 |
| preview 0,5 | 0,065 | 0,017 | 0,052 | 0,114 | 0,266 → **0,152** | 1,744 → **1,630** |
| preview 0,25 | 0,031 | 0,013 | 0,026 | mistura (0,15) | 0,264 → **0,094** | 1,715 → **1,549** |

- Estimativa em pirâmide com base ≤ 384 px (export) e 192/96 px no preview 0,5
  / 0,25 (já existia; agora pelo botão `flow`). A deformação é na resolução do
  vídeo; no 0,25 o preview troca movimento por mistura (já existia). **Não há
  estágio de oclusão separado** no algoritmo (a deformação usa ponto fixo de 3
  passos) — não se inventou um.
- Novo: amostras do desfoque vetorial pelo botão (16/8/4): −43% e −65%.
- Cache: duas texturas por camada + **orçamento** (48 MB padrão,
  `set_flow_cache_budget`), LRU por camada, despejo contando a textura que vai
  entrar. Teste: 3 camadas com orçamento de 1,90 MB → residente 1,32 MB, 7
  despejos, flow continua calculando.

### 5. Tracking (§83)

Textura sintética movida (2,3; −1,7) px/quadro em 1080p, 12 pontos, 30
quadros; erro em px da resolução CHEIA.

| Análise | Erro médio | Erro máx | Passo NCC (12 pontos) | RGBA → cinza |
|---|---|---|---|---|
| 1080p | 0,66 | 2,21 | 23,1 ms | 3,56 ms |
| 720p | 1,45 | 7,17 | 23,2 ms | 1,63 ms |
| 360p | 1,76 | 7,46 | 22,3 ms | 0,36 ms |
| 240p | 2,93 | 8,14 | 22,1 ms | 0,16 ms |

- O rastreio de pontos do motor já analisa em miniatura de ≤ 360 px (e o
  rastreador de câmera numa altura reduzida própria): a conversão cai 10× e o
  decode/escala também; o erro sobe de 0,66 para 1,76 px (em 1080p) —
  suficiente para estabilizar/fixar camada. **Nenhuma mudança de código**.
- Achado: o passo do NCC custa o mesmo em qualquer resolução (~1,9 ms por
  ponto no host: janela e raio fixos em px, média da janela recalculada por
  candidato). É o próximo alvo se o rastreio de muitos pontos pesar.

### 6. Efeitos (§86–91)

Custo = mediana da soma dos passes do próprio efeito, 1080p, valores de
demonstração (os da prévia). Export = qualidade cheia; preview 0,5 = escala
pesada 0,5 na mesma resolução (a redução de resolução do preview é da 8C).

| Efeito | Chave | Passes antes→depois | GPU export antes | depois | GPU preview 0,5 antes | depois |
|---|---|---|---|---|---|---|
| Ordenar pixels | `aurea.stylize.pixel_sort` | 1→1 | 1,932 | 1,930 | 1,937 | 1,989 |
| Minimax | `aurea.stylize.minimax` | 1→1 | 1,358 | 1,361 | 1,358 | 1,360 |
| Raios de luz | `aurea.light.rays` | 1→1 | 1,230 | 1,235 | 1,247 | **0,649** |
| Desfoque gaussiano | `aurea.blur.gaussian` | 2→2 | 0,765 | 0,759 | 0,759 | 0,758 |
| Dano de JPEG | `aurea.stylize.jpeg_damage` | 1→1 | 0,446 | 0,441 | 0,442 | 0,441 |
| Desfoque de lente | `aurea.blur.lens` | 1→1 | 0,428 | 0,420 | 0,419 | **0,201** |
| Eco e rastro | `aurea.time.echo` | 1→1 | 0,380 | 0,380 | 0,380 | 0,380 |
| VHS | `aurea.glitch.vhs` | 1→1 | 0,355 | 0,357 | 0,358 | 0,357 |
| Brilho profundo | `aurea.light.deep_glow` | 6→5 | 0,858 | **0,345** | 0,840 | **0,345** |
| Máscara de nitidez | `aurea.blur.unsharp` | 3→3 | 0,285 | 0,290 | 0,284 | 0,289 |
| Turbulência | `aurea.distort.turbulence` | 1→1 | 0,286 | 0,287 | 0,288 | 0,287 |
| Brilho | `aurea.light.glow` | 5→5 | 0,256 | 0,255 | 0,256 | 0,253 |
| Meio-tom | `aurea.stylize.halftone` | 1→1 | 0,207 | 0,203 | 0,203 | 0,202 |
| VHS Fita | `aurea.glitch.uni_vhs` | 1→1 | 0,199 | 0,200 | 0,204 | 0,201 |
| Nitidez | `aurea.blur.sharpen` | 1→1 | 0,189 | 0,189 | 0,188 | 0,188 |
| Lente | `aurea.distort.warp` | 1→1 | 0,189 | 0,188 | 0,189 | 0,189 |
| HoloMatrix | `aurea.stylize.holomatrix` | 1→1 | 0,166 | 0,167 | 0,168 | 0,167 |
| Sinal | `aurea.glitch.signal` | 1→1 | 0,139 | 0,140 | 0,143 | 0,146 |
| Ondulação que dissolve | `aurea.distort.ripple_dissolve` | 1→1 | 0,135 | 0,139 | 0,135 | 0,136 |
| Dano de filme | `aurea.stylize.film_damage` | 1→1 | 0,132 | 0,133 | 0,136 | 0,132 |
| Níveis | `aurea.color.levels` | 1→1 | 0,132 | 0,130 | 0,131 | 0,131 |
| Motion Tile | `aurea.stylize.motion_tile` | 1→1 | 0,123 | 0,123 | 0,114 | 0,123 |
| Curvas | `aurea.color.curves` | 1→1 | 0,111 | 0,110 | 0,111 | 0,110 |
| Exposição / Brilho e contraste / Saturação / Tingir / Matriz de cor | `aurea.color.*` | 1→1 | 0,103–0,104 | 0,103–0,104 | 0,103–0,104 | 0,103–0,104 |
| Faixa de luz | `aurea.light.sweep` | 1→1 | 0,106 | 0,103 | 0,112 | 0,105 |
| Glitchify | `aurea.glitch.glitchify` | 1→1 | 0,099 | 0,100 | 0,103 | 0,105 |
| Glitch em cruz | `aurea.glitch.cross` | 1→1 | 0,099 | 0,100 | 0,103 | 0,105 |
| Grão | `aurea.stylize.grain` | **0→1** | 0 (não desenhava) | 0,098 | 0 | 0,101 |
| Colorama | `aurea.color.colorama` | **0→1** | 0 (não desenhava) | 0,098 | 0 | 0,101 |
| Varredura | `aurea.stylize.scanlines` | **0→1** | 0 (não desenhava) | 0,079 | 0 | 0,079 |
| Chave de croma | `aurea.key.chroma` | 1→1 | 0,098 | 0,094 | 0,096 | 0,100 |
| Chave de luma | `aurea.key.luma` | 1→1 | 0,086 | 0,103 | 0,090 | 0,091 |
| Tremor | `aurea.distort.shake` | 1→1 | 0,078 | 0,081 | 0,078 | 0,078 |
| Onda | `aurea.distort.wave_warp` | 1→1 | 0,080 | 0,080 | 0,079 | 0,080 |
| Inverter | `aurea.color.invert` | 1→1 | 0,079 | 0,079 | 0,079 | 0,079 |
| Transformar | `aurea.transform` | 0→0 | 0 | 0 | 0 | 0 (entra na matriz da composição) |
| Posterizar tempo / RGB no tempo | `aurea.time.*` | 0→0 | 0 | 0 | 0 | 0 (temporal: quadro parado não tem outro instante) |
| Controles de expressão (5) | `aurea.control.*` | 0→0 | 0 | 0 | 0 | 0 (não desenham) |

CPU por efeito: 0,02–0,06 ms (planejamento e gravação).

- **Brilho profundo com pirâmide**: o halo já descia a pirâmide; o que pesava
  era o NÚCLEO borrado em resolução cheia (blur H+V = 0,55 de 0,86 ms). Agora
  núcleo com raio ≥ 12 texels vive em 1/2 e, quando núcleo e halo caem na
  mesma redução, o limiar (imagem clara) é um só: 6 → 5 passes, **0,858 →
  0,345 ms (−60%)**, export incluído.
- **Desfoque de lente** (anéis do disco) e **Raios** (amostras no raio) com
  variante de preview pelo botão `effects`: −52% e −48% no preview 0,5; export
  intocado.
- **VHS, VHS Fita, Glitchify, Glitch em cruz, Sinal, Dano de filme/JPEG e
  Grão**: procedurais na GPU, um passe cada, hash no shader; CPU só monta os
  uniforms (0,02–0,03 ms). Nada de ruído gerado na CPU nem textura de ruído
  subida por quadro.
- **P1 corrigido (mesmo achado da 8A)**: Grão, Varredura e Colorama eram
  `PerPixel` sem `color_op` e o EffectGraph os descartava como identidade —
  **não desenhavam na timeline nem no export** (só na prévia do catálogo). Agora
  `Neighborhood` (passe próprio). O teste de regressão ficou na 8A
  (`Gpu.EveryCatalogEffectChangesTheProjectFrame`); o benchmark A/B desta
  frente também acusa na base (3 efeitos sem mudar o quadro).
- Os caros que sobram (Ordenar pixels 1,9 ms, Minimax 1,4 ms, Raios 1,2 ms)
  não têm variante de preview: reduzir amostras mudaria o desenho de forma
  visível; o que os barateia no preview é a resolução 1/2–1/4 da 8C (custo
  proporcional à área).

**Golden/diff do export** (saída de EXPORT de cada um dos 47 efeitos, base ×
depois, 8 bits sRGB), 480×270: **43 efeitos idênticos** (diferença máxima 0);
Grão, Varredura e Colorama mudam porque agora desenham (a base não aplicava
nada); Desfoque de lente, Raios, Brilho e Faixa de luz também idênticos em
1080p. **Brilho profundo**: diferença média **0,084/255** em 1080p (0,047 em
480×270); **0,45% dos canais** passam de 3/255 — todos nas curvas de nível
duras do "estouro" (o limiar recorta o brilho e a borda anda alguns px com o
núcleo em 1/2); imagem visualmente igual (conferida lado a lado). Critério do
benchmark: o do golden (máx. 3, média 0,35) ou média ≤ 0,35 com ≤ 2% dos
canais acima de 3.

### Testes (0 falhas, 455 testes no host)

`tests/test_heavy.cpp` (novo): `AnimatedTextPlaybackNeverTouchesTheAtlasInSteadyState`,
`FallbackFontsLoadOneAtATime`, `StaticVectorAndMaskAreBuiltOnceWhileTheLayerMoves`,
`VectorMorphHashIsStableOutsideTheKeyRange`, `Scene3DInstancingCullingAndShadowKnob`,
`FlowCacheStaysWithinItsBudget`, `QualityLadderAndExportIsAlwaysFull` e os
benchmarks `Bench*` (só com `AUREA_BENCH=1`). `TestFramework.hpp`: registro
de 512 → 1024 testes (acima do limite os testes eram descartados em silêncio;
com as 9 frentes somando testes, 512 estourava).

### Não feito / limites declarados

- Nenhum número de celular (sem aparelho). Os ganhos de chamadas de desenho
  (instancing) e de partículas precisam do Android real para virar ms.
- Streaming de mips 3D; soltar as texturas decodificadas do `SceneAsset`
  depois do upload (80 MB no capacete) — recomendação para a 8B.
- Upload parcial do atlas de glifos ao digitar (4 MB por glifo novo) — precisa
  de API de sub-região no backend.
- Deduplicar o MESMO arquivo importado duas vezes (hoje vira dois assets) —
  é do import no Engine.
- HUD: os contadores estão em `Renderer::heavy_stats()`; mostrar é da 8A.
