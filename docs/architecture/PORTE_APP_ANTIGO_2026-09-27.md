# Porte do editor antigo — inventário e o que entrou (2026-09-27)

Extração do APK do editor antigo do dono, comparação com o catálogo do Aurea e
porte do que faltava. O APK fica em `build/legado/` (fora do versionamento) e o
nome do app antigo, o do desenvolvedor e os do pacote **não aparecem em lugar
nenhum** deste repositório — código, comentários, strings, nomes de arquivo,
testes, docs ou mensagens de commit.

## 1. Como o inventário foi feito

O APK é Kotlin + Compose ofuscado com R8 e traz os shaders como literais no
`classes.dex`. O caminho foi:

1. `build/legado/dexstrings.py` — tabela de strings do `classes.dex` (30.037);
2. `build/legado/dexscan.py` — decodifica as `code_item` de cada classe e lista
   as strings do CÓDIGO na ordem em que aparecem. É o que revela o catálogo: o
   id de um efeito vem logo antes do rótulo e dos parâmetros dele;
3. `build/legado/shaders/*.glsl` — as 79 fontes GLSL embutidas, agrupadas por
   classe depois do passo 2.

O catálogo de efeitos aparece inteiro numa classe só (a lista do menu "Adicionar
efeito"), e cada efeito que desenha tem a sua classe com o GLSL, os ids e os
rótulos dos parâmetros.

## 2. O que o editor antigo tem e o Aurea já tinha

Nem tudo era lacuna. Já estavam no Aurea, com o mesmo desenho (a maioria veio de
portes anteriores):

| Grupo | Efeitos |
|---|---|
| Cor | brilho/contraste, matiz e saturação, exposição+gama, níveis, curvas |
| Desfoque | gaussiano, de lente, radial |
| Distorção | onda, turbulência, lente, shake/agitar |
| Luz | brilho (glow), CC Light Sweep, raios radiais |
| Estilizar | vinheta, mosaico/painel de LED, meio-tom (com ganho do ponto e papel), detectar bordas |
| Gerar | ruído fractal, degradê, degradê de 4 cores, espectro de áudio |
| Recorte | chave de croma (+ tela verde/azul, derramamento), chave de luma |
| Padrões | grid, listras, raios radiais, motion tile |
| Transição | varredura linear, varredura radial |
| Movimento | oscilar, balançar, agitar (wiggle) |
| Partículas | o sistema "Particular" (emissor, física, rastro, colisão) |
| Tempo | remapear, posterizar, eco |

O que **não** era efeito e também já existia: expressões com API de cena
(`parentOf`, `boundsOf`, `pivotOf`, `layer`), marcadores, volume de áudio,
paleta de cores salvas, LUT de curvas.

## 3. O que faltava — e entrou

Oito efeitos e quatro modos de mesclagem. Todos nativos: chave estável, fonte em
`engine/src/effects/builtin/`, shader próprio, ficha em Kotlin e Swift e texto
nos sete idiomas.

| Efeito | Chave | Shader | Onde |
|---|---|---|---|
| Preencher | `aurea.color.fill` | (op de cor, fundida) | ColorEffects.cpp |
| Equilíbrio de cor (HLS) | `aurea.color.balance_hls` | (op de cor, fundida) | ColorEffects.cpp |
| Desfoque de zoom | `aurea.blur.zoom` | `zoom_blur.frag` | BlurEffects.cpp |
| Bojo | `aurea.distort.bulge` | `bulge.frag` | DistortEffects.cpp |
| Xadrez | `aurea.pattern.checkerboard` | `checkerboard.frag` | PatternEffects.cpp |
| Matriz hexagonal | `aurea.pattern.hexagonal` | `hexagonal_array.frag` | PatternEffects.cpp |
| Sombra projetada | `aurea.stylize.drop_shadow` | `drop_shadow_blur.frag` + `drop_shadow_combine.frag` | FinishingEffects.cpp |
| Borda | `aurea.stylize.border` | `border_dilate.frag` + `border_combine.frag` | FinishingEffects.cpp |

Modos de mesclagem novos em `BlendMode` (Types.hpp), no fim do enum para não
mexer no valor gravado dos antigos: **Divide 18, Vivid Light 19, Linear Dodge 20,
Linear Burn 21**.

### 3.1 O que mudou no porte (e por quê)

O porte não é cópia. O editor antigo trabalha em sRGB com alfa pré-multiplicado
e sem correção de aspecto; o Aurea trabalha em **linear** e mede em pixels da
camada. As diferenças que valem registro:

- **Sombra projetada em dois eixos de verdade.** A versão antiga borrava pela
  direção no primeiro passe e SEMPRE na vertical no segundo, então uma sombra na
  diagonal saía com o comprimento errado. Aqui o segundo eixo é a perpendicular
  da direção, e o comprimento é o mesmo em qualquer ângulo.
- **Passos em pixels da camada.** O passo e o deslocamento viajam em pixels e o
  shader os converte para uv com a densidade que o C++ manda (`p3`), de modo que
  o mesmo número desloca o mesmo tanto na horizontal e na vertical, e o mesmo
  tanto no preview reduzido e no export.
- **Bojo com as duas armadilhas fechadas.** O `pow(0, e)` indefinido do GLSL ES
  (que devolvia NaN e sorteava um texel no meio do quadro) e o colapso de `r` em
  meia precisão perto do centro. As duas correções estão comentadas no shader.
- **O antigo Xadrez usava uma rede hexagonal** para desenhar as células. O porte
  usa a grade retangular de `Cell Width` × `Cell Height`, que é o que o rótulo
  dele promete, e a matriz hexagonal ficou com um efeito próprio.
- **Mosaico/LED e Meio-tom já cobriam** o que o antigo chamava de *pixelate*,
  *Dot Matrix*, *LED Wall* e *CMYK Color* (o meio-tom do Aurea já tem ganho do
  ponto, papel e cor do papel). Não foram duplicados.

## 4. Verificação

- `aurea_tests` no host: catálogo com **103 efeitos**, ids estáveis conferidos
  pelo `I18n.EffectCatalogHasStableIdsForEveryLabel`, parâmetros com valores
  absurdos sem crash, e `Gpu.EveryCatalogEffectChangesTheProjectFrame` exigindo
  que todo efeito do catálogo mude de fato o quadro (a Sombra e a Borda rodam
  com a camada menor que o quadro e fundo claro — com a imagem cobrindo tudo
  não sobra lugar onde elas desenhem).
- `Gpu.EveryBlendModeMatchesTheFormulaPixelForPixel`: 22 modos contra a
  implementação de referência do W3C, com tolerância de 3/255.
- `tools/i18n_check.py`: 0 problemas nos sete catálogos.
- `tools/_ios_strings.py`: `AureaStrings.swift` regerado (1.964 chaves).
- `./gradlew :app:compileDebugKotlin`: compila.
- Checagens estáticas do iOS: `check_pbxproj`, `check_api_swift`,
  `check_shared_assets`, `check_effect_contract`, `check_symbols` — 0 problemas.
  O build nativo e o simulador só rodam no CI.
