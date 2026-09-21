# Aurea Editor

Editor e compositor de vídeo mobile. Engine gráfica própria em C++23,
compartilhada entre Android (Kotlin + Compose) e iOS (Swift + SwiftUI).

## Princípio central

```
CPU organiza.  GPU processa.  Hardware decodifica.  Hardware codifica.
UI apenas controla.
```

A UI não processa frame, não renderiza vídeo e não conhece a timeline. Ela
apresenta e recebe input. Tudo o mais vive no motor C++, que não muda entre
Android e iOS.

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
│   ├── tests/                 243 testes (os de GPU usam o Vulkan do host)
│   ├── gpu/vulkan/            backend Vulkan (Android e host de testes)
│   └── platform/              ponte JNI + MediaCodec (Android) e ObjC++ (iOS, futuro)
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
cmake --build engine/build/host --config RelWithDebInfo
engine/build/host/tests/RelWithDebInfo/aurea_tests.exe
```

### Android

```bash
cd android
./gradlew :app:assembleDebug      # APK de desenvolvimento
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
| versionCode | 2102 (o antigo produzia 2101) |
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

O que **funciona hoje**: backend Vulkan (preview em superfície nativa, cache
de pipeline, frames em voo, timestamps de GPU), FrameGraph e EffectGraph com
fusão de passes, 12 efeitos novos (inclusive o Motion Tile portado), decode de
vídeo por MediaCodec (zero-copy por AHardwareBuffer ou planos pela CPU) com a
cor do próprio arquivo, scrub coalescido, playback no ritmo do conteúdo, preview
adaptativo, miniaturas da timeline, imagens que voltam ao reabrir o projeto,
desfazer/refazer por snapshot, timeline, keyframes, projeto `.aurea` com autosave
e recuperação, painel de desempenho — coberto por 243 testes no host.

O que **não existe ainda**: Metal (iOS), áudio, export, texto, vetor, máscara
renderizada, 3D e partículas. A UI mostra esses pontos de entrada e avisa "em
breve" em vez de fingir; o motor recusa (`NotImplemented`) o que não faz.

Ver [`docs/architecture/ENGINE.md`](docs/architecture/ENGINE.md) para a lista
completa e a ordem de implementação.

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
| [EXPORT.md](docs/architecture/EXPORT.md) | export, por que é recusado hoje |
| [3D_ENGINE.md](docs/architecture/3D_ENGINE.md) | cena 3D, PBR, LOD |
| [PERFORMANCE.md](docs/architecture/PERFORMANCE.md) | orçamento, threads, memória |
| [ANDROID.md](docs/architecture/ANDROID.md) | build, ciclo de vida, superfícies |
| [IOS.md](docs/architecture/IOS.md) | o que o projeto iOS será |

## Próximo passo

O **backend Vulkan**. Ele desbloqueia todo o resto: sem ele não há preview, não
há efeito e não há export. É o item 1 da ordem de implementação.
