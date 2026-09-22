## 8F — Export

### Ambiente da medição (declarado, não é celular)
- **Host:** Windows 11 Pro 22631, AMD Ryzen 5 5500 (6 núcleos / 12 threads), 32 GB, NVIDIA GeForce RTX 3050 (driver 32.0.16.1692), Vulkan. Motor em Release (MSVC).
- **Encoder:** stub do host (`BenchSink` em `engine/tests/test_export.cpp`). Faz o mesmo trabalho que o `MediaCodecExport` faz na CPU: copia os planos Y/CbCr para um "buffer de entrada". Além disso calcula um hash (FNV em palavras de 64 bits) para o golden. O custo dele aparece em coluna própria (`encoder`). **Não há codificação H.264/HEVC real no host.**
- **Decoder:** vídeo sintético (`SyntheticVideo.hpp`, padrão cinza). O preenchimento por pixel foi trocado por `assign`, com os mesmos bytes de saída. Em 4K o laço antigo levava cerca de 40 ms por quadro e virava o gargalo de qualquer medida, o que não representa um decoder de hardware.
- **Áudio:** 48 kHz estéreo sintético, passando pelo mixer e pelo PCM16 reais.
- **Não medido aqui:** aparelho Android real (sem aparelho na bancada) e iOS (sem Mac). O caminho do MediaCodec foi **compilado** (`assembleDebug`, x86_64) mas **não executado**: pelas regras desta frente, sem adb/emulador.
- **Como reproduzir:**
  - comando: `AUREA_BENCH_EXPORT=1 AUREA_BENCH_GLTF=<pasta com DamagedHelmet.glb e Fox.glb> aurea_tests.exe Resolutions` (ou `EffectsAnd3D`, `LongExports`);
  - variante serial: `AUREA_BENCH_DEPTH=1`.

### Caminho antes (medido)
O export rodava numa thread só, com tudo em série: `prepare` (esperando o decoder) → `render` → **2× `read_texture`** → `write_video` → áudio. Em cada quadro, essa thread:
- criava e destruía dois buffers de leitura, dois command pools e dois fences;
- submetia e **esperava a GPU inteira duas vezes**;
- copiava os planos para vetores intermediários;
- quando faltava quadro do decoder, dormia 5 ms "às cegas" antes de tentar de novo.

Além disso, o `Renderer::render` com alvo offscreen chamava `begin_frame`. No Android, com swapchain, isso **adquiria e apresentava uma imagem vazia da tela a cada quadro exportado** e disputava a superfície com o ciclo de vida do app. É um risco de crash (P1) e foi corrigido: o render passou a usar `begin_offscreen_frame`.

### Caminho depois
O export virou um pipeline sobreposto de três estágios (§92–95):

| Estágio | Onde roda | O que faz |
|---|---|---|
| Decode N+1 | thread de decode (já andava adiantada no modo Playback) | Avisa o export por `on_frame_ready` → `exportWakeCv_`, sem o sono cego de 5 ms. |
| Render N | produtor `aurea-export` + GPU | Composição → NV12 → **cópia para o buffer de leitura do slot no mesmo frame**. Não há submissão extra nem espera logo depois do render. Depois de submeter N, espera **só o fence do quadro N−1** (`GPUBackend::wait_frame`). |
| Encode N−1 | thread `aurea-export-enc` | `write_video` direto do buffer mapeado, sem cópia intermediária, e o áudio até o fim do quadro. O sink continua sendo chamado de uma thread só. |

Limites e garantias:
- **Memória:** 3 slots circulam entre produtor e encoder, e isso limita a memória a 3 × 1,5 × L × A bytes (9,3 MB em 1080p, 37 MB em 4K). Quando o encoder não devolve slot, o produtor espera; é isso que segura o ritmo.
- **Ordem:** a ordem dos quadros e dos pts é garantida pela fila FIFO. O áudio continua amostra-exato.
- **Calor (§37):** em `set_thermal` Serious/Critical, ou com throttling, o export passa a 1 quadro em voo (serial). **Os bytes de saída são idênticos.** A UI recebe a flag `kExportThermalReduced`.

### Golden: saída idêntica antes × depois
Hash combinado (Y+CbCr de todos os quadros, com dither ligado, o padrão do app). O mesmo nas 2 execuções de antes e nas 3 de depois:

| Cenário | Hash antes | Hash depois |
|---|---|---|
| básico 1080p30 | cc85c3dc371f1641 | cc85c3dc371f1641 |
| básico 1080p60 | de4d470d4983892a | de4d470d4983892a |
| básico 4K30 | 91f26b38c5be5cc7 | 91f26b38c5be5cc7 |
| básico 4K60 | 8bc571984614c733 | 8bc571984614c733 |
| efeitos 1080p30 | f2483fefaf9e93d1 | f2483fefaf9e93d1 |
| efeitos 4K30 | 9b097721f0206073 | 9b097721f0206073 |
| 3D 1080p30 | 9e3ed556275721b5 | 9e3ed556275721b5 |
| 3D 4K30 | 49cf520ca3d89c04 | 49cf520ca3d89c04 |

**Diferença: 0.** Testes permanentes (`Export.*`):
- `PipelinedOutputIsByteIdenticalToSerial`: pipeline × serial, byte a byte (diferença máxima 0), com 64000 amostras de áudio exatas nos dois;
- `HeatReducesParallelismNeverQuality`: quente × frio, hash igual e flag ligada.

### Vazão do pipeline (quadros/s, host)
Antes: 2 execuções. Depois: 3 execuções, mediana entre parênteses. O ganho é calculado contra o **melhor** valor de antes.

| Cenário | Antes (q/s) | Depois (q/s) | Ganho |
|---|---|---|---|
| básico 1080p30 (150 q) | 277–306 | 704–735 (707) | 2,3× |
| básico 1080p60 (240 q) | 301–346 | 718–770 (748) | 2,2× |
| básico 4K30 (90 q) | 68–92 | 184–194 (188) | 2,0× |
| básico 4K60 (120 q) | 87–93 | 191–199 (192) | 2,1× |
| efeitos 1080p30 (120 q) | 197–222 | 392–405 (405) | 1,8× |
| efeitos 4K30 (60 q) | 58–64 | 97–106 (101) | 1,6× |
| 3D 1080p30 (120 q) | 124–138 | 177–194 (193) | 1,4× |
| 3D 4K30 (60 q) | 44–45 | 69–75 (72) | 1,6× |

Composição dos cenários:
- **efeitos:** 5 camadas (vídeo, 2 formas animadas, imagem e texto) com desfoque gaussiano 12 px, exposição, curvas, 3× brilho (glow), saturação, texto, e desfoque de movimento em 2 camadas que se movem.
- **3D:** DamagedHelmet (PBR), Fox (animação esquelética), sombras da luz-chave (padrão) e 2 emissores de partículas.

**Mesmo pipeline em serial** (`AUREA_BENCH_DEPTH=1`, que é o modo sob calor): 311/334/93/95 q/s no básico e 206–210/63–64/126–128/48 nos pesados. Isso é praticamente o antes. Ou seja, o ganho vem da **sobreposição**; tirar só a leitura síncrona quase não muda.

### Tempo por estágio (ms/quadro, básico 4K30)
| Estágio | Antes (serial, somam) | Depois (em paralelo) |
|---|---|---|
| decode (espera do quadro exato) | 0,09 | 0,03 |
| render (CPU: preparar + gravar + submeter) | 1,23 | 2,0–2,3 |
| readback (antes: 2 leituras síncronas; depois: espera do fence N−1) | 6,46 | 2,7–3,1 |
| encoder (stub: cópia de 12 MB + hash) | 2,80 | 3,5–4,0 |
| áudio | 0,27 | 0,28–0,31 |
| **tempo de parede por quadro** | **10,9** | **5,2** |

- **O que estoura depois:** o produtor (render + espera do fence, cerca de 5 ms), com o encoder logo atrás (cerca de 4 ms). No celular, a cópia para o buffer do MediaCodec entra na coluna "encoder".
- **Orçamento:** um export 4K30 no host anda a 5,2 ms/quadro, contra 33,33 ms do tempo real (6,3× o tempo real).

### Export longo (§98–101: modo acelerado, 160×90 a 30 fps com som)
| Duração | Antes | Depois | Memória privada a cada 10% (depois, MB) | Handles | Deriva A/V |
|---|---|---|---|---|---|
| 10 min (18 000 q) | 10,5 s, 1718 q/s | 4,8 s, 3767 q/s | 285 286 286 287 299 265 265 265 265 | 339 → 343 | 0 µs |
| 30 min (54 000 q) | 31,5 s, 1712 q/s | 17,2 s, 3141 q/s | 341 276 276 276 260 252 252 257 257 | 344 → 348 | 0 µs |
| 60 min (108 000 q) | 62,1 s, 1740 q/s | 36,9 s, 2924 q/s | 263 247 247 248 248 248 248 248 249 | 349 → 349 | 0 µs |

- **Memória:** não cresce por quadro. No de 60 min, de 10% a 90% ficou entre 247 e 249 MB; antes do 10% aparece a sobra do cenário anterior sendo devolvida.
- **Travamento:** nenhum (o vigia acusa travamento com 10 s sem quadro novo).
- **Áudio:** termina exatamente no fim do último quadro, em amostras inteiras, com pts contíguos. A deriva foi medida, não estimada.
- **Temperatura durante o longo:** não medida (o host não expõe estado térmico ao motor). A política de calor está coberta pelo teste de equivalência.

### Encoder de hardware (§96–97)
- O `MediaCodecExport` registra o encoder que **de fato** abriu, com nome (`AMediaCodec_getName`, API 28, via dlsym) e classificação hardware/software.
- **Fallback:** se o encoder de hardware recusa a receita (resolução ou fps acima do bloco), o sink tenta o encoder de software do sistema (`c2.android.*`/`OMX.google.*`) com a **mesma** resolução, taxa e fps. Não há queda silenciosa: aparece `AUREA_LOG_WARN` com os dois nomes e a UI recebe a flag `kExportSoftwareEncoder`.
  - O `ExportProgressPOD` leva as flags no campo +28 (antes `reserved`); o ABI não mudou.
  - O Exporter/ExportScreen mostra o aviso durante o export: "exportando por software (mais lento, mesma qualidade)".
- Quando o sink não sabe dizer (API < 28, ou o host), vale a tabela do MediaCodecList.
- **Sondagem de codecs:** antes do Android 10 todo codec contava como hardware. Agora o software do AOSP é reconhecido pelo nome. A versão da sondagem subiu para ela ser refeita uma vez.
- **Não verificado em aparelho:** a escolha real de codec, o fallback em execução e os nomes. Pendente para o teste em Android real.

### Temporários e cancelamento (§52, §153)
- **Arquivo parcial:** o sink apaga o arquivo parcial no cancelamento, no erro de encoder/muxer e na falha ao finalizar (código já existente, conferido).
- **Galeria:** o Exporter apaga o temporário depois de publicar, com sucesso ou falha. Na abertura, apaga o que sobrou de um export morto pelo sistema (arquivos da pasta `cache/export` mais velhos que a sessão, fora da main thread).
- **Cancelamento:** `cancel_export` acorda todas as esperas do pipeline (slot livre, fila do encoder, decoder). Medido em **15,9–18,5 ms** (antes 19,1 ms) com um encoder simulado de 20 ms por quadro (`CancelIsResponsiveAndReleasesTheSink`, limite de 250 ms). O sink é abortado, nunca finalizado, e a GPU volta ao preview.
- **Shutdown:** com export em andamento, usa o mesmo cancelamento. Antes só marcava a flag, e com o pipeline isso deixaria uma espera sem acordar.

### Testes
- `aurea_tests`: **450 testes, 0 falhas.** Eram 443; entraram 7 em `test_export.cpp`, dos quais 3 são benchmarks que só rodam com `AUREA_BENCH_EXPORT=1`.
- Os 9 testes de export que já existiam passam sem mudança.
- `:app:assembleDebug -PaureaAbi=x86_64` compila.
