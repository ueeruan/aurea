# Proposta de limpeza do AUREA — 13/10/2026

**Estado: nenhum arquivo apagado.** A revisão automática exigiu aprovação específica para esta lista de exclusões permanentes.

A proposta contém 68 caminhos existentes, todos verificados como não rastreados pelo Git. Na pasta principal são 85.08 GiB lógicos; nos demais worktrees, aproximadamente 35.57 GiB. O espaço físico recuperado poderá ser menor porque imagens de emulador são esparsas.

## Preservado

- Código e alterações Git, inclusive arquivos novos, históricos e todos os checkouts/worktrees.
- `_identity`, keystores, credenciais de assinatura e configurações locais.
- Dependências FFmpeg instaladas, third_party, assets e fixtures de origem.
- Projetos pessoais `.aurea`/`.aureaproj`, mídias de origem, vídeos exportados e notas.
- APK final da timeline e entregas atuais em `output/build-2183` e `output/build-2182`.
- Evidências/capturas/relatórios REA. Serão removidos apenas os dados do emulador temporário e APKs de instrumentação duplicados listados abaixo.
- Backup `Aurea-Backup-2026-09-24` e os dois AVDs em `.android/avd`.

## Exclusões propostas

| Caminho absoluto | Tamanho lógico na pasta principal |
| --- | --- |
| `C:\Users\Ruan\Documents\Aureabeta\engine\build` | 45.762 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\build\android` | 5.067 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\android\app\.cxx` | 5.969 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\.claude\worktrees\agent-a29d0154df0988e64\engine\build` | 0.692 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\.claude\worktrees\agent-a486e05843be197f3\engine\build` | 0.649 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\android\.gradle` | 0.098 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\android\.kotlin` | 0.000 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\android\build` | 0.001 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\build\astra-ffmpeg\objects` | 0.742 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\build\prompt03\emulator-recovery` | 3.644 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\build\reference\alight-motion-5.0.280\Aurea_AM_Ref.avd` | 9.398 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\build\reference\alight-motion-5.0.280\avd-registry` | 0.000 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\build\reference\keyflow-ui-trial-20261009\Keyflow_Play_API35.avd` | 8.297 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\output\iphone17-motion-15s\movie.rgba` | 3.476 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\output\hera-motion-aurea\render.rgba` | 1.127 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\build\reference\alight-motion-5.0.280\aurea-validation\aurea-uitest-x86_64.apk` | 0.152 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\build\reference\alight-motion-5.0.280\aurea-validation\aurea-instrumentation.apk` | 0.003 GiB |
| `C:\Users\Ruan\Documents\Aureabeta\build\reference\alight-motion-5.0.280\aurea-validation\aurea-instrumentation-final.apk` | 0.003 GiB |
| `C:\Users\Ruan\Documents\Aureabeta-015\build\android` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-015\android\app\.cxx` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-015\android\.gradle` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-015\android\.kotlin` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-015\android\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-015\engine\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-astra\build\android` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-astra\android\app\.cxx` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-astra\android\.gradle` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-astra\android\.kotlin` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-astra\android\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-astra\engine\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2adj\build\android` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2adj\android\.gradle` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2adj\android\.kotlin` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2adj\android\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2adj\engine\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2export\build\android` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2export\android\.gradle` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2export\android\.kotlin` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2export\android\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2export\engine\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2fx\build\android` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2fx\android\.gradle` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2fx\android\.kotlin` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2fx\engine\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2surf\build\android` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2surf\android\app\.cxx` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2surf\android\.gradle` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2surf\android\.kotlin` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-b2surf\engine\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-base017\engine\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-cc\build\android` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-cc\android\.gradle` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-cc\android\.kotlin` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-cc\engine\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-rel\build\android` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-rel\android\app\.cxx` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-rel\android\.gradle` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-rel\android\.kotlin` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-rel\engine\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-voz\build\android` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-voz\android\.gradle` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\Documents\Aureabeta-voz\android\.kotlin` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\.codex\worktrees\astra-015-current-base\Aureabeta\build\android` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\.codex\worktrees\astra-015-current-base\Aureabeta\android\app\.cxx` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\.codex\worktrees\astra-015-current-base\Aureabeta\android\.gradle` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\.codex\worktrees\astra-015-current-base\Aureabeta\android\.kotlin` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\.codex\worktrees\astra-015-current-base\Aureabeta\android\build` | Medição externa incluída no total aproximado |
| `C:\Users\Ruan\.codex\worktrees\astra-015-current-base\Aureabeta\engine\build` | Medição externa incluída no total aproximado |

## Verificação

As fontes rastreadas foram registradas por SHA-256 antes da limpeza; elas serão verificadas novamente após a exclusão. A rotina valida o caminho absoluto, o confinamento a cada raiz e ausência de reparse points nos ancestrais, recusa arquivos rastreados e registra cada resultado. Não fecha apps nem encerra processos de outros trabalhos.

Candidatos: `proposed-directories.json`. Script preparado: `remove-generated.ps1`. O modo `-PlanOnly` somente audita. A execução sem esse parâmetro permanece pendente de autorização para a lista acima.
