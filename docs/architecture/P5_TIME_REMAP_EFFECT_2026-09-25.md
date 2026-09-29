# Time Remap effect: canonical source-time curve

## Implemented
- Android and iOS render the real Time Remap graph and presets inside the `aurea.time.remap` effect card. Speed retains constant speed/reverse and frame blending.
- Generic key commands previously wrote both `TimeRemap` and the effect time parameter to `Layer.tracks`; playback reads `Layer.timeRemap`. Commands now resolve the canonical curve. The effect alias converts source seconds to composition frames; direct TimeRemap keys use frames.
- Layer/keyframe queries now include that curve, so timeline diamonds, selection, curve editing and dragging target actual playback data. Time Remap track labels open Effects.
- Effect enable/remove updates actual remap state. Smooth is EaseInOut rather than a misleading Linear alias.
- Presets include linear, smooth, slow middle, acceleration/deceleration, freeze at the current source frame and reversed source progression.

## Evidence
- `engine/build/host/remap-current-test.log`: 6 Remap tests, 139 checks, no failures, reported by scene3d agent. Includes canonical command writes, effect alias units, timeline query, enable/bypass, save/load, smooth/freeze/reverse.
- Android production Kotlin compilation passed in parent build; compact timeline regression passed separately in `timeline-compact-tests.log`.
- iOS API/static-name audit passed; this does not establish native Swift compilation or iPhone behavior.
- Added `ClipTime.MoveVideoToTwoSecondPlayheadPreservesSourceAndUndo` for the queued dock command at two seconds. Pending consolidated run. It has no GPU or AVFoundation decoder and does not reproduce/clear the reported physical iOS freeze.
- Added native XCTest `testAddingEffectClosesBrowserAndShowsAppliedCard`; requires Mac execution. Browser now observes effect-count confirmation after asynchronous submission rather than assuming immediate mutation.

## Limitations
Legacy remap migration now reads direct frame tracks or a uniquely identified effect time track in seconds. Composition double FPS converts values and velocity tangents; timestamps and normalized handles are preserved. Existing valid canonical curves win without merging conflicting timing. Old tracks move to lossless recovery metadata (Timeline section v23), outside active track queries; a persisted migration marker prevents deleted keys from returning after save/load. Unknown components, dangling effect IDs, expressions requiring unit reinterpretation, and multiple competing effect tracks without a canonical curve are left unchanged rather than guessed. Three Serialization.LegacyRemap regressions are added; consolidated compilation/tests pending. Encoded device export and physical touch interactions remain validation work; the source-time evaluator is shared with export.

## 2026-09-29: Time Remap em lista (UX inspirada no Node Video)
- O cartão do efeito `aurea.time.remap` virou três linhas + Ao contrário, nos dois apps: **Manter o tom do áudio** (liga/desliga), **Remapear tempo** (timecode H:MM:SS:QQ do momento da fonte no cabeçote; arrastar o valor para os lados = 1 quadro a cada 4 dp/pt, tocar = teclado em segundos, aceita "1:30"; ◇ marca/tira a chave; faixa fina com as chaves e o cabeçote) e **Interpolação do tempo** (Desligado / Mistura de quadros / Optical flow = `Layer::frameBlend` 0/1/2, prévia e exportação). Sem gráfico de valor nem atalhos.
- Easing entre chaves: o editor de curva NORMAL (`EditorPanel.Curve` / `openCurve(property: 30)`) no trecho da chave escolhida na faixa (ou o trecho sob o cabeçote). É a mesma trilha `Layer::timeRemap` com `keyframe_ease`, então Bounce, alças e força valem aqui também.
- `Engine::reverse_time_remap`: espelha a curva nos quadros que aparecem (`lo + (hi − 1) − t`), easing espelhado (`g(u) = 1 − f(1 − u)`: exato para linear, entrada/saída e bézier com força); duas vezes = identidade; um passo de desfazer; liga o remapeamento se estava desligado. `timeFlags` bit 256 = curva andando para trás (estado do interruptor).
- `Layer::keepPitch` (seção Timeline v39; projetos anteriores = desligado, som idêntico) + `Engine::set_keep_pitch`, `timeFlags` bit 128. O mixer (`keep_pitch_at` em `AudioInternal.hpp`) lê grãos Hann de 2048 amostras com 50% de sobreposição, cada um nascendo na posição da fonte do seu centro e tocando a ±1×: a 1× e a −1× reconstrói a fonte exatamente; fonte parada = silêncio. Vale no caminho de sempre e no da cadeia de efeitos (não nos envios antigos de reverb/eco).
- Testes: `ClipTime.RemapPanelKeysMapTimelineToSourceFreezeAndEase`, `ClipTime.RemapReverseMirrorsTheCurveExactlyAndUndoes`, `ClipTime.KeepPitchHoldsToneWhileTheSourceFollowsTheRemap`, `ClipTime.KeepPitchAndInterpolationSurviveSaveAndOldProjectsStayOff`. Mistura/optical flow já cobertos por `Gpu.FrameBlendMixesNeighbourSourceFramesInSlowMotion` e `Gpu.OpticalFlowPlacesTheMovingSquareBetweenFrames`.
