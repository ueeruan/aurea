# Modelo de aprimoramento por IA

Modelo: realesr-animevideov3, escala nativa x4, formato ncnn.
Arquivos: `realesr-animevideov3/x4.param` e `realesr-animevideov3/x4.bin`.
Origem: pacote oficial do Real-ESRGAN-ncnn-vulkan
https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip
(SHA-256 do zip abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d).

SHA-256:
- x4.param 850a248e7c14c27e5bd8cf7265113a9441036a7db63963bb8aa5169d788a435e
- x4.bin 548a36f9c3f4ab8da56cd3b13badf23968bee207b396dad14d04b830e5f2ab2d

Licenca: Real-ESRGAN, BSD-3-Clause (Xintao Wang). ncnn: BSD-3-Clause (Tencent).

Contrato verificado executando no ncnn 20260526: entrada `data` (RGB 0..1),
saida `output` (x4, RGB 0..1). Motor proprio em `native/enhance`.

Por que este modelo: medido no host (RTX 3050), foi o unico com custo de
video (16-30 ms a 320x180), emendas de tile invisiveis e pouco flicker
(+11% sobre o bicubico). O RealESRGAN_x4plus custou 30x mais, inventou
textura e dobrou o flicker. O realesr-general-x4v3 (indicado para video
real) nao tem versao ncnn oficial e ainda nao foi convertido nem validado.

Nao ha modelos, marcas ou presets da Topaz ou da Adobe.
