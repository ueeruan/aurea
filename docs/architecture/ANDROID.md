# Android

## Estrutura

```
android/
├── settings.gradle.kts         só o plugin do Android
├── build.gradle.kts            diretório de build fora da árvore
├── gradle/libs.versions.toml   versões num lugar só
├── key.properties              ASSINATURA (não versionado)
└── app/
    ├── build.gradle.kts        aplicação, assinatura, CMake
    └── src/main/
        ├── AndroidManifest.xml
        ├── java/com/aurea/aurea/
        │     ├── MainActivity.kt         única Activity
        │     ├── engine/                 A PONTE (nada mais toca o motor)
        │     │     ├── AureaEngine.kt
        │     │     ├── EnginePods.kt     contrato de memória
        │     │     └── CommandBatch.kt   escrita de comandos
        │     ├── editor/                 EditorViewModel, RenderLoop, telas
        │     └── ui/                     tema e cores da marca
        └── res/                          identidade preservada
```

## Onde o motor entra

`externalNativeBuild.cmake.path` aponta para **fora** do projeto Android:

```
path = file("../../engine/CMakeLists.txt")
```

É a MESMA árvore que o Xcode usa. Um motor duplicado por plataforma divergiria.

O CMake do motor gera `libaurea.so`, que o Kotlin carrega com
`System.loadLibrary("aurea")`. O nome tem que bater — está amarrado por
`OUTPUT_NAME` no CMake.

## As duas superfícies

O `SurfaceView` desenha o vídeo; o Compose desenha a interface por cima. São
duas superfícies de verdade.

É isso que permite o preview rodar a 60 fps mesmo quando a timeline rola — e é
por isso que nenhum bitmap de vídeo entra no heap gerenciado.

## Ciclo de vida

| Evento | O que acontece |
| --- | --- |
| `surfaceCreated` | inicializa o motor (fora da thread principal) |
| `surfaceChanged` | `resizeSurface` — rotação e split-screen |
| `surfaceDestroyed` | para o laço de frame |
| `onStart` | retoma o laço |
| `onStop` | **suspende** o motor (libera GPU e cache; o projeto fica) |
| `onDestroy` | encerra o motor |

`suspend()` não perde trabalho: o sistema pode matar o app em background a
qualquer momento, e o projeto permanece em memória. Voltar não recomeça.

### A janela nativa

`ANativeWindow_fromSurface` incrementa uma referência que PRECISA ser devolvida
com `ANativeWindow_release`. Esquecer a devolução vaza a superfície e o
SurfaceFlinger fica com um buffer preso.

O ponteiro é guardado num estático no bridge porque existe UM motor por
processo. Se dois motores forem criados, o bridge **recusa** o segundo em vez de
vazar a superfície do primeiro em silêncio.

## Laço de frame

`Choreographer`, não uma corrotina com `delay`.

`delay(16)` produz um laço a ~62 Hz sem relação com o display: num painel de
120 Hz metade dos frames chega tarde e a outra metade cedo, e o resultado é
micro-engasgo que nenhum perfil mostra como pico. Pior, a cada mudança de taxa o
laço continua no ritmo antigo.

O `Choreographer` entrega o `frameTimeNanos` do vsync REAL e reagenda na
cadência atual — a mesma fonte que o Compose usa, então o frame do motor e o da
interface caem no mesmo vsync.

## Build

```bash
# debug, para desenvolver
./gradlew :app:assembleDebug

# release
./gradlew :app:assembleRelease

# uma ABI só (APK menor)
./gradlew :app:assembleRelease -PaureaAbi=arm64-v8a
```

O diretório de build fica em `<projeto>/build/android`, **fora** da árvore
Android. Um `build/` dentro de `android/` apareceria para o git e para qualquer
busca no código — e são gigabytes de artefato.

## Assinatura

Ver `_identity/signing/IDENTIDADE.md` para a auditoria completa.

Resumo: o Aurea oficial é assinado com a **debug keystore** da máquina do dono
(o SHA-256 do certificado do APK distribuído bate exatamente). O novo projeto usa
a mesma chave via `android/key.properties`, que **não é versionado**.

Sem esse arquivo, o build cai no debug padrão do Gradle — que é a mesma chave.
Ele existe para deixar explícito de onde vem a assinatura, não porque o Gradle
precise dele.

## minSdk 26

O Aurea antigo declarava 24. O novo exige 26 porque o pipeline zero-copy precisa
de `AHardwareBuffer` (API 26) e de Vulkan 1.1 confiável.

Um aparelho em API 24 ou 25 não consegue instalar a atualização. É uma escolha
entre "instalar e não funcionar" e "não instalar" — e a segunda é honesta.

## Sem plugin do Kotlin

Com o AGP 9, o Kotlin é **embutido**. Aplicar `org.jetbrains.kotlin.android`
além dele faz o build falhar com "plugin no longer required".

O compilador de Compose NÃO é embutido: `buildFeatures.compose = true` sem
`org.jetbrains.kotlin.plugin.compose` falha com "the Compose Compiler Gradle
plugin is required".

## Configuração do build

- `-fno-exceptions -fno-rtti` no C++ — nenhuma fronteira do motor lança;
- `ANDROID_STL=c++_shared` — uma cópia da libc++ em vez de uma por biblioteca;
- `useLegacyPackaging = false` — a `.so` é carregada do APK, não extraída para
  um temporário. Extrair duplica em disco e falha quando `/data` está cheio;
- `abiFilters` explícito em `arm64-v8a` e `armeabi-v7a`. `--split-per-abi`
  perde os assets; o filtro explícito não.
