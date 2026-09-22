# AUREA V2 — FASE 8: OTIMIZAÇÃO EXTREMA + ESTABILIDADE + APARELHOS FRACOS

(Resumo fiel do pedido do dono em 2026-09-22. A numeração § segue o original.)

## Regra da fase
- NÃO ADICIONAR PODER. FAZER O PODER QUE JÁ EXISTE FUNCIONAR DIREITO.
- Primeiro medir, depois corrigir o gargalo real e medir de novo.
- Nenhuma feature nova, a não ser que seja indispensável para performance, estabilidade, compatibilidade ou diagnóstico (§1).
- Ordem de prioridade (§166): 1 crashes, 2 perda de dados, 3 áudio, 4 travadas de UI, 5 tempo de quadro do preview, 6 memória, 7 export, 8 temperatura, 9 abertura do app, 10 micro-otimizações.
- Não micro-otimizar primeiro (§167).
- **Números sempre reais.** Todo relatório diz: aparelho, SO, resolução do projeto, resolução do preview, FPS, tempo de GPU, tempo de CPU e quadros perdidos. Nunca "60 FPS estável" no olho (§4, §159). Sem truques de benchmark: não desligar efeito, não pular quadro no export, não baixar a saída sem avisar (§163–165). Export final nunca muda por causa do preview adaptativo (§8).
- **Orçamento de quadro:** 16,67 ms a 60 fps e 33,33 ms a 30 fps. Dizer qual estágio estoura (§5).

## Marcos (§196)

### 8A — PROFILING
- **Campanha de medição (§2):** abertura a frio e a quente, abrir projeto, importar, decode, playback, scrub, timeline, waveform, miniaturas, efeitos, texto, 3D, partículas, optical flow, tracking, export, salvar, autosave e fechar projeto.
- **HUD de desenvolvimento (§3):** FPS do preview e da UI; tempo de CPU e de GPU; decode, render, composite e present; latência de áudio; quadros perdidos; RAM e estimativa de memória de GPU; caches de decode, render e textura; camadas e efeitos ativos; draw calls, triângulos e partículas; estado térmico; escala de render; estado do proxy.
- **Suíte de benchmarks automatizada (§128):** PERF_1080_BASIC, PERF_1080_HEAVY, PERF_4K_BASIC, PERF_4K_HEAVY, PERF_3D, PERF_PARTICLES, PERF_CAPTIONS, PERF_TIMELINE e PERF_EXPORT.
- **Baseline e regressão:** guardar a baseline (§129) e detectar regressão, por exemplo GPU +40% (§130).
- **Golden frames:** manter os golden frames e a comparação de imagem com tolerância (§131–132).
- **Relatório:** `docs/performance/PHASE_8_REPORT.md`, com antes/depois reais (§158–159).

### 8B — MEMÓRIA
- **Orçamentos reais no gerenciador de memória (§12):** quadros decodificados, texturas, geometria, miniaturas, waveform, proxies, optical flow, cache de render, partículas, assets 3D e buffers de export.
- **Pressão de memória do sistema (§13), ordem de despejo:**
  1. miniaturas fora da tela;
  2. waveform antiga;
  3. quadros decodificados sem uso;
  4. cache de render antigo;
  5. mips altos;
  6. assets 3D sem uso;
  7. temporários.
- Nunca perder estado do projeto, alterações não salvas nem timeline.
- **Todo cache** tem orçamento, LRU, versão, invalidação e métricas, com taxa de acerto medida (§14–15).
- **Frame cache por modo (§16):** prefetch à frente no playback, para trás no reverso, região atual no scrub e dependências no time remap.
- **Armazenamento (§49–51):** limites de disco para proxy, miniaturas, waveform, render e assets. Tela Ajustes › Armazenamento › Limpar cache, com os tamanhos. Limpeza automática ao passar do limite.
- **Temporários (§52):** todo arquivo temporário tem dono e ciclo de vida (sucesso, falha, cancelamento e recuperação após crash).
- **Vazamentos (§102–104, §155–157):** sessões longas de abrir, editar, fechar e exportar com a RAM estabilizando. Vida útil de texturas, buffers, decoders, encoders, threads, arquivos e objetos de GPU. Contagem de threads e handles estável.

### 8C — RENDER / PREVIEW
- **Preview AUTO 2.0 (§6–11):**
  - decide por métricas reais: GPU, CPU, decode, quadros perdidos, RAM, temperatura e complexidade;
  - escada de qualidade FULL → 1/2 → 1/4 → 1/8, mais amostras de motion blur, flow, SSAO, sombra, partículas, blur, LOD e mips;
  - histerese e cooldown, sem oscilar;
  - relógio certo: derruba quadro, nunca desacelera o vídeo; áudio e timeline no tempo.
- **Pular trabalho desnecessário (§19–22):**
  - camada invisível, fora da tela ou com opacidade 0 (sem matte dependente) não gasta nada;
  - efeito desligado não entra no grafo;
  - efeito identidade vira bypass (blur raio 0).
- **Fusão de passes (§23):** exposição, contraste, saturação, tint, opacidade e matriz de cor.
- **FrameGraph (§24–25):** reaproveitar render targets e aliasing de recursos transitórios.
- **Alocação e concorrência (§26–30):**
  - quase zero alocação por quadro no caminho quente;
  - pools só onde provado útil;
  - contenção de mutex medida;
  - nenhuma operação comum trava o motor inteiro.
- **Jobs (§31–34):**
  - prioridades REALTIME (áudio), HIGH (preview, decode atual, scrub), NORMAL (miniaturas e waveform visíveis), LOW (proxy, análise) e BACKGROUND;
  - sem starvation, sem sacrificar o playback;
  - número de threads adaptado a núcleos grandes/pequenos, temperatura e carga.
- **Decode (§17–18):** pool central de decoders respeitando o limite de hardware. Com 15 vídeos, o agendador prioriza por visibilidade, tempo, opacidade e relevância.
- **Render sob demanda (§39):** pausado e sem mudança, não renderiza.
- **Frame pacing (§145):** medir a variância.
- **GPU (§140–144):** Vulkan validation limpa. Barreiras, fences e semáforos auditados, sem `vkDeviceWaitIdle` no caminho quente. Sobreposição CPU/GPU.

### 8D — UI / TIMELINE
- **Perfilar as telas (§40–41):** Home, Editor, Efeitos, Vetor, Legendas, Timeline e Curva. Sem timers nem recomposições ociosas.
- **Escala da timeline (§42–45):**
  - 1000 clipes, virtualizada;
  - desenho em lote no canvas (régua, keyframes, waveform, marcas);
  - 10.000 keyframes com scroll e pinça usáveis.
- **Waveform e miniaturas (§46–47):** waveform em várias resoluções com cache persistente; miniaturas em segundo plano, com LOD e cache.
- **Latência de toque (§147):** do gesto ao retorno visual (transformar, aparar, scrub).
- **Legendas grandes (§84–85):** projetos com 2000–5000 palavras fluidos, preferindo o agrupamento.

### 8E — SISTEMAS PESADOS
- **Texto e vetor (§64–67):** atlas de glifos sem re-rasterizar nem re-upload; cache de fontes; vetor não re-tessela sem mudança; máscara estática reaproveitada.
- **3D (§68–75):**
  - frustum culling verificado e contado;
  - limiares de LOD sem popping;
  - streaming de mips;
  - deduplicar malha, textura, material e animação;
  - instancing;
  - draw calls medidos;
  - sombras reduzidas no preview AUTO.
- **Partículas (§76–79):** 10K, 100K, 500K e 1M; buffer sem resize constante; reuso de slots; ordenar só quando o blend exige.
- **Optical flow (§80–82):** perfilar estimativa, warp, oclusão e interpolação; preview em 1/2 ou 1/4, export em qualidade cheia; cache de flow com limite.
- **Tracking (§83):** usar proxy ou resolução reduzida.
- **Efeitos (§86–91):**
  - custo de GPU por efeito;
  - variante de preview dos pesados;
  - Deep Glow com pirâmide;
  - Lens Blur com escala de qualidade;
  - VHS/Glitch e Grain procedurais na GPU.

### 8F — EXPORT
- **Pipeline sobreposto (§92–95):** decode N+1, render N e encode N−1 ao mesmo tempo, com profundidade de fila que não estoura a memória.
- **Encoder de hardware (§96–97):** capacidades reais; fallback com log claro.
- **Benchmarks (§98–101):**
  - 1080p30, 1080p60, 4K30 e 4K60;
  - com efeitos (5 camadas, blur, glow, cor, texto, motion blur);
  - 3D (PBR, sombras, animação, partículas);
  - longos (10, 30 e 60 min), vigiando crescimento de memória, temperatura, deadlock e drift.
- **Temperatura (§37):** o export pode reduzir paralelismo, nunca qualidade.

### 8G — ESTABILIDADE
- **Projeto (§53–58):** salvamento incremental/journal. Autosave fora da main thread, com debounce e sem micro-freeze. Recuperação após force kill, bateria, sistema matando o processo e crash. Journal corrompido usa o último estado válido. Escrita atômica (temp → fsync → rename).
- **Arquivos ruins (§118–124):**
  - disco cheio durante autosave, proxy, export e cache, sem corromper;
  - mídia corrompida dá erro claro, sem crash;
  - mídia, fonte ou 3D faltando abre com placeholder e relink;
  - migração versionada e transacional, com backup.
- **Desfazer (§125–126):** 1000 operações; histórico com orçamento.
- **Projeto extremo (§127):** 500 camadas, 100 vídeos, 100 textos, 50 efeitos, 20 precomps, 3D, partículas, legendas e milhares de keyframes, estável.
- **Código (§113–117, §134–141):**
  - diagnóstico de crash sem dados privados;
  - códigos de erro padronizados (GPU_OUT_OF_MEMORY, DECODER_FAILED, ENCODER_UNAVAILABLE, ASSET_CORRUPTED, PROJECT_CORRUPTED, TEMP_STORAGE_FULL);
  - nada de `catch(...){}` silencioso;
  - efeito que falha vira bypass com erro;
  - fuzz no parser, nos parâmetros e nos comandos;
  - bounds checks;
  - ASan/UBSan no host;
  - clang-tidy focado;
  - auditoria de JNI.
- **Ciclo de vida Android (§105):** background, foreground, tela bloqueada e superfície recriada.
- **Caches corrompidos (§174–178):** cache, proxy ou shader cache corrompido é refeito; atualização do app; cache frio.
- **Carga progressiva (§148–154):** metadados → timeline → assets visíveis → resto; placeholders; cancelamento responsivo; nenhum job zumbi depois de fechar o projeto.
- **Sessões longas (§170–173):** 1 hora de playback, 30–60 min de edição, force kill e teste de OOM.
- **Bugs (§192–194):** P0 perda de dados, crash e corrupção; P1 preview ou export quebrado; P2 UX/performance; P3 cosmético. Lista estruturada: ID, severidade, repro, aparelho, causa, correção e teste. Cada bug crítico ganha teste.

### 8H — APARELHOS FRACOS
- **Classificação interna (§107–112):**
  - LOW/MID/HIGH/ULTRA medida pelas capacidades reais (não pelo modelo);
  - perfil LOW: preview 1/4, sombras e partículas menores, preferência por proxy, prévias mais leves e cache de decode menor;
  - microbenchmark opcional em segundo plano, sem atrasar a abertura.
- **Não esconder (§109):** feature indisponível é avisada ("Optical flow de alta qualidade indisponível neste aparelho").
- **Temperatura (§35–36):**
  - NORMAL;
  - WARM: menos jobs de fundo;
  - HOT: preview, partículas, sombras, flow e motion blur reduzidos;
  - CRITICAL: editor responsivo, sem crash.
- **Bateria (§38):** sem busy loops nem polling, sem render sem mudança, sem decoder ocioso.

### 8I — RELEASE
- **Abertura do app (§59–63):** inicialização preguiçosa (3D, flow, partículas e tracker só quando usados); pré-aquecer só os pipelines frequentes; cache de pipeline; navegador de efeitos com prévias sob demanda.
- **Medir em release (§180–183):** sem logging caro no caminho quente.
- **Rede (§184):** nada de rede no caminho quente.
- **Código morto (§185–186):** Comunidade/Perfil e render ou efeitos obsoletos.
- **Tamanho (§187–190):** APK auditado; sem assets de teste; prévias comprimidas; tamanho instalado documentado.
- **Limpeza final (§195):** TODO crítico, workaround crítico, mock em produção e feature falsa, todos zerados.

## Critério final (§197)
- **Medição:** profiling real e baseline de performance.
- **Preview:** AUTO funcionando e frame pacing melhor.
- **Memória:** orçamentos de cache, pressão de memória tratada e sem vazamento óbvio.
- **Timeline:** virtualizada, com waveform e miniaturas otimizadas e keyframes em stress.
- **Sistemas pesados:** proxy otimizado; LOD e streaming 3D; partículas escaláveis; flow com escala de qualidade; efeitos perfilados; caches de texto e vetor.
- **Abertura e carga:** app mais rápido, carga progressiva e autosave sem travar.
- **Estabilidade:** recuperação de crash e de cache corrompido; disco cheio tratado; background/foreground robusto.
- **Testes longos:** projeto longo, export longo e 1 hora de playback.
- **Aparelhos:** testes em Android real e em iOS real; políticas de aparelho fraco e de temperatura.
- **Release:** profiling em release e relatório de benchmark.
- **Bugs:** nenhum P0 e nenhum crash crítico conhecido.

## Limites conhecidos deste ambiente (declarar no relatório, não fingir)
- Sem Mac: iOS/Metal/Instruments não podem ser testados aqui.
- Sem aparelho Android físico na bancada: o emulador (x86_64, GPU do host) serve para funcionalidade e estabilidade, não para números de performance de celular real.
