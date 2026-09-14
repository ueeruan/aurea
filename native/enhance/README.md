# libaurea_enhance — super-resolução por IA (Real-ESRGAN sobre ncnn)

Motor próprio em C++ (não usa o `realesrgan.cpp` do upstream): tiles na CPU
com margem de contexto de 10 px, inferência ncnn com Vulkan (fp16) quando
há GPU, clamp em ponto flutuante antes de arredondar e erro propagado
(falta de memória vira erro, não quadro preto).

Modelo: `assets/ai/realesr-animevideov3/x4.{param,bin}` (ver o README de lá).

## Android
O Gradle já constrói: `native/CMakeLists.txt` inclui este diretório quando
`ANDROID`. Na primeira configuração o CMake baixa o ncnn oficial
`ncnn-20260526-android-vulkan.zip` e confere o SHA-256
(`26909c92eed35afed4a966b5e9e503fcb0a529691ea3f910ec2c94a4fff52804`).
Sem internet: `-DAUREA_NCNN_DIR=<ncnn>/<ABI>`.

Verificado aqui: compila e linka para arm64-v8a com NDK 28.2 e exporta
`ae_create/ae_destroy/ae_info_get/ae_set_tile/ae_process/ae_resize`.
NÃO verificado em aparelho.

## Host (testes)
Pacote `ncnn-20260526-windows-vs2022.zip` (x64, Vulkan). Um CMake com
`set(AUREA_NCNN_DIR <ncnn>/x64)` + `add_subdirectory(native/enhance)` gera
`aurea_enhance.dll`. Depois:

```
teste_host <x4.param> <x4.bin> <entrada 320x180.png> <original 1280x720.png> <saida.png>
AUREA_ENHANCE_LIB=<caminho>\aurea_enhance.dll flutter test test/aprimoramento_motor_nativo_test.dart
```

Medido no host (NVIDIA RTX 3050, Windows, fp16, tile automático 200):
x4 com entrada 320x180 = p50 28-32 ms; tile 32 contra 200 = 57,7 dB;
intensidade 0 igual ao bicúbico; cancelamento devolve -3.

# RIFE — câmera lenta com IA na exportação (mesma biblioteca)

`aurea_rife.cpp` gera o quadro do instante t entre dois quadros reais com o
RIFE v4.6 (`rife/`, cópia corrigida do rife-ncnn-vulkan; ver
`rife/PATCHES.md`). Divide o ncnn estático e a instância Vulkan com o
aprimoramento (`aurea_gpu.h`): uma segunda biblioteca duplicaria ~16 MB e
destruiria a instância da outra.

Modelo: `assets/ai/rife-v4.6/flownet.{param,bin}`, empacotado só no Android
(`platforms: [android]` no pubspec). Ver `assets/ai/README.md`.

API C: `ar_create(pasta, use_gpu)`, `ar_interpolate` (RGB24) e
`ar_interpolate_png` (PNG para PNG, escrita atômica, guarda os dois últimos
quadros lidos). Erros: `-1` argumentos, `-2` inferência (memória da GPU),
`-4` modelo, `-5` arquivo.

No app (`lib/src/features/export/application/interpolacao_rife.dart`): um
clipe lento com interpolação "movimento" é lido na taxa da própria fonte e
o RIFE preenche os quadros do meio num isolate, com os nomes que o FFmpeg
escreveria. Fonte com quadros suficientes (60/120 fps) usa só os quadros
reais. Sem GPU, com pouca memória de GPU (2x o medido no host) ou em
qualquer falha, a exportação volta para o `minterpolate` do FFmpeg.

Host:

```
AUREA_ENHANCE_LIB=<caminho>\aurea_enhance.dll flutter test test/rife_motor_nativo_test.dart
AUREA_RIFE_GPU=1 AUREA_ENHANCE_LIB=<caminho>\aurea_enhance.dll flutter test test/rife_motor_nativo_test.dart
```

# Aprimoramento na exportação do editor

`ae_process_png(motor, entrada, saída, escala, força, largura, altura)`
lê um PNG, roda a rede na escala 1/2/4, leva ao encaixe da composição
(Catmull-Rom com o núcleo alargado ao reduzir) e grava com troca
atômica. Os PNGs passam por `aurea_png.cpp` (stb com ligação interna;
zlib do sistema no Android) e são o mesmo módulo do RIFE.

No app, o clipe de vídeo tem a porta "Aprimorar com IA" (liga,
intensidade e antes/depois de um quadro). A exportação lê esse clipe na
resolução da própria fonte (teto de 960x540, sem ampliar; a conta é do
FFmpeg depois de girar), aplica a câmera lenta se houver e só então a
rede, quadro a quadro, num isolate. Vídeo que já tem a resolução da
composição não passa pela rede. Sem o motor, o clipe sai sem o efeito e
a tela final diz isso; se a rede falhar, a exportação para com o nome do
clipe.

Host:

```
AUREA_ENHANCE_LIB=<caminho>\aurea_enhance.dll flutter test test/aprimoramento_export_nativo_test.dart
```
