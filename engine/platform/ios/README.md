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

## Shaders e compilação contínua

O workflow `.github/workflows/build-ipa.yml` compila no macOS com Xcode e
empacota `aurea-beta2-unsigned.ipa` como artefato `aurea-ipa`. É o mesmo fluxo
sem assinatura do projeto antigo: o instalador de sideload assina o IPA.
O workflow não publica no TestFlight.

Os shaders são compilados por `glslc` e traduzidos para Metal pela ferramenta
`engine/tools/metal-shaders`, com SPIRV-Cross fixado em um commit. Ela preserva
os bindings do motor, push constants no buffer 30 e os tamanhos dos grupos de
compute no cabeçalho AUREAMSL. O CMake embute esses blobs no lugar do SPIR-V.

Para compilar localmente num Mac:

```bash
brew install cmake ninja shaderc ccache
cmake -S engine/tools/metal-shaders -B build/metal-tools -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/metal-tools --parallel 3
export AUREA_METAL_COMPILER="$PWD/build/metal-tools/aurea-metal-compiler"
xcodebuild -project engine/platform/ios/Aurea.xcodeproj -scheme Aurea \
  -configuration Release -sdk iphoneos -destination 'generic/platform=iOS' \
  -derivedDataPath build/ios CODE_SIGNING_ALLOWED=NO build
```

A biblioteca `aurea_ios` compila a ponte ObjC++ pelo CMake; o target do Xcode
compila somente as telas Swift. Não compilar a ponte duas vezes. A assinatura
para distribuição oficial exige configurar a conta Apple no Xcode.

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

**Vídeo de 8 bits:** o VideoToolbox entrega BGRA; o backend importa esse buffer
como textura Metal sem cópia. **10 bits (PQ/HLG):** o buffer P010 é bloqueado
para leitura durante a vida do frame e seus dois planos passam pelos shaders
YUV do motor. Metal público não oferece um formato único de textura NV12/P010
com conversão implícita; não usamos identificadores inexistentes de formato.

**Áudio:** não há mixer novo. O `AudioEngine` do núcleo (o mesmo que o export
usa) produz os quadros e o `AVAudioSourceNode` só os entrega ao hardware.

## Identificadores

`com.aurea.aurea` — o MESMO `applicationId` de `android/app/build.gradle.kts`,
conferido automaticamente por `verify/check_scope.py`. `CFBundleDisplayName` =
Aurea; `CFBundleVersion` = 2105 (revisão iOS do porte do editor atual).

**Permissões:** nenhuma. O Android não pede `CAMERA` nem `RECORD_AUDIO` (não há
captura no editor) e a mídia entra pelo seletor do sistema, que no iOS não pede
permissão — por isso NÃO há `NSMicrophoneUsageDescription` nem
`NSPhotoLibraryUsageDescription`. Declarar permissão sem uso é promessa falsa
(o mesmo critério que tirou as permissões do manifest).

## Piso de iOS: 16.3

A biblioteca C++ do sistema disponibiliza `std::to_chars` de ponto flutuante a partir do iOS 16.3.

O backend Metal caberia em 14 (`MTLBinaryArchive` é iOS 14). A UI não: as folhas
contextuais do editor usam `presentationDetents` e a navegação usa
`NavigationStack`, ambos iOS 16. Trocar o piso custaria reescrever os painéis.

## Estado do porte do editor atual

A interface usa a organização e as operações do Android desta árvore:
Home/projetos, dock/FAB, timeline com miniaturas e waveform, transformações,
efeitos, aparência, velocidade/áudio, formas, texto e animadores, texto em
caminho, 3D e HDRI por objeto, máscaras e rastreamento, vetores/desenho livre,
partículas, legendas, presets, curvas/expressões e exportação.

Os controles chamam o motor C++ compartilhado; os arquivos continuam sendo
`.aurea`. A importação faz cópia/decodificação fora da thread principal.
Legendas Groq exigem chave própria e envio explícito pelo botão de gerar;
as transcrições ficam em cache local, e a chave no Keychain do aparelho.

O registro de compilação, testes e limitações está em
[`docs/ios-parity.md`](../../../docs/ios-parity.md). Xcode aprovado não comprova
paridade completa nem funcionamento de todas as operações no aparelho.
Os diálogos e seletores nativos do iOS mantêm diferenças de apresentação.

## Idiomas

O catálogo do Android tem SETE (`values/` = pt-BR, + ar, en, es, hi, id, ru). O
iOS traz **pt-BR e inglês**, gerados do MESMO XML por `tools/_ios_strings.py`
(`app/AureaStrings.swift`, chaves idênticas: `home_tab_projects`,
`editor_desfazer`…). Trazer os outros cinco é rodar a mesma extração com os
outros `values-*`; nenhuma tela precisa mudar. O idioma padrão segue o sistema e
pode ser trocado em Ajustes.

Os textos do painel "Este aparelho" (números do `DeviceReport`) são literais em
português — igual ao Android, que os monta em `DeviceReport.kt` fora do catálogo.

## Auditorias (rodam no Windows, não precisam de Mac)

```bash
python engine/platform/ios/verify/check_symbols.py    # todo símbolo do núcleo existe
python engine/platform/ios/verify/check_pbxproj.py    # pbxproj: fileRefs, alvo, bundle id
python engine/platform/ios/verify/check_api_swift.py  # model.* / engine.* / chaves de texto
python engine/platform/ios/verify/check_scope.py      # imports, escopo, bundle id
```

As auditorias de símbolos, Swift/ponte, referências do Xcode e escopo devem
ser executadas com a árvore atual. O registro de resultados está em
`docs/ios-parity.md`.
