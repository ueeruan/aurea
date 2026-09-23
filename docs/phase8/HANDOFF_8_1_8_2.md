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
