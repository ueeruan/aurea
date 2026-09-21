# Export

## Estado: recusado, não fingido

```
start_export(...)  →  Status{ NotImplemented }
export_progress()  →  { running: false, result: NotImplemented,
                        message: "exportacao de video ainda nao implementada" }
```

Nenhum arquivo é criado. Um teste verifica isso explicitamente.

O export depende de três coisas que não existem: os passes reais de composição
no backend gráfico, o decodificador de hardware da plataforma e o codificador de
hardware. Sem os três, qualquer coisa que `start_export` fizesse produziria um
arquivo vazio — ou pior, um arquivo que *parece* pronto e não tem imagem.

## O caminho

```
fonte
  ↓
decode de hardware
  ↓
Aurea GPU Renderer          ← o MESMO do preview
  ↓
encode de hardware
  ↓
mux
  ↓
arquivo final
```

| Plataforma | Decode | Encode | Mux |
| --- | --- | --- | --- |
| Android | `MediaCodec` | `MediaCodec` | FFmpeg |
| iOS | `VideoToolbox` | `VideoToolbox` | FFmpeg |

FFmpeg entra só no container e no mux — nunca como o compositor.

## Dois schedulers

`PreviewScheduler` e `ExportScheduler` são separados. O export não compete com o
preview e não herda a degradação dele.

O que eles **compartilham**, e é isso que garante que o resultado final é o que
o usuário viu:

- timeline;
- compositor;
- cadeia de efeitos;
- shaders;
- pipeline de cor;
- cena 3D;
- máscaras;
- avaliação de animação.

## A garantia de igualdade

**O export nunca degrada.** Preview e export chamam a MESMA função de compilação
de efeitos, com um booleano `preview` diferente:

```
EffectCompiler::compile(..., preview = true)   →  plano do preview
EffectCompiler::compile(..., preview = false)  →  plano FINAL
```

Mesma função, mesmo código, um bit de diferença. Não há como divergirem por
descuido — só por um bug, e um bug aparece nos dois.

O mesmo vale para a resolução: o export usa a resolução escolhida, sempre. O
preview pode estar em 1/4; o arquivo sai em cheio.

## Pré-aquecimento

Antes de começar, o export **compila todos os pipelines** de que vai precisar
(`ShaderLibrary::prewarm_effects`).

Um compile de 200 ms no meio da exportação seria 200 ms de vídeo com o frame
errado ou uma pausa — e nenhum dos dois é recuperável depois.

## Paralelismo

Planejado: o export identifica segmentos independentes e os distribui entre os
workers. `ExportSettings::parallelSegments` controla quantos; 1 é sequencial.

A paralelização tem um limite que não é de hardware: **o decode tem estado**. Um
GOP precisa ser decodificado desde o keyframe. Segmentar só funciona se cada
segmento começar num keyframe, e é por isso que o paralelismo é por GOP, não por
N frames.

## Formatos

| Container | Codecs de vídeo | Codecs de áudio |
| --- | --- | --- |
| MP4 | H.264, HEVC, AV1 (quando há HW) | AAC |
| MOV | H.264, HEVC, ProRes (Apple) | AAC, PCM |

Resoluções: 720p, 1080p, 1440p, 4K — e a arquitetura não assume teto, então
resoluções maiores dependem só de o aparelho suportar.

**Teto por aparelho**: `max_export_width/height` vêm do maior decoder detectado,
limitado também pela memória (um frame 4K RGBA16F ocupa 66 MB; num orçamento de
384 MB, três desses já enchem). Um aparelho que não decodifica 4K não deve deixar
o usuário escolher export 4K sem aviso — a exportação sairia com frames faltando
ou levaria uma eternidade em software.

## Duração e A/V

A duração do arquivo vem da maior composição de topo. Uma pre-comp de 3 s usada
dentro de uma de 30 s não estica a saída.

O áudio acompanha a timeline, não a cadência do render: se um frame demorar mais
que o previsto, o próximo áudio já está no lugar certo — o vídeo é que pula.

## Configurações persistidas

`ExportSettings` é guardado no `.aurea`. Reexportar depois usa os mesmos ajustes,
porque o usuário não quer reconfigurar bitrate a cada vez.

```cpp
ExportSettings {
    width, height, fps
    videoCodec, videoBitrateMbps, rateMode, keyframeIntervalFrames
    audioCodec, audioBitrateKbps, audioSampleRate, audioChannels
    container
    outputColorSpace, toneMapToSdr
    parallelSegments
    motionBlurSamples, opticalFlowQuality
    scale
}
```
