# Exportação falhando no Android e no iOS (0.3.0, build 2196)

Data: 25/10/2026. Branch `claude/release-2194`.

Relatos (lista de bugs críticos): erro do codificador H.264 sem como trocar o
codificador; Android travando sempre no mesmo ponto (quadro 302 no Poco X6 Pro
num vídeo só cortado; quadro 46 ou fechando entre 52 e 62; 74 %); clipes
1080p60 na timeline (Poco M8; reexportar o clipe em outro app resolve); iOS
travando ou fechando no meio (iPhone 11 com Ball Action, BCC Glitch ou Roto
Brush; iPhone XR); celulares de até 4 GB, sequência de imagens e export a
200 qps.

Não havia log, projeto nem analytics dos usuários neste repositório (os
relatórios ficam no Worker, com token de administrador). A análise é do código,
com reprodução no emulador onde ele alcança.

Legenda: **[PROVADO]** pelo código ou por teste; **[INFERIDO]** dedução sem
execução que a confirme; **[NÃO COMPROVADO]** depende de dados do aparelho.

## Causas e correções

### 1. Quadro exato que nunca chega: trava sempre no mesmo quadro

- O export só grava um quadro quando a fonte entrega o quadro EXATO
  (`OfflineFrameRenderer::render`). Se o decoder do aparelho não entrega um
  ponto do clipe, o export espera até 30 s sem avanço (ou 120 s no total) e
  falha com `DecodeFailed`. [PROVADO]
- A escada de tentativas classifica isso como "parou" e refaz com outro
  encoder e menos pixels, sempre do quadro 0. Nenhum degrau muda o decoder:
  todos param no MESMO quadro. É o "quadro 302" / "quadro 46" / "74 %". [PROVADO
  pelo código; a causa no aparelho de cada usuário é NÃO COMPROVADA]
- Correção (motor, vale para os dois):
  1. Sem avanço do decoder por 8 s, a fonte de quem espera é reaberta (uma vez
     por quadro): codec novo e seek novo.
  2. Sem o exato depois de 16 s sem avanço (ou 45 s no total), sai o quadro mais
     próximo que a fonte tem, **só do mesmo trecho do clipe** (a regra do
     "fantasma do corte" continua valendo). Logo depois da entrada de um clipe —
     o caso do corte — o vizinho à frente também vale, só no export e só depois
     do prazo (`RenderSettings::approximateAhead`).
  3. Depois do 1o aproximado os prazos caem (3 s / 6 s / 15 s).
  4. No máximo 3 aproximados seguidos: decoder morto de vez continua falhando
     como antes (sem cauda congelada no arquivo).
  5. As telas mostram o aviso que já existia ("Alguns quadros do vídeo original
     não puderam ser lidos a tempo; o quadro mais próximo foi usado").
- Testes do motor: `Export.UndecodableFramePointUsesNearestFrameAndFinishes`
  (o quadro 12 nunca chega, a 0,4 s da entrada do clipe: o export termina com
  24/24 e o aviso) e `Export.DeadDecoderStillFailsAfterAFewApproximateFrames`.
  `Export.DecoderStallFailsBeforeAnyApproximatePictureIsEncoded` segue passando.

### 2. Sequência de imagens não exporta no app de produção (Android)

- A cópia congelada do export grava a PASTA dos quadros com o caminho absoluto
  do app (`/data/user/0/com.aurea.aurea/files/projetos/Media/sequences/…`). O
  resolvedor de caminhos antigos do pacote `com.aurea.aurea` exigia **arquivo**
  regular e devolvia "" para a pasta: o decoder da sequência nunca abria e todo
  degrau parava no 1o quadro dela. [PROVADO]
- Só o APK de sideload (`com.aurea.aurea`); Play (`com.aurea.editor`), debug e
  uiTest não passam por esse resolvedor. iOS não é afetado.
- Correção: `document_asset_path` aceita pasta existente (`Engine.cpp`).
- Teste: `ImageSequence.AndroidReleaseAbsoluteFolderResolves`.

### 3. Memória: iPhone 11/XR e celulares de até 4 GB fecham no meio

- O renderizador do export é outro objeto e nascia com a regra de aparelho
  grande: Roto Brush com 48 + 64 MB de cache e refino em 1920 px, pool de
  texturas sem teto. [PROVADO]
- Os avisos de memória do sistema (Android `onTrimMemory`, iOS aviso/polling)
  só soltavam o renderizador da prévia; o do export (pool, recortes do Roto,
  redes, texturas de imagem) nunca. [PROVADO]
- Correção (motor):
  - aparelho de pouca memória: o export usa o modo de pouca memória do Roto
    Brush (24 + 32 MB, refino em 1280) e um teto do pool igual ao orçamento do
    processo — um quadro acima dele falha com falta de memória (a escada tenta
    com menos pixels) em vez de o sistema matar o app;
  - aviso de memória durante o export: entre dois quadros, com os quadros em voo
    já entregues, o renderizador do export solta o que se refaz.
- Quando o sistema mata o app no meio (sem chance de a escada agir), o PRÓXIMO
  export no Automático começa no degrau de falta de memória, uma vez, com aviso:
  Android pelo histórico de saídas do sistema (`ApplicationExitInfo`: crash,
  falta de memória ou sinal, com a etapa `PROJECT_EXPORT_VIDEO`); iOS por uma
  marca de "export em andamento" no `UserDefaults`. No iOS fechar o app à mão no
  meio do export também conta (o sistema não diz o motivo).
- Não reproduzido: jetsam do iOS e LMK do Android dependem do aparelho.
  [NÃO COMPROVADO no aparelho]

### 4. Validação depois dos 100 % confundida com travamento

- Depois de gravar, o arquivo inteiro é decodificado de novo para conferir. A
  mensagem ficava parada em "validando" e as duas telas cancelam após 180 s sem
  mudança de quadro nem de mensagem: um export longo ou a 200 qps, já pronto,
  virava "travou" e a escada refazia tudo. [PROVADO pelo código; o tempo da
  validação no aparelho é INFERIDO]
- Correção: a validação conta os quadros conferidos e o progresso mostra
  "validando N/M" (`ExportOutputValidation::validatedFrames`).

### 5. 200 qps

- Nada recusa 200 (o teto é 240), mas nenhum encoder de hardware se anuncia
  para 1080p200: o pedido já começa no software. O primeiro degrau que baixava a
  taxa era o 3, junto com 720p. [PROVADO]
- Correção: os degraus 1 e 2 limitam a 60 qps (só muda pedido acima de 60). A
  tela diz "até 60 qps" ao refazer e "exportado a 60 qps" no fim — agora também
  no iOS, que tinha os textos mas não os usava, e também quando a taxa vem do
  projeto.
- Teste: `ExportAttempts.HighFrameRatesDropTo60BeforeAnyPixel`.

### 6. "Erro do codificador H.264, sem como trocar"

- A tela não tinha escolha de codificador; só a escada automática. [PROVADO]
- Correção (Android e iOS, 7 idiomas): Exportar → Avançado → **Codificador**:
  Automático (padrão), Compatível (H.264 básico no codificador do aparelho) e
  Software (H.264 pelo processador). É o degrau em que a escada começa; se
  falhar, segue dali. HEVC só no Automático.

## Validação (25/10/2026)

### Implementação
- Motor C++ (vale para Android e iOS): itens 1 a 5.
- Android (Kotlin): escolha de codificador, avisos de fps pela taxa efetiva,
  export mais leve depois de o app fechar no meio, textos em 7 idiomas.
- iOS (Swift + ponte ObjC++): escolha de codificador, avisos de fps (antes
  ausentes no iOS), export mais leve depois de o app fechar no meio, textos em
  7 idiomas, `exportAttemptFpsCap` na ponte.

### Compilação
- Motor no host (Windows, MSVC): compila.
- Android: APK uiTest x86_64 (Kotlin + nativo) compila. APK e IPA de entrega
  NÃO foram gerados (pedido do usuário).
- iOS: **não compilado** (sem Mac nesta máquina). Checagens estáticas:
  `check_api_swift` 0 problemas (1600 chaves usadas, todas no catálogo),
  `check_export_startup_async`, `check_memory_pressure` e
  `check_effect_contract` passam; os casos Swift assíncronos não rodaram (sem
  swiftc).

### Testes do motor no host (Vulkan, RTX 3050)
- `ExportAttempts` 10/10, `ImageSequence` 14/14, `Hdri` 15/15.
- Filtro `Export`: 227 testes, 18 verificações falhando em 7 testes — os mesmos
  que já falhavam nesta máquina antes (notas 0.2.6/0.2.7): `BetaHeavy`
  TrackText3D…, `MemoryWarningReducesAdmission…`, `ExportSchedulerGpu`
  ProfilesPreserve…, os 3 de `OriginalModelExport` e
  `SwappedTextureAndImagePlanesSurviveMotionBlurInExport` (o PNG do teste não
  é gravado no disco). Os novos e os que tocam o mesmo código passam:
  `UndecodableFramePointUsesNearestFrameAndFinishes` (0,6 s),
  `DeadDecoderStillFailsAfterAFewApproximateFrames`,
  `DecoderStallFailsBeforeAnyApproximatePictureIsEncoded`,
  `MissingVideoFramesAbortInsteadOfReportingBlankVideoAsSuccess`,
  `DeadDecoderStillFailsAsMediaQuickly`,
  `FailedOpeningAndReadyStallKeepTheirOwnShortDeadline` e
  `LateValidationCannotPublishAnAbandonedSession`.
- Android JVM (`testUiTestUnitTest`, diagnósticos e export): 44/44, com o novo
  `ExitDiagnosticsTest.onlyDeathsInTheMiddleOfAVideoExportCount`.

### Emulador (Android 15, x86_64, 4 GB, decoder e encoder de software)

`FieldExportReportsDeviceTest` (novo, APK uiTest), antes e depois das correções:

| Caso | Antes | Depois |
|---|---|---|
| 1080p60 H.264 High, B-frames, cortado no quadro 151, projeto 30 qps | 360/360, 21 s | 360/360, 23 s |
| o mesmo com fps variável (quadros faltando e PTS irregulares) | 360/360, 20 s | 360/360, 23 s |
| 1080p60 VFR num projeto a 60 qps | 720/720, 34 s | 720/720, 33 s |
| saída a 200 qps (720p, 3 s) | 600/600, 13 s | 600/600, 13 s |
| sequência de 90 PNG 1080p | 113/113, 7 s | 113/113, 7 s |

Nenhum caso precisou de quadro aproximado. O emulador não reproduz os relatos:
decoder e encoder são de software e o pacote de teste não passa pelo resolvedor
do item 2. Os casos ficam como regressão.

### Não comprovado
- Nada foi validado em aparelho físico (Poco X6 Pro, Poco M8, Huawei, iPhone
  11/XR): decoder e encoder de hardware, LMK do Android e jetsam do iOS.
- iOS nativo: nem compilado nem executado.
- Os prazos do quadro aproximado (8 s / 16 s / 45 s) foram escolhidos, não
  medidos em aparelho.
