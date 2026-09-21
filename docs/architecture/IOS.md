# iOS

## Estado: esqueleto, não implementação

O projeto iOS **ainda não existe**. Este documento descreve o que ele será e por
quê, para que a decisão tomada no Android não seja redescoberta — ou pior,
tomada de forma diferente.

## O que já está pronto do lado do motor

Nada no núcleo C++ é específico de Android. `aurea_core` compila para iOS sem
alteração: não inclui `<jni.h>`, não inclui cabeçalho da Apple, não cria
dispositivo gráfico. As três plataformas são distinguidas só por macro:

| Macro | Onde |
| --- | --- |
| `AUREA_PLATFORM_ANDROID` | definida pelo CMake quando `ANDROID` |
| `AUREA_PLATFORM_IOS` | definida quando `CMAKE_SYSTEM_NAME` é `iOS` |
| `AUREA_PLATFORM_HOST` | qualquer outra coisa (testes) |

`core/Time.cpp` é o único arquivo com código por plataforma: `mach_absolute_time`
no iOS, `clock_gettime` no Android, `steady_clock` no host.

## A ponte

Android usa **JNI**. iOS usará **Objective-C++**.

```
Swift
  ↓
Objective-C++ (.mm)      ← o equivalente ao jurema_bridge.cpp
  ↓
C++ (aurea_core)
```

Objetivo-C++ é a escolha certa aqui: fala C++ direto (sem marshalling) e é
chamável do Swift sem camada intermediária. O arquivo `.mm` vai espelhar
`platform/android/aurea_jni.cpp` — **burro de propósito**: traduz tipos e chama o
motor, sem decidir nada. Duas implementações da mesma regra divergem na primeira
correção que só um lado recebe.

Estrutura planejada:

```
ios/
├── Aurea.xcodeproj
├── Aurea/
│   ├── AureaApp.swift           @main
│   ├── EditorView.swift         a UI (SwiftUI)
│   ├── EditorModel.swift        o equivalente ao EditorViewModel
│   ├── Engine/
│   │   ├── AureaEngine.swift    a classe da ponte (só ela declara @_silgen_name)
│   │   ├── EnginePods.swift     contrato de memória (espelho de BridgePods.hpp)
│   │   └── CommandBatch.swift   escrita de comandos
│   ├── UI/                      tema e telas
│   └── Assets.xcassets          identidade preservada
└── AureaEngine.xcconfig         flags do motor
```

## Contrato de memória

O mesmo `BridgePods.hpp`. Os offsets Swift usam `MemoryLayout<T>.offset(of:)` ou
constantes espelhadas com os mesmos `static_assert` do lado C++.

O ponto que **não** pode ser diferente: nada de `String`, `Array` ou classe
atravessando. Só POD em buffer contíguo. Um `Array` Swift seria copiado sem os
elementos, e o motor leria lixo.

## Preview

`CAMetalLayer` em vez do `SurfaceView`. Um `MTKView` ou uma view com
`layerClass = CAMetalLayer.self`, entregue ao motor como ponteiro opaco.

```
VideoToolbox → CVPixelBuffer → CVMetalTexture → Metal → FrameGraph → Display
```

O `CVPixelBuffer` é o equivalente do `AHardwareBuffer`: o motor o importa como
textura sem cópia de CPU, via `import_external_image`.

## Metal

O iOS **não** recebe shader escrito à mão em MSL. O mesmo GLSL é compilado para
SPIR-V e traduzido para MSL no build, por SPIRV-Cross. É isso que garante que
preview Android e preview iOS sejam visualmente idênticos — só existe uma
implementação do shader.

`MetalBackend` implementa a mesma interface `GPUBackend`. Nada acima dele muda.

## Interface

SwiftUI, com a mesma organização do Alight Motion, igual ao Android:

- preview grande em cima;
- timeline abaixo, com nomes fixos à esquerda e barras deslizando à direita;
- barra de ferramentas entre os dois;
- painel de propriedades embaixo.

A `EditorModel` espelha o `EditorViewModel`: ciclo de vida do motor, laço de
frame (`CADisplayLink` no lugar do `Choreographer`), tradução de gesto em
comando.

`CADisplayLink` em vez de `Timer`: entrega o instante do vsync real e acompanha
mudanças de taxa (ProMotion alterna entre 24 e 120 Hz conforme o conteúdo) — o
mesmo motivo pelo qual o Android usa `Choreographer`.

## Identidade

| Item | Valor |
| --- | --- |
| Bundle identifier | `com.aurea.aurea` |
| Display name | `Aurea` |
| Bundle name | `aurea` |
| Ícone | `Assets.xcassets/AppIcon.appiconset` |
| Splash | `Assets.xcassets/LaunchImage.imageset` |

Os assets do ícone e da launch image estão preservados em
`_identity/branding/ios-appicon/` e `_identity/branding/splash/`.

`ios/ExportOptions.plist` do projeto antigo está em `_identity/signing/`. Nenhum
certificado ou provisioning profile foi encontrado — a distribuição iOS sempre
foi por assinatura automática com `TEAM_ID` vindo de segredo do CI.

## Ordem de implementação

1. projeto Xcode com o target do motor (`aurea_core` + `MetalBackend`);
2. `AureaEngine.swift` + o `.mm` da ponte, com o contrato de memória;
3. `MetalBackend` — device, `CAMetalLayer`, import de `CVPixelBuffer`;
4. `EditorModel` com o laço de `CADisplayLink`;
5. as telas.

O passo 1 depende do mesmo bloqueio do Android: `MetalBackend` não existe.
