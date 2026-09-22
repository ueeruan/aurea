# Fase 8 — Relatório de performance

(Cada frente escreve só a sua seção.)

## 8I — Release e abertura

### Onde foi medido (§4, §159)
- **Host:** Windows 11 Pro 22631, AMD Ryzen 5 5500, 32 GB, NVIDIA RTX 3050 (Vulkan real, os MESMOS shaders SPIR-V e o MESMO backend do Android). Teste `engine/tests/test_startup.cpp` (suíte `Startup`), mediana de 5 rodadas, 3 execuções.
- **APK:** `assembleRelease` (arm64-v8a + armeabi-v7a, assinado com a chave de debug — não há `key.properties` na árvore de trabalho), auditado com `dexdump`/`llvm-readelf` do SDK/NDK.
- **Não medido aqui (declarado, não fingido):** abertura a frio/quente no celular, tamanho instalado real, tempo de compilação de pipeline em Mali/Adreno. Não há aparelho físico na bancada e a regra desta frente era não usar adb/emulador. No host o driver da NVIDIA tem cache de shader próprio em disco, então "a frio" = sem o cache do Aurea; no celular cada pipeline compilado a frio custa tipicamente dezenas de ms, e é aí que o corte de pipelines na abertura aparece de verdade.

### Abertura do motor (§59–62)

| Host, mediana de 5 | Antes | Depois |
|---|---|---|
| Pipelines compilados antes do 1º quadro | **40** (todos os efeitos + todo o 3D + export) | **16** (composição, vídeo, forma, vetor, texto, máscara, pilha de cor, saída) |
| Renderer na abertura fria (shaders + pipelines) | 13,8 ms (13,0–18,3 em 3 execuções) | 9,0 ms (7,7–9,1) |
| Renderer na abertura quente (cache do Aurea no disco) | 3,4 ms (2,8–4,2) | 2,5 ms (2,2–2,6) |
| `Engine::initialize` fria / quente | 64,9 / 56,7 ms | 57,0 / 50,8 ms |
| Backend (instância + dispositivo Vulkan) | ~51 ms | ~47 ms (não é código do Aurea; varia com a carga da máquina) |
| 1º quadro de projeto novo (forma + texto) | 8,6 ms | 7,1–11,4 ms (sem pipeline novo: os dois estão no conjunto quente) |

- O que saiu da abertura, medido no MESMO processo: 12 pipelines de efeito = 2,1–2,6 ms a frio no host; os 11 do 3D entram no 1º quadro parado depois da primeira camada 3D (1º quadro com 3D: 6,6–10,1 ms no host, compilando os 11).
- `Engine::startup_timings()` + uma linha de log por abertura: `abertura do motor: X ms (aparelho, gpu, renderer com N pipelines, resto)` — a abertura no celular passa a ser medida pelo logcat, sem estimar.

**Preguiçoso, fora do playback (§60):** fora do playback contínuo (parado, scrub, export) o renderer varre o projeto inteiro (efeitos ligados e camadas 3D de TODAS as composições, não só o instante) e compila o que falta antes de pegar a imagem da swapchain. No playback não varre (nada muda no modelo sem pausar). `compiles_since_mark()` continua 0 no playback (teste `ProjectPipelinesWarmWhenUsedNotAtOpen`: desfoque + 3D aquecidos no 1º quadro parado, 5 quadros depois sem compilar nada). 3D, optical flow, partículas, rastreio e legendas não criam nada na abertura: o 3D sobe só texturas neutras de 1 px; os outros nascem no primeiro uso (conferido no `Engine::initialize` e no `EditorStore.init`).

**Cache de pipeline persistente (§62, §176):** o arquivo agora é cabeçalho nosso + blob do driver: versão = impressão digital FNV-1a de todo o SPIR-V embutido (~330 KB, calculada uma vez), fornecedor, dispositivo, versão do driver, UUID, tamanho e soma. Antes de entregar ao driver também confere o cabeçalho Vulkan dentro do blob (drivers de celular com defeito caem com blob ruim). Truncado, corrompido, de outra GPU/driver, formato antigo ou app atualizado → apagado e recompilado. Marca `.carregando` gravada antes de entregar o blob: se sobreviveu, a carga anterior derrubou o processo e o cache é apagado sem ser lido (sem loop de crash na abertura). Gravação atômica (tmp → fflush/fsync → rename) e só quando entrou pipeline novo (ir para segundo plano deixou de regravar ~600 KB toda vez). Teste `CorruptPipelineCacheIsDiscardedAndRebuilt`: byte trocado, truncado, formato antigo, lixo, versão trocada e marca de crash — em todos o motor sobe, o arquivo é refeito e a abertura seguinte o aceita.

### Abertura do app (inspeção do código, §59–63)
O que roda até a Home, na ordem:
1. `MainActivity.onCreate`: edge-to-edge, `setContent`. `EditorStore` (ViewModel) no construtor: `AureaEngine.create` (carrega a .so), `EffectPrefs`, `ThumbnailCache`; `refreshProjects()` em `Dispatchers.IO`.
2. Thread `aurea-ciclo`: `DeviceProfile.probe` (tabela de codecs só na 1ª abertura ou SO novo; depois prefs), `engine.initialize` (acima), superfície pendente.
3. Main, com o motor pronto: `deviceReport`, listener térmico, `readCatalog` (1 JNI, 47 efeitos), `EffectPreviewStore` (agora só um objeto), laço de status.

Tirado da abertura: **decodificação da foto das prévias** (JPEG 640² + cópia de 1,6 MB para o motor, no main thread, em toda abertura) → carregada na 1ª prévia, na fila de render das prévias; **`CaptionsState`** (cofre da chave Groq) e **`PresetLibrary`** (listagem das 5 pastas de presets + 2 SharedPreferences, no main thread) → `by lazy`, no primeiro uso.

### Navegador de efeitos (§63)
- Já era sob demanda (cada cartão pede a sua ao entrar na tela). Concorrência NÃO era limitada: cada prévia ia para `Dispatchers.Default` e até N núcleos ficavam presos esperando o mesmo mutex de render do motor. Agora: fila de UMA (`limitedParallelism(1)`); cartão que sai da tela antes da vez é cancelado sem custo.
- O comentário prometia cache em disco e ele não existia: toda abertura do navegador refazia tudo. Agora memória → disco (`cache/motor/previas/<instalação>/…webp`, WebP 90: ~20 KB por prévia contra ~127 KB em PNG) → GPU. App atualizado = pasta nova, a antiga apagada. "Limpar cache" renomeia na hora e apaga em segundo plano.
- Custo que o disco evita (host): 44 prévias de 47 efeitos = 425–559 ms de GPU na 1ª vez (31 pipelines compilados), 396–529 ms nas seguintes sem o disco. Com o disco, da 2ª abertura em diante: zero prévias na GPU.
- A foto: 640 → 320 px (o cartão é 320 × 200 e o corte usa a largura toda, 1:1), 117 → 42 KB; textura e bitmap 4× menores.

### Build release e tamanho (§180–183, §187–190)

| APK release (arm64 + armv7) | Antes | Depois |
|---|---|---|
| **APK** | **24.208.966 B (23,09 MB)** | **11.548.193 B (11,01 MB)** |
| dex (no APK / cru) | 11,5 MB / 43,0 MB (3 dex) | 1,55 MB / 2,98 MB (1 dex) |
| libaurea.so arm64 / armv7 | 4,75 / 3,78 MB | 3,81 / 3,05 MB |
| entradas na `.dynsym` da .so (arm64) | 4.348 (o motor inteiro exportado) | 555, das quais 252 definidas: 179 `Java_*`, `JNI_OnLoad` e a API pública do zstd |
| res / resources.arsc | 66 itens 409 KB / 415 KB | 24 itens 324 KB / 119 KB |
| assets | foto 117 KB + presets 1,2 KB | foto 42 KB + presets 1,2 KB |

- **R8 ligado** (`isMinifyEnabled`, `isShrinkResources`). O dex era 44 MB cru por causa do `material-icons-extended` inteiro e do Compose sem encolher. `proguard-rules.pro` prende o que o C++ chama por JNI (`AureaEngine.openContentFd`/`decodeImage` estáticos + nativos). Conferido no dex do APK: 179/179 nativos com o nome exato que o C++ exporta, os dois callbacks `PUBLIC STATIC` com a assinatura que o `GetStaticMethodID` pede. `mapping.txt` sai em `build/android/app/outputs/mapping/release/` (guardar junto com o APK publicado).
- **`-fvisibility=hidden`** no build nativo do Android: a .so exportava o motor inteiro (~400 KB de `.dynsym`/`.dynstr` por ABI) e toda chamada entre funções do motor passava pela PLT. `--gc-sections` e `-ffunction-sections` já vinham do NDK.
- Sem assets de teste na release (assets = foto + 4 JSON de presets; res = ícone, splash, fonte de ícones). Os 4 `modelo_*.jpg` (73 KB) não tinham referência e saíram.
- **Instalado (não medido — estimativa):** a .so não é extraída (`useLegacyPackaging = false`), então o instalado ≈ APK (11,0 MB) + código do ART (perfil de base presente: tipicamente 1–3 MB) ≈ 12–14 MB, mais dados: cache de pipeline ~0,6 MB e prévias ~0,9 MB (44 × ~20 KB). Um aparelho arm64 com APK por ABI (loja/AAB) baixaria ~7,2 MB.
- Build de release é gerável e medível: `./gradlew :app:assembleRelease` (sem `key.properties` usa a chave de debug, que é a mesma do Aurea oficial — ver `_identity/signing`).

### Log e rede (§180–184)
- **Log no motor:** `AUREA_LOG_TRACE/DEBUG` compilam para nada com `NDEBUG` (o Android é `RelWithDebInfo`, que define `NDEBUG`); `log_write` confere o nível ANTES de formatar; buffer na pilha, sem alocação. Auditados os 70 `AUREA_LOG_INFO/WARN`: nenhum por quadro no caminho normal — o do export é a cada 300 quadros; os demais são de falha ou de evento único. **Kotlin:** zero `Log.*`/`println` no app.
- **Rede:** a única chamada é `GroqWhisperProvider.transcribe` (HTTPS para a Groq), feita só pelo botão "Gerar legendas"/"Transcrever de novo", em `Dispatchers.IO`, com timeouts de 20 s/180 s. Nada de rede na abertura, no playback, no export ou no autosave.

### Código morto e botões falsos (§185–186, §195)
- **Inalcançáveis:** `ParentPanel`, `TransitionsPanel`, `EchoPanel` (nenhum botão abria `EditorPanel.Parent/Transitions/Echo`; "Seguir camada" vive na barra do topo, e o eco virou o efeito "Eco e rastro" com migração do projeto antigo). Junto saiu o `queryEcho` que rodava (JNI + alocação) em todo refresh do detalhe da camada.
- **"Em breve" zerado:** "Qualidade 3D" (Ajustes), o "olho" de opções de visualização (barra de transporte), 16 curvas sem motor (famílias Quique e Outras, 3 "Degraus"; ficam Bézier e Manter) e as ações "Gráfico de velocidade" e "Loop" da curva. `comingSoon()` não existe mais.
- **Restos de Comunidade/Perfil e outras sobras:** glifos (At, CloudFill, LockShield, PlusApp, Person*), `rememberResourceThumbnail`/`decodeResource` (miniatura dos modelos da A.01), `HomeLinkRow`, `SwitchRow`, `TileChevron`, `MaterialChevron`, `GlyphCircle`, `EffectBrowserTile`, `ShellSwitch`, `casasAutomaticas`, `compactBar*`, `effectHuman`, `effectMeta`, `paramTypeLabel`, `selectOnly`, `cancelPointPick`, `parentSelectionToLast`, `showError`, `Engine3D`/`LayerSeconds`/`Quality3D`; 4 drawables `modelo_*`.
- **Permissões sem recurso:** CAMERA, RECORD_AUDIO, REQUEST_INSTALL_PACKAGES, WRITE_EXTERNAL_STORAGE (no Android 8–9 o export já ia para a pasta do app). As de leitura de mídia ficaram (projetos migrados da A.01 podem ter caminho de arquivo).

### Testes
- Motor: **447 testes, 0 falhas** (eram 443; +4 `Startup`).
- Android: `assembleDebug -PaureaAbi=x86_64` e `assembleRelease` (arm64 + armv7) compilam; auditoria JNI do APK acima.

### Para as outras frentes (achado aqui, fora da posse da 8I)
- **8H/8D:** o laço de status (`RenderLoop`, Choreographer) roda a cada vsync desde a abertura, também na Home — `readStatus` por JNI 60–120×/s sem editor aberto.
- **Áudio:** `AudioEngine::initialize` abre o stream AAudio na abertura do motor; poderia abrir no primeiro play.
- **8B/8G:** `MemoryManager` ("orçamento recusou") e `EffectGraph` ("efeito não montou os passes") logam a cada quadro enquanto a falha persistir — sem limite de taxa.
- **8H:** `DeviceProfile.surveyed/hardwareDecoderCount` e `DeviceReport.exportCeiling` sem chamador.
- Espelho JNI: `AureaEngine`/`CommandBatch` têm ~15 wrappers sem chamador (echo, rgb, transição, recuperação de sessão, telemetria…). O R8 já os tira do dex; o C++ correspondente continua na .so.
