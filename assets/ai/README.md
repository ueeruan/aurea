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

# Modelo de interpolação de quadros (câmera lenta)

Modelo: RIFE v4.6, formato ncnn, só `flownet` (a v4 não usa contextnet nem
fusionnet). Arquivos: `rife-v4.6/flownet.param` e `rife-v4.6/flownet.bin`.
Origem: pasta `models/rife-v4.6` do rife-ncnn-vulkan, commit
`a7532fc3f9f8f008cd6eecd6f2ffe2a9698e0cf7`
(https://github.com/nihui/rife-ncnn-vulkan).

SHA-256:
- flownet.param 28df14d57a225725ee5386f52eba422488450d37c9f40800ed4f62e8ba846692
- flownet.bin f334ed2260149ce0188a6dcf049844e8b0cdd912e01cbcfb63553157d2508958

Licenças: RIFE (Practical-RIFE, hzwer) MIT; rife-ncnn-vulkan (nihui) MIT.

Empacotado só no Android (`platforms: [android]` no pubspec): o motor
(`native/enhance/aurea_rife.cpp`) não existe no iOS.

Contrato verificado executando no ncnn 20260526: entradas `in0`/`in1`
(RGB 0..1, lados múltiplos de 32) e `in2` (instante t), saída `out0`.
No host, RIFE ganhou da mistura por 3,2 a 3,9 dB em vídeo real (720p e
1080p) e acertou o deslocamento em janelas sintéticas.
