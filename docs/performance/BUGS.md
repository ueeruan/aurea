# Aurea V2 — Fase 8: lista de bugs (§192–194)

Severidade (§192): **P0** perda de dados, crash, corrupção · **P1** preview/export quebrado (ou crash só com entrada malformada) · **P2** UX/performance · **P3** cosmético.
Cada P0/P1 tem teste automatizado (§194). IDs por frente: `G8-*` = 8G Estabilidade.

Ambiente de todos os números abaixo: **host Windows 11, MSVC Release, SSD NVMe, GPU do host (Vulkan)**. Nenhum número é de celular — não há aparelho na bancada (SPEC, "Limites conhecidos"). Tempo de fsync num flash de celular costuma ser maior; os valores servem para comparar antes/depois, não como meta de aparelho.

## 8G — Estabilidade

| ID | Sev. | Área | Estado | Teste |
|---|---|---|---|---|
| G8-01 | P0 | Salvar com disco cheio | corrigido | `Stability.DiskFullAndIoFailuresKeepTheOriginalIntact` |
| G8-02 | P0 | Rename que apagava o projeto | corrigido | idem (falha de rename injetada) |
| G8-03 | P0 | Leitura fora do buffer (offset perto de 2^64) | corrigido | `Stability.SectionOffsetsNearTwoToTheSixtyFourAreRejected` |
| G8-04 | P0 | Gravações simultâneas no mesmo `.tmp` | corrigido | `Stability.ConcurrentSavesAndEditsNeverCorruptTheFile` |
| G8-05 | P0 | Principal corrompido sem último estado válido | corrigido | `Stability.CorruptMainOpensLastValidCopyAndKeepsTheBadFile` |
| G8-06 | P0 | Seção de versão futura abria parcial e salvar apagava | corrigido | `Stability.FutureVersionIsRefusedWithoutTouchingAnyFile` |
| G8-07 | P1 | Track matte / legenda / texto no caminho trocavam de camada ao reabrir | corrigido | `Stability.LayerReferencesSurviveReopenAfterReorderAndDelete` |
| G8-08 | P1 | Pré-composição abria vazia depois de apagar uma composição | corrigido | `Stability.PrecompLinksSurviveReopenAfterACompositionIsDeleted` |
| G8-09 | P1 | `string_at` / `submit_commands`: estouro de u32 e cópia fora do buffer | corrigido | `Fuzz.RandomCommandsThroughTheQueueNeverCrash` |
| G8-10 | P1 | `Command` com 56 bytes de payload não inicializados | corrigido | `History.*` (test_engine) + fuzz de comandos |
| G8-11 | P1 | Comando com enum fora da faixa / NaN / quadro absurdo entrava no modelo | corrigido | `Fuzz.RandomCommandsThroughTheQueueNeverCrash` |
| G8-12 | P1 | Parâmetro de efeito fora do contrato (NaN, inf, 1e6) chegava ao efeito | corrigido | `Fuzz.EffectParametersWithWildValues*` |
| G8-13 | P1 | Journal corrompido alocava o que o cabeçalho declarava (até 4 GB) | corrigido | `Stability.CorruptJournalHeaderDoesNotAllocateWhatItDeclares` |
| G8-14 | P1 | Desfazer sem orçamento de memória | corrigido | `Stability.UndoHistoryRespectsItsMemoryBudget`, `Stability.ThousandUndoRedoStepsAreConsistent` |
| G8-15 | P1 | Duplicar projeto com disco cheio derrubava o app | corrigido | sem teste automatizado (ver linha) |
| G8-16 | P2 | Salvar prendia o lock do modelo durante E/S + fsync (micro-freeze) | corrigido | `Stability.ConcurrentSaves…`, `Stability.ExtremeProject…` (medem) |
| G8-17 | P2 | Ir para segundo plano gravava na main thread | corrigido | sem teste automatizado (Kotlin, sem emulador) |
| G8-18 | P2 | Sidecar `.meta.json` e miniatura sem escrita atômica | corrigido | `SaveAndErrorsTest.sidecarIsWrittenAtomically` (JVM) |
| G8-19 | P2 | Erros chegavam à UI como número ("código 28") e aberturas degradadas eram silenciosas | corrigido | `SaveAndErrorsTest.standardizedCodesHaveHumanMessages` (JVM) + asserts de `last_load_notice` |
| G8-20 | P2 | `catch` silenciosos em caminhos de mídia/import (Kotlin e JNI) | corrigido | — (log) |
| G8-21 | P2 | Efeito que falha logava a cada quadro | corrigido | `EffectGraph::bypassed_total` no fuzz de GPU |
| G8-22 | P2 | Import com mídia/modelo corrompido saía como erro genérico | corrigido | `Stability.CorruptImportsFailWithStandardCodeAndChangeNothing` |
| G8-23 | P2 | Registro de testes com teto de 512 descartava testes em silêncio | corrigido | contagem da suíte (464+) |
| G8-26 | P0 | Use-after-free no upload de buffer só-GPU (Vulkan) — achado pelo ASan | corrigido | `Gpu.Scene3D*` sob ASan |
| G8-24 | P2 | RGB no tempo com deslocamento grande prende `render_offscreen` 2–4,6 s | **aberto** (frente de render/export) | `Fuzz.EffectParametersWithWildValuesRenderOnGpu` (imprime `LENTO`) |
| G8-25 | P3 | Suíte JVM do Android não compila na base (`TimelineMathTest`: `Friction` inexistente; `TimelineHitTest` com 2 falhas) | **aberto** (frente de timeline) | — |

---

### G8-01 · P0 · Salvar com disco cheio trocava o projeto bom por um truncado
- **Repro:** encher o armazenamento e salvar (manual ou autosave). Teste: falha injetada (`fileio::Fault::DiskFullAfter` 0 / 100 / metade, `FlushFails`).
- **Causa:** `write_file_exact` só olhava o `fwrite`; `fflush` e `fclose` não eram checados e não havia fsync de verdade. ENOSPC costuma aparecer justamente no flush/close (o `fwrite` só enche o buffer da libc) → "gravado" → rename do temporário truncado por cima do projeto.
- **Correção:** `project/FileIO` (`write_atomic`): temporário → fwrite → fflush → fsync (`fsync`/`_commit`) → fclose, todos checados; falha remove o temporário e devolve `StorageFull` (TEMP_STORAGE_FULL) ou `IoError`. O projeto continua sujo e o autosave tenta de novo (Kotlin espera 30 s depois de uma falha e avisa uma vez).
- **Teste:** 6 falhas injetadas; o arquivo anterior confere **byte a byte**, nenhum `.tmp` fica, `dirty` continua `true`, e com o espaço de volta a gravação entra.

### G8-02 · P0 · Rename que podia apagar o projeto
- **Causa:** `rename_replace` no Windows apagava o destino antes de renomear (janela sem projeto em disco); no POSIX, se o `rename` falhasse, apagava o destino e tentava de novo — se a segunda também falhasse, o projeto sumia.
- **Correção:** POSIX `rename` atômico sem apagar nada; Windows `ReplaceFileW` / `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`; fsync da pasta no POSIX.
- **Teste:** `Fault::RenameFails` → original intacto, `.tmp` removido.

### G8-03 · P0 · Leitura fora do buffer com offsets perto de 2^64
- **Causa:** `sh.offset + sh.size > file.size()` (e `indexOffset + 40·i`, também no `peek`) em u64: com offset perto de 2^64 a soma dá a volta, passa no teste e o CRC lê memória fora do arquivo. Achado na auditoria do leitor.
- **Correção:** comparações por subtração (`offset > size || len > size - offset`) no `load_bytes` e no `peek`; enums lidos do arquivo passam por `checked_enum` (camada, mistura, interpolação, máscara, luz, trilha, asset, cor, codec, prévia).
- **Teste:** índice e seção com offset `~0`, `~0-7`, `2^64 - tamanho + 1` → recusado (estrito) ou seção marcada corrompida (tolerante); `peek` não devolve seção. Mais 4000 buffers aleatórios e 1094 truncamentos sem crash.

### G8-04 · P0 · Gravações simultâneas escreviam intercaladas no mesmo temporário
- **Repro:** autosave (thread IO) + "Salvar" + ir para segundo plano ao mesmo tempo.
- **Causa:** nenhuma exclusão entre gravações; todas usavam `<path>.tmp`.
- **Correção:** `Engine::saveMutex_` (uma gravação por vez) e trava global em `write_atomic`.
- **Teste:** 2 threads × 25 gravações + 40 edições concorrentes; o arquivo final abre limpo (CRC de todas as seções).

### G8-05 · P0 · Principal corrompido: não havia último estado válido
- **Repro:** queda no meio da escrita, bytes corrompidos, arquivo zerado.
- **Causa:** sem cópia anterior; `load_project` abria "tolerante" (uma timeline corrompida virava projeto vazio e o usuário podia salvar por cima) ou falhava.
- **Correção:** cada gravação guarda a versão anterior em `.bak` sem janela (hard link / `ReplaceFileW`, cópia se o sistema recusar link). A abertura tenta: principal estrito → `.tmp` (queda entre fsync e rename, é o mais novo) → `.bak` → parcial só se a timeline veio. O principal ruim é copiado para `.corrompido` (nunca apagado) e a próxima gravação **não** o gira para `.bak`. Nada válido → `ProjectCorrupted` (PROJECT_CORRUPTED) e o projeto que estava aberto continua aberto. A UI avisa (`nativeLoadNotice`).
- **Teste:** 8 cenários (cortes em 0/10/64/⅓/½/n−1, ruído, bit trocado) → abre o `.bak` com o número certo de camadas, `.corrompido` igual ao arquivo ruim, `.bak` preservado depois de salvar; `.tmp` inteiro preferido; sem cópia → `ProjectCorrupted`, sem trocar o projeto aberto.

### G8-06 · P0 · Seção de versão futura abria parcial e salvar apagava a timeline
- **Causa:** seção Timeline de versão maior que a conhecida era "pulada" e o projeto abria sem ela; salvar por cima gravava uma timeline vazia.
- **Correção:** seção conhecida com versão futura → `UnsupportedVersion` ("gravado por uma versão mais nova do Aurea"); a abertura não tenta `.bak` (abrir o mais velho e salvar por cima perderia o trabalho novo). A Home (só metadados) ainda lê o título.
- **Teste:** cabeçalho com `minReaderVersion` 99 e seção Timeline v99 → recusa, principal e `.bak` intactos, nenhum `.corrompido`.

### G8-07 · P1 · Referências de camada apontavam para outra camada ao reabrir
- **Repro:** duplicar/reordenar/apagar camadas, ligar track matte (ou legenda, texto no caminho), salvar e reabrir → a matte vira outra camada.
- **Causa:** ao abrir, os ids de camada são refeitos na ordem vertical; `parent` e câmera ativa eram remapeados, `matteSource`, `text.captionSource` e `text.pathLayer` não.
- **Correção:** todas passam pelo mapa id-do-arquivo → id-da-sessão; id sem camada vira "nenhuma".
- **Teste:** o teste **falhava antes** da correção (matte apontando para outra camada) e passa depois. O teste antigo `Serialization.EffectsAndMasksSurviveRoundTrip`, que conferia um id de matte solto `{7,3}` copiado cru, passou a usar uma camada real.

### G8-08 · P1 · Pré-composição abria vazia depois de apagar uma composição
- **Causa:** composições são recriadas na ordem dos slots, sem os buracos; `layer.nested.composition`, raiz e atual eram lidos crus.
- **Correção:** mapa de composições na leitura; pré-composição, raiz e composição atual remapeadas.
- **Teste:** **falhava antes** (verificado desligando só o remapeamento: `alive == 1` falha) e passa depois.

### G8-09 · P1 · Strings de comando: estouro de u32 e leitura fora do buffer
- **Causa:** `submit_commands` checava `offset + length > blobSize` em u32 (dá a volta); `CommandQueue::string_at` fazia o mesmo e, no caso inválido, devolvia `""` — de onde `drain_commands_locked` copiava `length` bytes.
- **Correção:** comparações por subtração; `string_at` devolve `nullptr` fora da arena.
- **Teste:** fuzz de 20 000 comandos com offsets `0xFFFFFFF0/0x20` e aleatórios, pela fila real e pelo atalho direto.

### G8-10 · P1 · `Command` montado em C++ com 56 bytes de lixo no payload
- **Causa:** o `raw = 0` da união zera só 8 dos 64 bytes; comandos montados campo a campo (motor, recuperação, testes) levavam lixo da pilha — `position_cmd` dos testes não escrevia `z`, e o transform recebia o que houvesse na pilha. Exposto pela validação nova (G8-11), que recusou o NaN.
- **Correção:** construtor que zera o comando inteiro (continua trivialmente copiável para a bridge).

### G8-11 · P1 · Comando malformado entrava no modelo
- **Causa:** tipo de camada 999, modo de mistura 300, NaN/inf em transform/keyframe/efeito, quadro 2^62 chegavam ao modelo (e ao renderer como índice de tabela ou matriz envenenada).
- **Correção:** `command_valid` antes do snapshot de desfazer (comando inválido não vira ação vazia no histórico): enums na faixa, floats finitos, quadros |t| < 2^40, fps (0, 1000].
- **Teste:** fuzz de comandos (20 000, 100 ms) + 300 projetos mutados abertos/renderizados/salvos no motor.

### G8-12 · P1 · Parâmetro de efeito fora do contrato chegava ao efeito
- **Repro:** expressão, keyframe antigo ou arquivo corrompido levando NaN/inf/1e6 a um parâmetro. Medido: RGB no tempo com deslocamento 1e6 quadros → **4 432–4 948 ms por quadro** em `render_offscreen` (160×90).
- **Correção:** `evaluate_param` entrega NaN/inf → padrão e o resto no `[min, max]` declarado (cor livre por HDR; o slot de expressão também passa pela regra).
- **Teste:** 47 efeitos × 60 valores extremos no planejamento (CPU) e 47 × 6 quadros na GPU: 0 quadros recusados, 0 crash. Resta o G8-24 (valor legítimo no limite ainda é lento).

### G8-13 · P1 · Journal corrompido alocava o que o cabeçalho declarava
- **Causa:** `read_journal` fazia `vector<Command>(commandCount)` (até 128 MB) e `vector<char>(stringBlobSize)` (até 4 GB) antes de ler — com exceções desligadas, OOM = abort, justamente na recuperação pós-queda.
- **Correção:** o arquivo é lido uma vez (teto 512 MB) e cada bloco é conferido contra os bytes que sobram antes de alocar; `append_journal` checa cada escrita e devolve `StorageFull`.
- **Teste:** bloco bom + bloco com contagens absurdas → o bom é lido, nada é alocado.
- **Nota:** o journal existe mas **não está ligado** ao autosave (o autosave grava o projeto inteiro quando parado; ver "Autosave" abaixo).

### G8-14 · P1 · Desfazer sem orçamento de memória
- **Causa:** 200 snapshots completos da composição, sem teto de bytes. Medido: composição de 300 camadas × 200 keyframes = **3,11 MB por snapshot** → 200 ações ≈ 620 MB.
- **Correção:** até **1000 ações** (§125) com orçamento de bytes (§126): 1/16 do orçamento do motor, entre 16 e 128 MB; saem as mais antigas; a última ação sempre fica desfazível; só saem ações já aplicadas (o refazer não desalinha). `undoBlobBytes` da telemetria agora é real.
- **Teste:** 1000 ações variadas (opacidade, posição, keyframes, efeito, duplicar, trim, visibilidade) → 1000 desfazer e 1000 refazer com a timeline **idêntica byte a byte** a cada passo; nenhuma mutação fora do histórico. Teto: 5,5 snapshots de orçamento → 5 entradas, `bytes() ≤ teto` a cada ação.

### G8-15 · P1 · Duplicar projeto com disco cheio derrubava o app
- **Causa:** `src.copyTo(dst)` dentro de `viewModelScope.launch(IO)` sem `try`: `IOException` sem dono na corrotina = crash; a cópia pela metade ficava na pasta.
- **Correção:** cópia para `.tmp` + rename, `try/catch`, limpeza do que ficou pela metade, aviso com `humanError`.
- **Teste:** sem teste automatizado — é Kotlin com `Application`, e esta frente não usa emulador. Revisado no código; a escrita atômica do sidecar usada ali tem teste JVM.

### G8-16 · P2 · Salvar prendia o lock do modelo durante a E/S (micro-freeze)
- **Causa:** `Engine::save_project` segurava `modelMutex_` durante serializar + escrever + fsync; a UI lê o estado a cada vsync (`read_status`) sob o mesmo lock.
- **Correção:** `encode` sob o lock (cópia em bytes), E/S + fsync fora; "sujo" só é limpo se nada mudou durante a escrita (`mark_clean_if` + revisão do modelo).
- **Medido (host):** projeto extremo (510 camadas): lock **5,7 ms** + escrita **16,6 ms** (0,73 MB). Antes o lock cobria os dois: **~22 ms** preso. Projeto comum (11 camadas com efeitos, keyframes e máscara; 47 KB): lock ≤ **2,0 ms** (máx. de 51 gravações), escrita 12,5 ms. Em celular o fsync é maior, e é exatamente a parte que saiu do lock.

### G8-17 · P2 · Ir para segundo plano gravava na main thread
- **Causa:** `onEnterBackground()` chamava `saveIfDirty()` → `saveBlocking` (encode + fsync + render da miniatura) na main.
- **Correção:** a gravação vai para a `lifecycleThread`, na mesma fila e ANTES do `suspend` (o motor ainda está de pé); tempo logado. `onCleared` (fim da Activity) segue síncrono, antes do `shutdown`.

### G8-18 · P2 · Sidecar e miniatura sem escrita atômica
- **Causa:** `File.writeText` / `FileOutputStream` direto: queda no meio = cartão da Home sem título ou miniatura cortada.
- **Correção:** `writeTextAtomic` (tmp → `fd.sync()` → `Files.move(ATOMIC_MOVE, REPLACE_EXISTING)`) no salvar, renomear e duplicar; miniatura por tmp + move.

### G8-19 · P2 · Erro numérico e abertura degradada silenciosa
- **Correção:** `Errc` ganhou `StorageFull`, `AssetCorrupted`, `ProjectCorrupted`, `EncoderUnavailable` (no fim do enum; números com `static_assert`); `error_code_name()` dá os nomes §115 (GPU_OUT_OF_MEMORY = `OutOfDeviceMemory`, DECODER_FAILED = `DecodeFailed`, ENCODER_UNAVAILABLE, ASSET_CORRUPTED, PROJECT_CORRUPTED, TEMP_STORAGE_FULL) para log sem dado do usuário. Kotlin: `humanError(code)` em abrir/salvar/autosave/importar; `nativeLoadNotice` → aviso de "abrimos a última cópia válida", "abriu com partes faltando", "versão anterior: cópia guardada", "N mídias não encontradas".
- **Pendente de outra frente:** `EncoderUnavailable` existe e tem mensagem, mas quem o devolve é o export (frente 8F).

### G8-20 · P2 · `catch` silenciosos
- `AureaEngine.openContentFd`, `decodeBitmapRgba` (Kotlin), `copyModelToSandbox`, `copyToDir`, `readMeta`, `renameProjectFile` e as duas `ExceptionClear` da JNI (abrir mídia, decodificar imagem) agora logam a classe do erro (sem URI/caminho do usuário). As cópias de import removem o temporário pela metade (§52).

### G8-21 · P2 · Efeito que falha logava a cada quadro
- **Correção:** bypass mantido (a camada segue sem o efeito, o quadro sai), log **uma vez por tipo**, contador `EffectGraph::bypassed_total()` (§117).

### G8-22 · P2 · Import corrompido com erro genérico
- Vídeo sem dimensões, áudio sem duração e modelo 3D ilegível → `AssetCorrupted`; arquivo que não sonda → `UnsupportedFormat`. Nada muda no projeto.
- **Teste:** 152 modelos quebrados (glTF com buffer de 1 TB, GLB com chunk absurdo, 150 arquivos de lixo `.glb/.gltf/.fbx/.obj`) → 152 recusados, 0 camadas criadas; fonte e HDRI de lixo recusados.

### G8-23 · P2 · Teto de 512 testes
- `TestFramework::Registry::add` ignorava o teste 513 em diante sem avisar (a suíte ficaria "verde" com testes a menos). Teto → 1024.

### G8-26 · P0 · Use-after-free no upload de buffer só-GPU (Vulkan)
- **Repro:** `aurea_tests` no build ASan → `Gpu.Scene3DFrontFaceIsVisibleAndBackFaceIsCulled`: `heap-use-after-free` em `vk::Backend::write_buffer` (VulkanResources.cpp:283), chamado por `GpuModel::upload` (import/abertura de modelo 3D).
- **Causa:** `write_buffer` pegava `Buffer* b = buffers_.get(dst)` e, para buffer só de GPU, criava o staging com `create_buffer` — que pode realocar o pool — e depois lia `b->buffer`. Sem ASan, lia memória já liberada e, na maior parte das vezes, "funcionava".
- **Correção:** o `VkBuffer` de destino é copiado antes de criar o staging (`b` zerado para não ser reusado). Os caminhos de textura usam outro pool e não têm o problema (conferidos).
- **Teste:** a suíte inteira sob ASan passa sem nenhum relatório (ver abaixo).

### G8-24 · P2 · ABERTO · RGB no tempo com deslocamento grande trava `render_offscreen`
- **Repro:** `Fuzz.EffectParametersWithWildValuesRenderOnGpu` imprime `LENTO`: 2 219–4 560 ms por quadro 160×90 com deslocamentos no limite do parâmetro (±120 quadros, ou ±120 s na unidade "segundos"), clipe sintético de 300 quadros.
- **Causa provável:** as três fontes extras são pedidas longe do cabeçote (ou fora da duração, com "prender nas pontas" desligado) e o caminho offscreen/export espera o decoder até o prazo. O preview ao vivo não espera.
- **Por que não corrigido aqui:** o código está em `render/Renderer.cpp` (frente de render/export). Afeta export e a miniatura do salvar (`captureFrame`), que agora roda fora da main.

### G8-25 · P3 · ABERTO · Suíte JVM do Android quebrada na base
- `TimelineMathTest.kt` referencia `Friction` (não existe) e `TimelineHitTest` tem 2 falhas — a task `testDebugUnitTest` não compila na base. `SaveAndErrorsTest` (esta frente) foi rodado com o `TimelineMathTest` fora do caminho, localmente: **2/2 ok**. Arquivos da frente de timeline, não mexidos.

---

## Resultado das suítes (8G)

- **Host, Release:** `aurea_tests.exe` — **465 testes, 547 049 verificações, 0 falhas** (eram 443; +22: 17 `Stability`, 5 `Fuzz`), 53–76 s com GPU.
- **Fuzz (determinístico, semente fixa):** parser 6 000 mutações com CRC refeito, 1,3–1,6 s (todas as que abriram regravam e reabrem); 300 projetos mutados pelo motor inteiro (abrir, 4 quadros, desfazer, salvar); 20 000 comandos aleatórios pela fila real, 100–134 ms; 47 efeitos × 60 valores extremos no planejamento e 47 × 6 quadros na GPU; 4 000 buffers aleatórios; 1 094 truncamentos. Zero crash.
- **ASan (§136):** MSVC `/fsanitize=address` em todos os alvos (núcleo, Vulkan, terceiros, testes), build separado `engine/build/asan` (`-DAUREA_ENABLE_SANITIZE=ON`; a DLL `clang_rt.asan_dynamic-x86_64.dll` da pasta `bin\Hostx64\x64` do MSVC no PATH). Resultado: 1ª rodada achou o G8-26 (abortou em `Gpu.Scene3D*`); depois da correção, **465 testes, 0 relatórios do ASan**, 375 s. A única falha nessa rodada é de tempo, esperada com instrumentação: `Expr.ThousandLayersWiggleCostPerFrame` mede 18,8 ms/quadro sob ASan contra o teto de 16 ms (0,86 ms no Release). UBSan não existe no MSVC — não rodado; clang-tidy (§139) não rodado nesta frente.
- **Android:** `assembleDebug` x86_64 compila; `SaveAndErrorsTest` 2/2 (ver G8-25).

## Projeto extremo (§127) — medido

`Stability.ExtremeProjectOpensSavesAndRenders` (host, Release, GPU do host): **510 camadas** (100 vídeos sintéticos 64×36, 60 textos + 20 pré-composições de 2 textos, 20 partículas, 120 formas, 20 vetores, 20 nulos, 150 legendas agrupadas de 600 palavras reais via `create_captions`), **21 composições**, **5 160 keyframes**, **50 efeitos**. Três rodadas:

| Medida | Faixa |
|---|---|
| Montar | 79–113 ms |
| 1 ação pela UI (keyframe + snapshot de desfazer) | 0,43–0,72 ms |
| Salvar: lock do modelo | 3,1–5,7 ms |
| Salvar: escrita + fsync + `.bak` (sem lock) | 12,1–26,9 ms (0,73 MB) |
| Abrir | 4–6 ms |
| Render 1920×1080 offscreen, 30 quadros | média 20,8–27,9 ms, pior 62–91 ms |

Nenhum crash; reabrir confere camadas, composições e as 20 pré-composições apontando para composições vivas. É host, não celular: serve de linha de base para a frente de profiling (8A), não como número de aparelho.

## Autosave (§55–57) — como está

- Kotlin grava o projeto **inteiro** quando está sujo e parado (3 s sem mudança, sem gesto aberto, sem tocar/scrub); nunca durante arrasto. Roda em `Dispatchers.IO`; a main só dispara. Depois de uma falha espera 30 s e avisa uma vez.
- Cada gravação: encode sob lock (medido acima) → escrita atômica com fsync → `.bak` da anterior.
- Recuperação após force kill / queda: o último autosave completo é o principal; queda no meio da escrita deixa o anterior intacto (e, se já tinha feito fsync, o `.tmp` completo, que a abertura prefere).
- O journal de comandos (`append_journal`) existe e foi endurecido, mas não está ligado; o formato não tem gravação incremental por seção (`incremental_save_implemented() == false`, declarado no código).
