# AUREA FX V2 — cobertura e verificação

Versão 1.2.0 (35). Implementação própria; não inclui código Adobe/Cycore nem
plugins comerciais. Não é uma certificação de equivalência ao After Effects.

## O que mudou

30 efeitos do catálogo usam novos kernels de fragmento, compartilhados pela
prévia e pela exportação: Levels, Posterize, Curves, Vibrance, Corrections,
White Balance, Color Wheels, Unmult, Tint, Vignette, Mosaic, Film Grain,
Fractal Noise, Turbulent Displace, Radial Blur, Directional Blur, Unsharp Mask,
Light Rays, Zoom Warp, Gradient 4, RGB Split, Chromatic Aberration, Bend,
Split, Digital Damage, Glitchify, VHS, Film Damage, Flicker e Glitch.

Glow e Deep Glow mantêm a pirâmide multiescala, mas receberam extração por
luminância/crominância, joelho suave, tintas distintas e resposta tonal por
pixel. Tonemapping e Lens Dirt agora são consumidos. Lens Dirt é um padrão
procedural determinístico, não uma fotografia carregada. A resposta tonal é
aplicada à fonte antes do blur em SDR, não a um buffer HDR.

Os 11 demais operadores foram mantidos nos caminhos especializados existentes:
Gaussian Blur, Shake, Echo, Space Echo, Time Remap, Force Motion Blur,
Motion Tile, Seed/Scatterize, Pixel Sort, Blob Tracker e Liquid Glass.
Portanto **não houve reescrita completa dos 43 efeitos**.

## Contrato técnico

- IDs e unidades persistidos não mudam; keyframes são avaliados no tempo local.
- Ordem dos efeitos é a ordem da composição; bypass não avalia o efeito.
- RGB é desmultiplicado antes de operações de cor e premultiplicado na saída.
- Posterize quantiza canais em tons reais. Levels usa gama não linear.
- Ruídos e pulsos usam tempo/semente explícitos, sem relógio de parede.
- Amostras de blur são normalizadas para não escurecer uma imagem constante.
- Impeller usa filtros nativos; Skia usa snapshots. Falha de carregamento do
  shader mantém o caminho anterior disponível.
- O shader é carregado antes de abrir o projeto; a exportação também aguarda.

## Limites conhecidos

Pipeline SDR, sem equivalência ao espaço de cor, HDR/32 bpc, plugins e todos
os controles de AE. Curves continua sendo uma curva tonal simplificada,
não uma spline livre. Os operadores temporais têm semântica própria.
Algumas distorções ficam limitadas aos limites da textura de entrada.
Snapshots em backends sem filtro nativo têm limitações com vídeo ao vivo.
Projetos antigos abrem, mas os algoritmos novos podem alterar o visual.

O After Effects instalado foi localizado, mas a tentativa de criar uma
referência sintética por script não produziu o AEP; não há render pareado de
AE validado nesta rodada. Testes internos **não comprovam igualdade com AE**.
Também não substituem teste em iPhone/Android físico.

## Verificação reproduzível

`flutter test --no-pub --concurrency=2`

`flutter test --no-pub test/pixel_effect_engine_test.dart --dart-define=EFFECT_SCREENSHOTS=true`

Os testes exercitam o shader real: tons de Posterize, referência numérica de
Levels/tonemapping, alfa, neutros, determinismo, parâmetros do catálogo,
ordem e keyframes. A regressão de Glow renderiza o CompositionView com FX V2
ativo. A ajuda testa busca, navegação de volta e tela de 375 × 667.
Fixtures visuais ficam em `build/qa/effects-v2/`; são amostras sintéticas,
não comparações com Adobe. Contato visual revisado antes do empacotamento.

Ajuda offline: **Sobre → Como usar o AUREA** ou **?** no painel de efeitos.
Seis passos iniciais, receita de texto animado, cuidados de desempenho e
referência pesquisável dos 43 efeitos.

## Fontes primárias consultadas

- [Adobe: Stylize, Glow, Mosaic, Posterize e Motion Tile](https://helpx.adobe.com/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/stylize-effects.html)
- [Adobe: Blur & Sharpen](https://helpx.adobe.com/uk/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/blur-sharpen-effects.html)
- [Adobe: efeitos temporais](https://helpx.adobe.com/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/time-effects.html)
- [Flutter: fragment shaders e contrato ImageFilter](https://docs.flutter.dev/ui/design/graphics/fragment-shaders)

Publicação autorizada no repositório privado. Primeiro compilar, verificar e
entregar o IPA sem assinatura; só depois iniciar o APK. Gradle limitado a
dois workers e heap de 3 GB. Assinatura Android de desenvolvimento, não Play.
