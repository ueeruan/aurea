# Limpeza do Aurea antigo — manifesto

Registro técnico da Fase 3: o que saiu do projeto antigo, o que ficou para trás
e como se provou que o Aurea novo não depende dele. Data da auditoria e do
build isolado: **21/09/2026**.

| | |
|---|---|
| Pasta antiga | `C:\Users\SnyX\Documents\Projetos - Claude\Aurea` (Flutter, release Beta A.01 `1.2.1-beta-a01`) |
| Remotos do git antigo | `origin` = github.com/ruanpablo9928-sys/aurea · `ueeruan` |
| Tamanho medido | **50,49 GB** (robocopy) · 51.721 MiB (du) |
| Script de limpeza | [`limpar_aurea_antigo.ps1`](limpar_aurea_antigo.ps1) — ensaio por padrão; apaga só com `-Executar` |
| Quem apaga | o dono (apagar é permanente; o agente só prepara e valida) |

## 1. Classificação (A–F)

| Classe | O quê | Tamanho | Destino |
|---|---|---|---|
| **A — precisa ser preservado** | keystore de assinatura, IDs do app, logos/ícones/splash, prints aprovados, commits só-locais | — | **Nada disso depende da pasta antiga.** Keystore já é externa (§3); o resto está no repo novo (§2) |
| **B — já foi migrado** | UI A.01 (reescrita em Compose), Motion Tile (reescrito no motor C++), fonte CupertinoIcons, marca, miniaturas dos modelos, `ExportOptions.plist`, prints `t1/t2/t3/tela96/tela96b` | ~3 MB úteis | repo novo |
| **C — build/cache regenerável** | `tmp/` 12,0 GB · `.dart_tool/` 10,8 GB · `build/` 8,6 GB · `.tooling/` 3,2 GB · `android/.gradle`, `android/app/.cxx`, `android/build` · `packages/*/third_party`, `packages/*/.cxx` · `servidor/comunidade/.wrangler` · `__pycache__` · `flutter_0*.log` · `aurea-release.apk` (194 MB, build antiga `1.0.0-beta.3`) | ~36 GB | apagar |
| **D — código antigo descartado** | `lib/` (Dart), `packages/` (renderer antigo, Filament, whisper…), `native/`, `servidor/`, `test/`, `tool/`, `ios/`, `android/` (casca Flutter) · histórico `.git/` 11,8 GB | ~13 GB | apagar — **tudo versionado** nos remotos (§6) |
| **E — asset duplicado** | `assets/` (29 MB): os oficiais já estão em `_identity/` e em `android/app/src/main/res`; o resto está no git remoto | 29 MB | apagar |
| **F — desnecessário** | `.claude/` 1,5 GB (worktrees e config de agentes antigos), `.idea/`, `*.iml`, `local.properties` (só caminhos da máquina) | ~1,5 GB | apagar |
| **decisão do dono** | `output/` 1,2 GB — renders, demo "floresta-mágica", narração do tutorial da cena 3D. **Não está no git.** | 1,2 GB | apagar, ou mover com `-ManterOutput <destino>` |

## 2. O que foi preservado e onde

| Item | Onde está no repo novo |
|---|---|
| IDs (`com.aurea.aurea` Android/iOS), nome, versão de origem, decisão do `minSdk 26` | [`_identity/signing/IDENTIDADE.md`](../../_identity/signing/IDENTIDADE.md) |
| `ExportOptions.plist` (distribuição iOS) | `_identity/signing/ExportOptions.plist` (idêntico ao original) |
| Ícone, ícone adaptativo, monocromático, splash, cores | `_identity/branding/` e `android/app/src/main/res/` |
| Fonte de ícones da UI (CupertinoIcons 1.0.9, MIT) | `android/app/src/main/res/font/cupertino_icons.ttf` + [`docs/licenses/CupertinoIcons-MIT.txt`](../licenses/CupertinoIcons-MIT.txt) |
| Miniaturas dos modelos da Home | `android/app/src/main/res/drawable*/modelo_*.jpg` |
| Prints aprovados da A.01 (mesmo SHA-256 dos originais) | `docs/migration/ui_reference/13–17` (`t1`→13, `t2`→14, `t3`→15, `tela96b`→16, `tela96`→17) + 01–12 |
| Especificação da UI A.01 (medidas, cores, textos, gestos) | `docs/migration/ui_spec/01–04` |
| Commits que só existiam no disco | `docs/migration/git/aurea-antigo-so-local.bundle` (§6) |

## 3. Assinatura

- O Aurea oficial sempre foi assinado com a **debug keystore da máquina**
  (`%USERPROFILE%\.android\debug.keystore`), SHA-256
  `55:BF:3C:C8:…:52:B5:C8` — mesma impressão do APK distribuído
  (auditoria completa em `IDENTIDADE.md`).
- **Não existe** `*.jks` nem `key.properties` na pasta antiga. A keystore fica
  onde está — fora da pasta antiga e fora do repo — e **não foi movida**.
- `gradlew :app:signingReport` com a pasta antiga desativada resolve as
  variantes `debug` e `release` para essa keystore (validade até 19/08/2056).
- Se um dia houver chave de release, o `build.gradle.kts` já lê
  `key.properties` (fora do git) e usa a de debug só na falta dela.

## 4. Motion Tile

Único efeito portado; reescrito no motor novo (não copiado).

| Verificação | Estado |
|---|---|
| Lógica/shader | `engine/src/effects/builtin/MotionTileEffect.cpp`, `engine/shaders/effects/motion_tile.frag`, geometria em `engine/include/aurea/effects/MotionTile.hpp` |
| Parâmetros | centro, largura/altura do mosaico, largura/altura da saída, bordas espelhadas, esticar bordas, fase, fase horizontal |
| Regra que custou caro no antigo | a cópia central é a própria layer (mesmo tamanho e lugar); saída e cobertura automática crescem **para fora** — travada por teste |
| Bordas (mirror/clamp), tile, phase, output width/height | cobertos |
| Testes | 30 (21 de CPU/render + 9 de GPU Vulkan real) — inclui os cenários de teste do projeto antigo |
| Golden frame | `engine/tests/golden/motion_tile.png` |
| No aparelho | adicionado pela galeria, parâmetros ao vivo, reaberto do disco com o efeito intacto (emulador) |

## 5. UI

- UI aprovada = **Beta A.01 (release)**, escolha do dono. Portada para
  Jetpack Compose (Home, editor, timeline, painéis, galeria de efeitos,
  controles de propriedade), ligada ao motor novo: o motor é a única fonte de
  verdade; a UI lê instantâneos por revisão e escreve por comandos.
- Nenhum arquivo Dart/Flutter, renderer, timeline ou efeito antigo foi trazido.
- Bugs corrigidos nesta fase (além dos da spec 01–04): cor de efeito e fundo
  saíam mais claras (sRGB × linear); trocar fps cortava o vídeo (agora preserva
  segundos); tamanho acima do teto do aparelho era recusado em silêncio;
  duplicar deixava o original escolhido (a lixeira apagava o original); aba
  Mídia com blocos colados; perda de edição se o processo morresse em primeiro
  plano (autosave 3 s após a última mudança).

## 6. Git

- Todo commit das branches locais já existe em `origin`/`ueeruan`, **exceto**
  `ui-nova` (5), `3d-diligent` (1), a tag `ui-nova-aprovada` e 1 `stash`.
- Esses quatro refs estão em `docs/migration/git/aurea-antigo-so-local.bundle`
  (92 KB, `git bundle verify` = ok, SHA-256
  `d2a210c267fdbe79dfc2f11da07c6dfc3879aa0558cc38aca5af797f84567155`).
  É um bundle "fino": depende dos commits do `origin`. Para restaurar:
  `git clone <origin> && git fetch <bundle> 'refs/*:refs/restaurado/*'`.
- O script recusa apagar se aparecer qualquer commit só-local novo fora do
  bundle.
- `.gitignore` do repo novo cobre build, `.gradle`, `.cxx`, `DerivedData`,
  `Pods`, `cmake-build-*`, `out`, `dist`, `cache`, `.cache`, `tmp`, `logs`,
  proxies, miniaturas, render cache, `*.apk`, `*.aab`, `*.ipa`.

## 7. Dependência do caminho antigo

Busca global no repo novo (caminho absoluto, `AureaAntigo`, `Aurea_DISABLED`,
Gradle/CMake/asset/shader/include/library paths): **0 dependências**. O
caminho antigo só aparece em texto de documentação (`IDENTIDADE.md`, specs,
este arquivo). Nenhum symlink nem junction no repo.

## 8. Build e teste isolados (pasta antiga renomeada para `Aurea_DISABLED`)

| Etapa | Resultado |
|---|---|
| Motor C++: configure + build do zero em diretório novo | 95 s; **252 testes, 528.046 verificações, 0 falhas** (inclui GPU Vulkan e Motion Tile) |
| Android: `clean`, sem build cache, `--rerun-tasks`, `.cxx` apagado | BUILD SUCCESSFUL (nativo recompilado do zero) |
| Assinatura (`signingReport`) | debug e release → `~/.android/debug.keystore` |
| APK no emulador (API 34 x86_64) | abriu, reabriu projeto do disco, tocou 3,3 s com decode de vídeo e keyframes interpolados, **0 crash** |

Depois a pasta voltou ao nome original para o dono decidir a exclusão.

Fluxo completo de UI testado no emulador (antes do isolamento, mesmo código):
criar projeto, importar imagem e vídeo, selecionar, multisseleção, mover no
palco e na timeline, aparar, dividir, reordenar, girar, escalar, keyframes,
efeito (Motion Tile), desfazer/refazer, duplicar, apagar, play/pause, scrub,
⚙ Projeto (fps, proporção, fundo), salvar, fechar, matar o processo, reabrir.
**0 crash.**

Não validado: aparelho Android físico (nenhum conectado) — o caminho
zero-copy (AHardwareBuffer) só roda de verdade em hardware; o emulador usa os
planos pela CPU.

## 9. Tamanho

| | |
|---|---|
| ANTES | **50,49 GB** |
| PRESERVADO (no repo novo) | ~3,2 MB (`_identity` 1,1 MB + prints 2,0 MB + bundle 92 KB) |
| REMOVIDO | 50,49 GB (ou 49,3 GB guardando `output/`) — quando o dono rodar o script |
| DEPOIS | 0 (a pasta deixa de existir) |

Espaço livre em C: antes da limpeza, 445,8 GB.

## 10. Cache do Aurea novo

- Motor: `cacheDir/motor` (pipeline Vulkan, temporários). Miniaturas de
  projeto: `filesDir/projetos/.miniaturas` (as órfãs saem na limpeza).
- "Limpar cache" em **Ajustes** apaga o cache do motor e as miniaturas de
  projetos que não existem mais, e diz quanto liberou.
- Builds do host e do Android saem em `build/` na raiz (ignorado); nada de
  build dentro de `android/` além do `.cxx` do CMake (ignorado).

## 11. Como rodar a limpeza

```powershell
# 1. ensaio (confere as travas e mede; não apaga)
powershell -ExecutionPolicy Bypass -File .\docs\migration\limpar_aurea_antigo.ps1
# 2a. apagar tudo
powershell -ExecutionPolicy Bypass -File .\docs\migration\limpar_aurea_antigo.ps1 -Executar
# 2b. ou guardar output/ antes (é movida, não copiada)
powershell -ExecutionPolicy Bypass -File .\docs\migration\limpar_aurea_antigo.ps1 -ManterOutput "D:\Aurea-output-antigo" -Executar
```

Ensaio rodado em 21/09/2026: as 5 travas passaram, alvo 50,49 GB.

## 12. Encerramento

**PHASE 3 — COMPLETE** (21/09/2026, aprovada pelo dono). Revalidado no
fechamento: 0 dependências do caminho antigo em código/build/assets (só texto
de documentação), 0 symlinks; motor 252 testes 0 falhas; Android clean build
sem cache e com `.cxx` apagado OK; testes JVM da timeline 27, 0 falhas.

A exclusão física da pasta antiga fica com o dono, pelo script da §11 (o
Aurea V2 não depende dela). O projeto antigo não é mais consultado a partir
daqui.
