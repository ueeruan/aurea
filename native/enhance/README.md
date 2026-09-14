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
