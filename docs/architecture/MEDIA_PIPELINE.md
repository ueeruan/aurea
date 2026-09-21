# Pipeline de mídia

## Estado: interface pronta, implementação ausente

O motor **define** o pipeline e **recusa** o que não pode fazer. Nada aqui está
implementado ainda; o que existe são os contratos que garantem que a
implementação não vai reintroduzir o caminho proibido.

## O caminho proibido

```
decode → bitmap → Swift/Kotlin → UI → GPU
```

Cada seta dessa cadeia é uma cópia de um frame que pode ter 33 MB. A 60 fps,
essa cópia sozinha consome a banda que o compositor precisa.

## O caminho do Aurea

```
Android:
  FFmpeg / MediaExtractor   →  demux e parsing de container
          ↓
  MediaCodec                →  decode de hardware
          ↓
  Surface / AHardwareBuffer →  buffer compartilhado
          ↓                    (o motor importa como textura)
  Vulkan                    →  amostra direto
          ↓
  FrameGraph                →  composição

iOS:
  AVAssetReader / demux     →  demux
          ↓
  VideoToolbox              →  decode de hardware
          ↓
  CVPixelBuffer             →  buffer compartilhado
          ↓
  CVMetalTexture            →  importa como textura
          ↓
  FrameGraph                →  composição
```

**Sem cópia de CPU em nenhum ponto do caminho de frame.**

## Papel do FFmpeg

Demux, mux, container, parsing, formatos, importação, fallback de codec, áudio,
compatibilidade. **Não** é o compositor principal — o compositor é o Aurea
Renderer.

Sem codecs GPL. O que o FFmpeg faz aqui é ler containers e formatos; quem
decodifica é o hardware, e quem codifica é o hardware.

## Detecção de capacidades

`DeviceCapabilities` é preenchida pela plataforma (Android: `MediaCodecList`;
iOS: `VTIsHardwareDecodeSupported` + sondagem). O motor então se adapta:

| Pergunta | Uso |
| --- | --- |
| H.264 / HEVC / AV1 / VP9? | escolhe o caminho; sem codec, avisa que a reprodução será lenta |
| 8 ou 10 bits? | formato de textura interna |
| HDR? | pipeline de cor |
| resolução máxima? | teto de preview e de export |
| FPS máximo? | orçamento de decode |
| instâncias simultâneas? | paralelismo de decode REAL |
| aceleração de hardware? | decide entre zero-copy e cópia |

Nunca se pergunta "é um celular?" para assumir o pior. Pergunta-se "quantos
decoders HEVC 4K este aparelho tem?" e decide-se por isso.

## `ExternalImageHandle`

O ponto de entrada do zero-copy:

```cpp
struct ExternalImageHandle {
    void* nativeHandle;    // AHardwareBuffer* / CVPixelBufferRef
    u32   width, height;
    PixelFormat format;    // NV12, P010, RGBA8...
    i32   presentationTime;
    u32   timescale;
};
```

O motor só repassa; quem cria e destrói é a camada de mídia da plataforma. O
backend importa como textura e **cuida da sincronização**: o decoder sinaliza um
fence, o backend o espera antes de amostrar. Devolver a textura antes do fence é
o bug clássico de frame rasgado — por isso a interface só expõe
`import_external_image`, que já faz isso.

## Proxy

Um 4K HEVC não decodifica em tempo real em aparelho médio para *scrubbing*, ainda
que decodifique para playback. O proxy resolve:

```
Original:  4K HEVC
Proxy:     720p ou 540p, H.264, gerado em segundo plano
```

- o preview usa o proxy;
- **a exportação SEMPRE usa o original**;
- a troca é transparente: o usuário não escolhe, e não precisa saber.

## Metadados na importação

Abrir o container de um 4K HEVC custa dezenas de ms. Fazer isso ao arrastar uma
camada travaria o arrasto. Então os metadados são lidos UMA vez, na importação, e
guardados no `.aurea`:

- codec, perfil, nível, profundidade, subsampling;
- primárias e transferência de cor;
- resolução, taxa, contagem de frames;
- VFR? e a lista de tamanhos de amostra (sem ela, buscar um frame no meio de um
  VFR é chute).

## Áudio

O áudio é o **master clock** durante o playback. A camada de áudio precisa
produzir uma posição consultável; o renderer pergunta "que instante é agora?" e
desenha o frame correspondente.

Se o áudio não estiver pronto, o motor cai no relógio do sistema por um frame e
**registra isso na telemetria**. Nunca finge que o áudio está sincronizado.

Waveform: pré-computada num arquivo plano de picos por bucket. Desenhar a
waveform lendo o áudio inteiro a cada repaint seria absurdo.

## Export

```
fonte → decode HW → Aurea GPU Renderer → encode HW → mux
```

`PreviewScheduler` e `ExportScheduler` são **separados** — o export não compete
com o preview e não herda a degradação dele. Mas ambos compartilham timeline,
compositor, efeitos, shaders, pipeline de cor, 3D, máscaras e avaliação de
animação.

É essa partilha que faz o resultado final ser o mesmo que o usuário viu.

### Estado atual

`start_export` devolve `NotImplemented` e **não cria arquivo nenhum**. Um arquivo
vazio que o usuário acha que é o trabalho dele é pior do que um erro claro. Um
teste verifica que nenhum arquivo é criado.

## Ordem de implementação

1. camada de mídia Android: demux + `MediaCodec` → `AHardwareBuffer` →
   `import_external_image`;
2. passes de decode e de composição no FrameGraph;
3. proxy automático;
4. áudio e master clock;
5. codificador de hardware + muxer (export).

Os passos 1 e 2 dependem do backend Vulkan.
