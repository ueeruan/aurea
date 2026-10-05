# Aurea Editor

Editor e compositor de vídeo mobile. Engine gráfica própria em C++23,
compartilhada entre Android (Kotlin + Compose) e iOS (Swift + SwiftUI).

## Princípio central

```
CPU organiza.  GPU processa.  Hardware decodifica.  Hardware codifica.
UI apenas controla.
```

A UI apresenta o estado do projeto e recebe os gestos. As regras de edição,
animação e renderização vivem no motor C++ compartilhado. As pontes nativas
integram codecs, áudio, arquivos e o ciclo de vida de cada plataforma.

## Estrutura

```
Aureabeta/
├── engine/                    o motor C++23 (compartilhado)
│   ├── CMakeLists.txt
│   ├── include/aurea/         cabeçalhos públicos
│   │   ├── core/              tipos, handles, matemática, tempo, versão
│   │   ├── memory/            arena, orçamento de memória
│   │   ├── jobs/              pool de tarefas
│   │   ├── command/           fila de comandos, undo/redo
│   │   ├── platform/          capacidades do aparelho
│   │   ├── animation/         keyframes e curvas
│   │   ├── timeline/          composição e camadas
│   │   ├── project/           assets, projeto, formato .aurea
│   │   ├── render/            GPUBackend, FrameGraph, EffectGraph, scheduler
│   │   ├── bridge/            CONTRATO DE MEMÓRIA (BridgePods.hpp)
│   │   └── shaders/           fonte única dos shaders
│   ├── src/                   implementação
│   ├── tests/                 testes do motor, incluindo GPU Vulkan real
│   ├── gpu/vulkan/            backend Vulkan (Android e host de testes)
│   └── platform/              Android/JNI, iOS/ObjC++ e host Windows
│
├── android/                   UI nativa
│   └── app/src/main/
│       ├── java/com/aurea/aurea/
│       │   ├── engine/        a ponte (nada mais toca o motor)
│       │   ├── editor/        view model, laço de frame, telas
│       │   └── ui/            tema da marca
│       └── res/               identidade preservada
│
├── docs/architecture/         decisões arquiteturais
├── _identity/                 marca, assinatura, identificadores
└── tools/
```

## Build

### Motor (host, com testes)

```bash
cmake -S engine -B engine/build/host -G "Visual Studio 18 2026" -A x64 -DAUREA_BUILD_TESTS=ON
cmake --build engine/build/host --config Release --target aurea_tests
engine/build/host/tests/Release/aurea_tests.exe
```

### Android

```bash
cd android
./gradlew :app:assembleUiTest     # APK para testes da interface
./gradlew :app:assembleRelease    # APK de release (assinado)
./gradlew :app:assembleRelease -PaureaAbi=arm64-v8a   # uma ABI só
```

Requer `JAVA_HOME` apontando para um JDK 17+ e `android/local.properties` com
`sdk.dir`.

## Identidade preservada

| Item | Valor |
| --- | --- |
| applicationId / bundle id | `com.aurea.aurea` |
| Nome | Aurea Editor |
| versionCode / build iOS | 2139 |
| minSdk | 26 (o antigo declarava 24 — ver a nota abaixo) |
| Assinatura | a MESMA do Aurea oficial |

A assinatura do APK novo e do APK oficial distribuído são **idênticas**
(SHA-256 `55bf3cc8…52b5c8`) — o novo APK atualiza por cima da instalação
existente. Auditoria completa em
[`_identity/signing/IDENTIDADE.md`](_identity/signing/IDENTIDADE.md).

O `minSdk` subiu de 24 para 26 porque o pipeline zero-copy precisa de
`AHardwareBuffer` e de Vulkan 1.1 confiável. Um aparelho em API 24 ou 25 não
consegue instalar a atualização — escolha consciente entre "instalar e não
funcionar" e "não instalar".

## Estado da implementação

Há implementação de renderização Vulkan e Metal, preview com cache limitado e
linha azul na timeline, exportação de vídeo/áudio, composição 2D/3D, texto,
vetor, máscaras, partículas, efeitos, animação, legendas, importação de mídia,
projetos com autosave/recuperação, contas, comunidade e integração de IA.

Implementação não equivale a validação em aparelho. A auditoria de estabilidade
registra separadamente os testes do motor, Android, serviços locais e verificações
estáticas do iOS em
[`FULL_AUDIT_2026-10-04.md`](docs/architecture/FULL_AUDIT_2026-10-04.md).
O host Windows não compila nem executa a interface iOS; essa validação exige Xcode
e simulador/iPhone. Testes locais dos serviços usam dados e provedores de teste.

## Documentação

| Documento | Assunto |
| --- | --- |
| [ENGINE.md](docs/architecture/ENGINE.md) | arquitetura geral e camadas |
| [BRIDGE.md](docs/architecture/BRIDGE.md) | contrato de memória com a UI |
| [RENDERER.md](docs/architecture/RENDERER.md) | backend gráfico, zero-copy, shaders |
| [FRAMEGRAPH.md](docs/architecture/FRAMEGRAPH.md) | passes, poda, aliasing |
| [EFFECTGRAPH.md](docs/architecture/EFFECTGRAPH.md) | fusão de efeitos |
| [TIMELINE.md](docs/architecture/TIMELINE.md) | modelo, relógio, handles |
| [PROJECT_FORMAT.md](docs/architecture/PROJECT_FORMAT.md) | formato `.aurea`, autosave |
| [MEDIA_PIPELINE.md](docs/architecture/MEDIA_PIPELINE.md) | decode, proxy, zero-copy |
| [EXPORT.md](docs/architecture/EXPORT.md) | documentação do pipeline de exportação |
| [3D_ENGINE.md](docs/architecture/3D_ENGINE.md) | cena 3D, PBR, LOD |
| [PERFORMANCE.md](docs/architecture/PERFORMANCE.md) | orçamento, threads, memória |
| [ANDROID.md](docs/architecture/ANDROID.md) | build, ciclo de vida, superfícies |
| [IOS.md](docs/architecture/IOS.md) | arquitetura e integração iOS |

## Próximo passo

O **backend Vulkan**. Ele desbloqueia todo o resto: sem ele não há preview, não
há efeito e não há export. É o item 1 da ordem de implementação.
