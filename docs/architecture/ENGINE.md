# Aurea Engine — arquitetura

O motor é uma biblioteca C++23 compartilhada entre Android e iOS. Nenhuma linha
dele conhece Compose, SwiftUI, Vulkan ou Metal diretamente.

```
                    AUREA
                      │
          ┌───────────┴───────────┐
          │                       │
       ANDROID                   iOS
       Kotlin                    Swift
 Jetpack Compose              SwiftUI
          │                       │
          └───────────┬───────────┘
                      │
                Native Bridge
                 JNI / ObjC++
                      │
        ┌─────────────▼──────────────┐
        │      AUREA ENGINE C++      │
        │  Timeline · Composition    │
        │  Animation · Effects       │
        │  FrameGraph · Cache        │
        │  Project · Export          │
        └─────────────┬──────────────┘
                      │
            ┌─────────┼──────────┐
            │         │          │
          MEDIA     RENDER     AUDIO
       HW Decode   FrameGraph   Mixer
```

## O princípio que decide tudo

```
CPU organiza.  GPU processa.  Hardware decodifica.  Hardware codifica.
UI apenas controla.
```

Toda decisão de arquitetura abaixo é consequência disso. As três coisas
proibidas, explicitamente:

- `GPU → CPU → GPU` (ler o frame de volta para a CPU entre passes);
- `decode → bitmap → Dart/Swift/Kotlin → UI → GPU`;
- qualquer processamento pesado na thread da UI.

## Camadas

### `aurea_core` — estático, sem plataforma

Não inclui `<jni.h>`, não inclui cabeçalho da Apple, não cria dispositivo
gráfico. É por isso que ele compila e roda no host: **a timeline, a animação,
os comandos e a serialização são testáveis sem GPU e sem aparelho**.

| Diretório | Responsabilidade |
| --- | --- |
| `core/` | tipos, handles, matemática, resultado, tempo, log, versão |
| `memory/` | arena de bump, orçamento de memória por categoria |
| `jobs/` | pool de tarefas, filas por prioridade, `parallel_for` |
| `command/` | command queue SPSC, undo/redo |
| `platform/` | `DeviceCapabilities` — o que ESTE aparelho faz |
| `animation/` | keyframes, curvas, avaliação |
| `timeline/` | composição, camadas, ordem vertical |
| `project/` | assets, projeto, formato `.aurea` |
| `render/` | `GPUBackend` (interface), `FrameGraph`, `EffectGraph`, `ShaderLibrary`, `RenderScheduler` |
| `engine/` | `Engine` — a fachada |

### `aurea` — compartilhada, por plataforma

Só existe no Android e no iOS. Contém a ponte e, quando existir, o backend
gráfico. Não pode conter regra de negócio: a mesma regra escrita duas vezes
diverge na primeira correção que só um lado recebe.

## A fronteira

Ver [BRIDGE.md](BRIDGE.md) para o contrato de memória. O resumo:

- a UI escreve **comandos POD** num buffer contíguo e envia o bloco;
- o motor devolve **status POD** num buffer direto que a UI já tem;
- **nenhum bitmap atravessa**. O frame vai do compositor direto para a
  superfície nativa.

Três travessias de bridge por frame, independente de quantas camadas o usuário
está mexendo.

## Propriedade e ciclo de vida

- Sem singleton global. `Engine` é criado e destruído explicitamente; quem cria,
  destrói.
- Sem exceções (`-fno-exceptions`) e sem RTTI no Android. Todo erro é `Status`.
- Handles com geração em vez de ponteiros: um handle de uma camada apagada
  devolve `nullptr` em vez de memória liberada.
- `RAII` em tudo que tem recurso: `Reservation` para memória, `ArenaScope` para
  a arena de frame.

## Onde cada coisa roda

| Thread | O quê |
| --- | --- |
| UI (Compose/SwiftUI) | gestos, desenho de painéis, escrita de comandos |
| Render | drenagem da fila, avaliação de animação, montagem do grafo, submissão |
| Workers (pool) | decode, proxy, miniatura, waveform, geometria, assets |
| Áudio | mixer — o master clock durante o playback |
| I/O | leitura e gravação de projeto |

Nenhuma tarefa longa bloqueia a thread de render. O que a UI espera é sempre
`render_frame`, e ele nunca espera a GPU terminar o frame anterior.

## Testes

171 testes, 1359 verificações, rodando no host sem GPU. Cobrem:

- matemática e interpolação de curva (numérico, com tolerância);
- motor de keyframes (limites, hold, bezier, ordenação);
- handles, arena, orçamento de memória, fila de comandos, undo;
- pool de tarefas e `parallel_for`;
- preview adaptativo, cache de frame, prefetch;
- timeline, composição, split, parenting, aninhamento;
- FrameGraph (ordem, poda, aliasing) com backend falso;
- compilador de efeitos (fusão, degradação de preview);
- cache de pipeline e shader;
- serialização `.aurea` (round-trip, corrupção, truncagem, journal);
- a fachada `Engine` ponta a ponta, incluindo o que **não** está implementado.

```bash
# host (Windows, com Visual Studio instalado)
cmake -S engine -B engine/build/host -G "Visual Studio 18 2026" -A x64 -DAUREA_BUILD_TESTS=ON
cmake --build engine/build/host --config RelWithDebInfo
engine/build/host/tests/RelWithDebInfo/aurea_tests.exe
```

## O que NÃO está implementado

Declarado aqui e no código, nunca escondido atrás de um botão que não faz nada:

| Recurso | Estado |
| --- | --- |
| Backend Vulkan | não existe — `GPUBackend::create_default()` devolve `nullptr` |
| Backend Metal | não existe |
| Decodificação de hardware | não existe — a interface `ExternalImageHandle` está pronta |
| Codificação de hardware / export | **recusado com `NotImplemented`** |
| Renderização de efeitos na GPU | o compilador de efeitos existe; os shaders não |
| Texto, vetor, máscara renderizada | estrutura de dados existe; rasterização não |
| Cena 3D, partículas | comandos existem e devolvem `NotImplemented` |
| Gravação incremental do `.aurea` | não existe — `incremental_save_implemented()` devolve `false` |
| Áudio (mixer, waveform) | não existe |

O motor **recusa** em vez de fingir: `start_export` devolve `NotImplemented` e
não cria arquivo nenhum. Um arquivo vazio que o usuário acha que é o trabalho
dele é pior do que um erro claro.

## Próximos passos, em ordem de dependência

1. **Backend Vulkan.** Desbloqueia tudo o resto: sem ele não há preview, não há
   efeito, não há export.
2. **Camada de mídia Android** (`MediaCodec` → `AHardwareBuffer` →
   `import_external_image`). Desbloqueia o vídeo no preview.
3. **Passes de composição** no FrameGraph, usando o backend.
4. **Codificador de hardware + muxer.** Desbloqueia o export.
5. **Efeitos**: registro + shaders, começando pelos que fundem.
