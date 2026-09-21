# A fronteira nativa

Como a UI fala com o motor, e por que do jeito que fala.

## O problema

A UI roda a 60, 90 ou 120 Hz. Durante um arrasto ela mexe em dezenas de
propriedades por frame. Se cada `setPositionX()` atravessasse a fronteira
individualmente:

- 13 campos de transform × 40 camadas = 520 chamadas nativas por frame;
- cada uma com custo fixo de marshalling;
- cada uma pegando um lock.

A 60 Hz isso é 31 mil travessias de fronteira por segundo para mover um dedo.

## A solução

**Um bloco por frame.** A UI escreve comandos POD num buffer contíguo e envia o
bloco numa chamada. O número de travessias deixa de depender de quantas camadas
o usuário está mexendo.

```
   GESTO na UI
       │  Compose/SwiftUI escreve Command (128 B, POD) no buffer direto
       ▼
   submit_commands(buffer, count, stringBlob)      ◄── 1 travessia
       │
       ▼
   CommandQueue (SPSC, sem trava)
       │  drenada UMA vez por frame, na thread de render
       ▼
   Engine aplica ao modelo
       │
       ▼
   Renderer
```

## As três chamadas de um frame

| Chamada | Direção | Conteúdo |
| --- | --- | --- |
| `submitCommands` | UI → motor | comandos POD, ~1 KB por gesto |
| `renderFrame` | UI → motor | um `i64` (tempo de áudio) |
| `readStatus` | motor → UI | 256 B POD |

Mais nada. `readStatus` devolve `EngineStatusPOD`, preenchido pelo motor no
buffer direto que a UI já tem — sem alocação, sem objeto do lado gerenciado.

## O contrato de memória

`engine/include/aurea/bridge/BridgePods.hpp` é a fonte da verdade. Todo offset
tem um `static_assert`, e o espelho Kotlin (`EnginePods.kt`) usa os mesmos
números.

```
C++                                    Kotlin
─────────────────────────────────────  ─────────────────────────────────
BridgePods.hpp                         EnginePods.kt
  static_assert(offsetof(...) == N)      const val OFF_X = N
        │                                       │
        └────────── divergir quebra a compilação do motor ──────────┘
```

**Por que os tipos da fronteira são separados dos internos.** `Layer` e
`EngineStatus` são estruturas do motor; elas mudam quando o motor precisa. Se a
UI lesse essas structs por offset, cada mudança interna quebraria a UI sem
aviso: os campos sairiam deslocados, a tela mostraria números errados, sem crash
e sem erro de compilação. Aqui o bridge copia campo a campo, e as asserções
pegam o desalinhamento na compilação.

### Layouts congelados

| Tipo | Bytes | Onde |
| --- | --- | --- |
| `Command` | 128 | `command/Command.hpp` |
| `LayerRow` | 64 | `bridge/BridgePods.hpp` |
| `KeyframeRow` | 24 | `bridge/BridgePods.hpp` |
| `EngineStatusPOD` | 256 | `bridge/BridgePods.hpp` |
| `TelemetryPOD` | 128 | `bridge/BridgePods.hpp` |
| `ExportProgressPOD` | 128 | `bridge/BridgePods.hpp` |

`Command` ocupa 128 bytes — exatamente duas linhas de cache. Cada slot da fila
está alinhado a 64 bytes, então produtor e consumidor nunca compartilham linha
(o falso compartilhamento custaria mais do que o trabalho real da fila).

### Strings

Nomes de camada, conteúdo de texto e caminhos viajam num **blob separado**,
endereçados por `(stringOffset, stringLength)`. Nunca um ponteiro: um ponteiro
para o heap gerenciado seria inválido do outro lado, e o coletor poderia movê-lo
no meio da leitura.

### Payload dos comandos

O payload de 64 bytes é uma união: cada comando usa o seu arranjo. Os offsets
são declarados em `cmd_layout` (C++) e em `Off` (Kotlin), com `static_assert` de
cada um.

Ao acrescentar um campo a um payload: **acrescente no fim do struct**, atualize
os números nos dois lados, e deixe a asserção falhar até os dois concordarem.

## Por que buffers diretos e não objetos

A alternativa "natural" seria o JNI criar uma data class por camada. O custo,
por elemento:

- uma busca de campo por nome (`GetFieldID`);
- uma chamada JNI para ler cada campo;
- uma alocação no heap gerenciado.

Com 200 camadas a 60 Hz: 200 alocações e alguns milhares de buscas por frame.
O coletor de lixo cobra essa conta durante o playback — exatamente quando não
pode. O buffer direto elimina os três.

## Regra de uso

`CommandBatch.kt` concentra a escrita. Não escreva num offset solto: se falta um
método com nome, é sinal de que falta um comando no motor.

## O que NUNCA atravessa

- **Bitmap de vídeo.** O frame composto vai do compositor direto para a
  superfície nativa. A UI nunca vê pixels.
- **Ponteiro do motor.** Só handles (`u64`) e POD.
- **`std::string`, `std::vector`, `std::function`.** Um tipo com destrutor ou
  ponteiro na fronteira seria copiado byte a byte, e o ponteiro apontaria para
  memória do outro lado do processo.
