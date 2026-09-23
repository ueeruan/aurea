# Fase 8.1 + 8.2 — onde parou (passagem de sessão)

Master em `8da7427`: keyframes nos parâmetros do Particular e contrato v21
(70 `ParticleParam`, enums e campos novos, gravação v21). Suíte: 564 testes, 0 falhas.

## Frentes em andamento (cada uma na própria worktree/branch, a partir de 8da7427)

| Frente | Branch | Escopo |
|---|---|---|
| A — idiomas | `worktree-agent-ab50db9757e9d6823` | ~1376 literais de tabelas de dados → recurso (7 idiomas); auditoria LTR (timeline, curvas, gizmos, X/Y/Z); testes de shaping árabe/devanágari e nome de projeto Unicode |
| B — emissão/aparência | `worktree-agent-a3d6f3accc69cf62f` | emissores Layer/Text/Path/Mesh; partícula Texture/Mesh instanciada; gradiente e curvas ao longo da vida; colisão esfera/caixa; painel + keyframes; presets; benchmark 10K–1M |
| C — 3D | `worktree-agent-a256aa5de592bf86f` | partículas no passe da cena 3D (depth, câmera, Null/nulo rastreado); espaço mundo/local com histórico de nascimento; motion blur por subamostra de tempo |

## Como fechar

1. Em cada branch: `git log master..<branch>` para ver os commits; o relatório de cada frente está na mensagem do último commit.
2. Merge na ordem A → B → C (B e C tocam `particles.vert` e o `prepare` do Renderer; foram instruídas a isolar o código em funções/arquivos próprios). Conflitos: resolver com regex ancorada em início de linha.
3. `cmake -S . -B build/host` (arquivos novos), build Release, `aurea_tests.exe` inteiro (0 falhas), `assembleDebug`.
4. Emulador: SÓ em projeto novo (o dono usa o emulador): troca de idioma pt-BR → ar → ru → hi → en → es → id → pt-BR; em árabe: criar projeto, importar, timeline, pinça, efeitos, keyframes, curvas, 3D, Null 3D, export, salvar/reabrir.
5. Fechar com números medidos. Metal/iOS: não testável sem Mac — declarar.

## Estado ao fim da sessão (2026-09-22)

- **A (idiomas) — MERGEADO no master.** 570 testes, 0 falhas. Entrou: `test_i18n.cpp` (árabe RTL com formas contextuais, devanágari em cluster, cirílico, nome/caminho Unicode salva e reabre), correção real do `fopen` UTF-8 no Windows (`FileIO::open_file`), `AppText.kt`, `tools/i18n_count.py` (867 literais visíveis restantes, 59 arquivos) e `tools/i18n_check.py` (852/849 chaves, 0 problemas de formato). Rascunho da conversão Kotlin que não fechou: `docs/phase8/wip/wip_kotlin_i18n.patch` (inclui a correção de `Element3DPanel.kt:164`). Falta: converter os 867 literais, auditoria RTL, limpar `panel_1_to`/`panel_2_to`/`editor_else`.
- **B (emissão/aparência) — NÃO mergeado**, branch `worktree-agent-a3d6f3accc69cf62f` (commit 08f098a): shader em funções + `particles_extras.glsl` (binding AUREA_DATA) + `ParticleExtras.cpp` (pontos de emissão de imagem/texto/forma/vetor/máscara/modelo, malha da partícula, curvas, aleatórios, colisão esfera/caixa). Falta ligar no Renderer, API/JNI/painel, testes e benchmark.
- **C (3D) — NÃO mergeado**, branch `worktree-agent-a256aa5de592bf86f` (commit 6172ca9): binding 16 para histórico, funções `ps_*` no shader (histórico no nascimento, taxa animada por aceitação, Z/billboard, subamostra de tempo), desenho na cena com depth. Falta `ParticleScene.cpp`, upload do histórico, entrar no grupo de cena (`asPlane`), motion blur, testes (a)–(h). ATENÇÃO: `Renderer.hpp` declara 3 funções ainda sem corpo.
- **B e C conflitam em `particles.vert`** (ambos isolaram em funções: B `emit_*`/`collide_*`, C `ps_*`/`emit_vertex`) — mergear B primeiro, depois C resolvendo o `main`.
- **Bug achado (B):** projeto anterior à v20 com Faíscas/Neve/Poeira abre com emissor 0 (Ponto); o antigo era sempre Caixa → corrigir na leitura v<20.

## Atualização 2026-09-23 — escopo reduzido pelo dono

- **8.1 FECHADA (1511a94): só pt-BR + inglês.** Ajustes > Idioma = Sistema / Português / English; APK só com `pt`/`en` (`androidResources.localeFilters`); sistema em outro idioma abre em inglês e LTR. Catálogos es/ru/hi/id/ar ficam no repositório para uma fase futura — não editar agora. Restam 328 literais visíveis em tabelas de rótulo (TransformPanel, VectorPanel, TextAnimSection, PresetsPanel, DeviceReport…): próximo passo quando voltar a idiomas.
- **8.2:** B e C mergeados (1dd76a7, 05b2ab6). Ligação em andamento em duas worktrees novas: B2 (Renderer ← `particles::build_frame`, textura/malha, API/JNI/painel, testes, benchmark) e C2 (`ParticleScene.cpp`: histórico, mundo/local, cena 3D com depth, motion blur, testes). Merge: B2 → C2, resolvendo o ponto de desenho uma vez; suíte inteira só no fim.
