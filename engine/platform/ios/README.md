# Aurea — target iOS

A casca nativa do iOS: host SwiftUI, ponte ObjC++ ↔ C++, VideoToolbox e o
projeto Xcode. **O núcleo é o mesmo do Android** — este diretório não compila um
segundo motor, ele compila o motor (`engine/`) e a ponte.

```
engine/platform/ios/
  CMakeLists.txt          toolchain/opções iOS; o motor entra como subdiretório
  ios_toolchain.cmake     CMAKE_SYSTEM_NAME=iOS, arm64 / simulador
  bridge/AureaBridge.h    C++ puro: Host (dono do Engine) + fábricas de plataforma
  bridge/AureaBridge.mm   ciclo de vida, superfície, log, batch de comandos
  bridge/AureaEngine.h    superfície ObjC (é o que o Swift vê)
  bridge/AureaEngine.mm   a tradução: tipos e chamadas, nenhuma regra de edição
  bridge/IOSSurfaceView.mm  UIView com CAMetalLayer + CADisplayLink
  bridge/IOSVideoDecoder.mm VideoToolbox (decode/encode) + ImageIO + sonda do aparelho
  bridge/IOSAudio.mm      AVAudioEngine sobre o Audio Engine do núcleo
  app/                    SwiftUI: Home, editor, preview, timeline, painéis, export
  Aurea.xcodeproj/        o alvo do app (14 Swift + 5 .mm + 14 frameworks)
  verify/                 as auditorias desta entrega (rodam sem Mac)
```

## Como compilar (num Mac)

Pré-requisitos: Xcode 15 ou 16, CMake ≥ 3.22, e um `glslc` (o motor compila os
shaders GLSL → SPIR-V no build). O `glslc` vem no NDK (`shader-tools/darwin-*`)
ou no Vulkan SDK:

```bash
cd engine/platform/ios
cmake -S . -B build/iphoneos \
      -DCMAKE_TOOLCHAIN_FILE=ios_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DAUREA_GLSLC=/caminho/do/glslc
cmake --build build/iphoneos --parallel
```

Isso produz `libaurea_core.a`, `libaurea_thirdparty.a`, `libaurea_metal.a` e
`libaurea_ios.a`. Depois, o app:

```bash
open Aurea.xcodeproj      # escolha o destino e dê Run
```

O alvo do Xcode tem uma fase de script ("Build Aurea core (CMake)") que roda os
dois comandos acima em `build/core` — é ela que faz o `libaurea_ios.a` existir,
e por isso o app compila sem o passo manual.

**Simulador:** `-DAUREA_IOS_SIMULATOR=ON` (o script do Xcode já passa quando o
destino é o simulador).

## O que falta para compilar hoje

1. **Os shaders MSL.** O backend Metal (`engine/gpu/metal/`) consome um blob
   **MSL** (`msl_glue.md`: cabeçalho `AUREAMSL` de 32 bytes + texto ou
   `.metallib`), e não SPIR-V. O motor embute SPIR-V hoje, então falta a
   tradução no build — que vive em `engine/cmake/AureaShaders.cmake` (fora deste
   diretório: o iOS consome o motor, não o altera). O ponto de extensão daqui é
   `-DAUREA_METAL_SHADERS=ON` + `-DAUREA_SPIRV_CROSS=<binário>` +
   `-DAUREA_METAL_SHADER_SCRIPT=<script cmake>`: o script recebe
   `AUREA_METAL_SPV_DIR` (os `.spv` já gerados), `AUREA_METAL_OUT_DIR` e o
   binário, e o formato do blob que ele precisa produzir está no `msl_glue.md`.
   Sem esse passo o app sobe, o preview não abre pipeline — e o log diz por quê.
2. **Assinatura (signing)** — `CODE_SIGN_IDENTITY` e o time estão VAZIOS de
   propósito: quem assina é o dono, com a conta dele, no Xcode
   (Targets › Aurea › Signing & Capabilities). O `Aurea.entitlements` também
   está vazio: o app não usa nenhuma capability.
3. **`glslc`** no PATH/`-DAUREA_GLSLC` (ver acima) — o motor compila GLSL →
   SPIR-V no build. O `spirv-cross` entra no item 1.
4. **Uma linha no `engine/CMakeLists.txt`**, se o integrador quiser o backend
   Metal no build do motor: `add_subdirectory(gpu/metal)` (é o que o contrato do
   backend pede). Este CMakeLists se vira sem ela — ele adiciona o diretório se
   o alvo `aurea_metal` ainda não existir, e não cria de novo se já existir.
5. Nada além disso. Não há dependência de terceiro para instalar: o único
   "pacote" que o núcleo usa está em `engine/third_party/` (já no repositório).

## O que foi ligado ao núcleo (nenhum caminho paralelo)

| Assunto | Interface do núcleo | Implementação iOS |
|---|---|---|
| Decode de vídeo | `aurea::VideoDecoderBackend` (`media/VideoSource.hpp`) | `IOSVideoDecoder.mm` — AVAssetReader + `VTDecompressionSession` |
| Sonda de mídia | `aurea::VideoSourceFactory` (`media/MediaManager.hpp`) | idem (`probe`/`open_video`/`open_audio`) |
| Decode de áudio | `aurea::audio::AudioDecoderBackend` (`audio/Audio.hpp`) | idem — PCM float no formato do arquivo |
| Export | `aurea::ExportSink` (`export/ExportSink.hpp`) | idem — `VTCompressionSession` + `AVAssetWriter` |
| Áudio na saída | `aurea::audio::AudioOutput` (`audio/Audio.hpp`) | `IOSAudio.mm` — `AVAudioEngine` + `AVAudioSourceNode` |
| Preview | `SurfaceDesc::nativeWindow` (`render/GPUBackend.hpp`) | `IOSSurfaceView.mm` — `CAMetalLayer*` |
| Comandos | `Command` / `CommandQueue` (`command/Command.hpp`) | `AureaEngine.mm` + `ios::Batch` |
| Projeto `.aurea` | `project/Serialization.cpp` | o MESMO arquivo, sem formato próprio |

**Zero-copy de vídeo:** o `CVPixelBuffer` sai do VideoToolbox com
`kCVPixelBufferMetalCompatibilityKey = true` e `kCVPixelBufferIOSurfacePropertiesKey`
vazio (IOSurface sem propriedades) e vai para `import_external_image` como
`ExternalImageDesc::nativeHandle`. Nenhum plano passa pela CPU.

**BGRA e não NV12, no conteúdo de 8 bits:** o backend amostra
`MTLPixelFormat420YpCbCr8BiPlanar*`, mas a conversão embutida nesse formato usa
sempre a matriz BT.601 — vídeo BT.709 (o padrão de celular) sairia com a cor
deslocada. Pedindo `kCVPixelFormatType_32BGRA`, quem converte é o VideoToolbox,
com a matriz DO ARQUIVO, e o backend importa BGRA direto (`MetalResources.mm`).
Continua zero-copy e a cor fica exata. O 10 bits (PQ/HLG) segue em YCbCr
biplanar `x420`, para não jogar fora a profundidade.

**Áudio:** não há mixer novo. O `AudioEngine` do núcleo (o mesmo que o export
usa) produz os quadros e o `AVAudioSourceNode` só os entrega ao hardware.

## Identificadores

`com.aurea.aurea` — o MESMO `applicationId` de `android/app/build.gradle.kts`,
conferido automaticamente por `verify/check_scope.py`. `CFBundleDisplayName` =
Aurea; `CFBundleVersion` = 2102 (o `versionCode` do Android).

**Permissões:** nenhuma. O Android não pede `CAMERA` nem `RECORD_AUDIO` (não há
captura no editor) e a mídia entra pelo seletor do sistema, que no iOS não pede
permissão — por isso NÃO há `NSMicrophoneUsageDescription` nem
`NSPhotoLibraryUsageDescription`. Declarar permissão sem uso é promessa falsa
(o mesmo critério que tirou as permissões do manifest).

## Piso de iOS: 16.0

O backend Metal caberia em 14 (`MTLBinaryArchive` é iOS 14). A UI não: as folhas
contextuais do editor usam `presentationDetents` e a navegação usa
`NavigationStack`, ambos iOS 16. Trocar o piso custaria reescrever os painéis.

## O que ainda NÃO tem tela no iOS

A ponte já expõe as operações (elas existem no motor e estão na `AureaEngine.h`),
mas as telas abaixo ainda não foram escritas em SwiftUI. A prioridade desta
entrega foi Home, editor, preview, timeline, transporte, efeitos, transform,
3D e export:

texto (conteúdo/fonte/estilo/animadores), máscaras e track matte, legendas,
Aurea Particular (painel), presets, expressões, vetorial/forma (edição de
pontos), rastreio de ponto e de máscara, câmera 3D, velocidade/áudio por
camada, marcas e batidas, curvas e o painel DEV de performance.

Cada uma é uma tela SwiftUI que chama os métodos que já estão na ponte — nenhum
trabalho de C++ é necessário para elas.

## Idiomas

O catálogo do Android tem SETE (`values/` = pt-BR, + ar, en, es, hi, id, ru). O
iOS traz **pt-BR e inglês**, gerados do MESMO XML por `tools/_ios_strings.py`
(`app/AureaStrings.swift`, chaves idênticas: `home_tab_projects`,
`editor_desfazer`…). Trazer os outros cinco é rodar a mesma extração com os
outros `values-*`; nenhuma tela precisa mudar. O idioma padrão segue o sistema e
pode ser trocado em Ajustes.

Os textos do painel "Este aparelho" (números do `DeviceReport`) são literais em
português — igual ao Android, que os monta em `DeviceReport.kt` fora do catálogo.

## Shaders MSL

Ver o item 1 de "O que falta". O contrato do blob que o backend consome está em
`engine/gpu/metal/msl_glue.md` (magic `AUREAMSL`, versão, `threadgroup` para
compute, entradas `vs_main`/`fs_main`/`cs_main` e os índices de recurso que o
SPIRV-Cross precisa fixar). Ligar `AUREA_METAL_SHADERS` sem o script ou sem o
`spirv-cross` falha no CONFIGURE, com a mensagem dizendo o que falta — nunca um
build pela metade.

## Auditorias (rodam no Windows, não precisam de Mac)

```bash
python engine/platform/ios/verify/check_symbols.py    # todo símbolo do núcleo existe
python engine/platform/ios/verify/check_pbxproj.py    # pbxproj: fileRefs, alvo, bundle id
python engine/platform/ios/verify/check_api_swift.py  # model.* / engine.* / chaves de texto
python engine/platform/ios/verify/check_scope.py      # imports, escopo, bundle id
```

Resultado desta entrega: **298/298 símbolos**, **19/19 arquivos referenciados no
pbxproj**, **0 problemas** nas quatro. As auditorias foram validadas ao
contrário: injetar um método inventado ou um arquivo inexistente faz cada uma
delas falhar.
