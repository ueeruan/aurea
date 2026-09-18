import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/scheduler.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/ui/snack.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/grid_rig.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// Painel "Curva de gradacao": grafico tempo->tempo com grade, alcas
/// grandes, thumbnails de preset a direita e navegacao entre segmentos.
/// AREA DE TRANSFERENCIA DE CURVA: copiar o easing de um trecho e colar
/// em outro — de outro parametro, de outra camada, de outro efeito.
/// Uma so, global, em memoria: e assim que se usa (copia, vai la, cola).
class EasingClipboard {
  EasingClipboard._();

  static Easing? valor;
}

class CurvePanel extends ConsumerStatefulWidget {
  const CurvePanel({
    super.key,
    required this.playback,
    required this.prop,
    required this.onBack,
  });

  final PlaybackController playback;
  final LayerProp prop;
  final VoidCallback onBack;

  @override
  ConsumerState<CurvePanel> createState() => _CurvePanelState();
}

class _CurvePanelState extends ConsumerState<CurvePanel> {
  bool _overshoot = false;

  /// GRAFICO DE VELOCIDADE em vez do de valor.
  ///
  /// A curva em S que da o timing "Apple" — desaceleracao forte nas duas
  /// pontas — se molda olhando a VELOCIDADE: la a desaceleracao e uma
  /// rampa que se ve; no grafico de valor ela e uma curvinha que se
  /// adivinha. Sao a mesma bezier vista de dois jeitos: aqui a derivada.
  bool _velocidade = false;

  /// O ULTIMO TRECHO MOSTRADO, em tempo local da camada.
  ///
  /// Fora dos keyframes nao existe trecho sob o cabecote. A saida ANTIGA
  /// era arrastar o cabecote de volta para dentro — e isso brigava com a
  /// reproducao: o relogio avancava, o painel puxava de volta, o relogio
  /// avancava de novo. O cabecote ia e voltava sem parar.
  ///
  /// Agora ninguem move o cabecote. Fora de qualquer trecho o painel
  /// segura o ultimo, esmaecido, com o rotulo dizendo qual e.
  Duration? _lembrado;

  @override
  void initState() {
    super.initState();
    // Segue o playhead: o segmento mostrado acompanha o scrub na timeline.
    widget.playback.time.addListener(_onClock);
  }

  @override
  void dispose() {
    widget.playback.time.removeListener(_onClock);
    super.dispose();
  }

  void _onClock() {
    if (!mounted) return;
    // O clock pode bater DENTRO do frame (o ticker roda antes do build);
    // nesse caso adia o setState para depois, senao e rebuild durante
    // rebuild.
    if (SchedulerBinding.instance.schedulerPhase ==
        SchedulerPhase.persistentCallbacks) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) setState(() {});
      });
    } else {
      setState(() {});
    }
  }

  List<Duration> _kfTimes(Layer layer) {
    final track = switch (widget.prop) {
      LayerProp.position => layer.position.keyframes.map((k) => k.time),
      LayerProp.scale => layer.scaleX.keyframes.map((k) => k.time),
      LayerProp.rotation => layer.rotation.keyframes.map((k) => k.time),
      LayerProp.opacity => layer.opacity.keyframes.map((k) => k.time),
      LayerProp.skew => layer.skewX.keyframes.map((k) => k.time),
      LayerProp.pivot => layer.pivot.keyframes.map((k) => k.time),
      LayerProp.parent => const Iterable<Duration>.empty(),
    };
    return track.toList();
  }

  Easing _easeOf(Layer layer, Duration segStart) => switch (widget.prop) {
    LayerProp.position => layer.position.easeAt(segStart),
    LayerProp.scale => layer.scaleX.easeAt(segStart),
    LayerProp.rotation => layer.rotation.easeAt(segStart),
    LayerProp.opacity => layer.opacity.easeAt(segStart),
    LayerProp.skew => layer.skewX.easeAt(segStart),
    LayerProp.pivot => layer.pivot.easeAt(segStart),
    LayerProp.parent => Easing.linear,
  };

  (Duration, Duration)? _segmentAt(Layer layer, Duration local) {
    final times = _kfTimes(layer);
    for (var i = 0; i < times.length - 1; i++) {
      if (local >= times[i] && local < times[i + 1]) {
        return (times[i], times[i + 1]);
      }
    }
    return null;
  }

  void _jumpSegment(Layer layer, int dir) {
    final times = _kfTimes(layer);
    if (times.length < 2) return;
    final local = layer.localTime(widget.playback.time.value);
    final mids = [
      for (var i = 0; i < times.length - 1; i++)
        times[i] + (times[i + 1] - times[i]) ~/ 2,
    ];
    var idx = 0;
    for (var i = 0; i < mids.length; i++) {
      if (local >= times[i]) idx = i;
    }
    final target = (idx + dir).clamp(0, mids.length - 1);
    // Trocar de trecho e ESCOLHA da pessoa, entao aqui mover o cabecote e
    // legitimo — mas pausa antes, para nao disputar com o relogio.
    widget.playback.pause();
    widget.playback.seek(layer.startTime + mids[target]);
    setState(() => _lembrado = times[target]);
  }

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);
    final id = ref.watch(selectedLayerProvider);
    final layer = id == null ? null : project.layerById(id);
    if (layer == null || id == null) {
      return const ColoredBox(color: AmColors.panel);
    }

    final controller = ref.read(editorControllerProvider.notifier);
    final local = layer.localTime(widget.playback.time.value);
    final times = _kfTimes(layer);

    // O CABECOTE E DE QUEM O MOVE — do dedo e do relogio, de mais
    // ninguem. Este painel nunca o desloca.
    final sobCabecote = _segmentAt(layer, local);
    if (sobCabecote != null) _lembrado = sobCabecote.$1;

    // Fora de todo trecho: segura o ultimo. Se nem esse existe mais
    // (keyframe apagado), cai no primeiro que houver — sem mover nada.
    var segment = sobCabecote;
    if (segment == null && times.length >= 2) {
      final alvo = _lembrado;
      var i = alvo == null ? -1 : times.indexOf(alvo);
      if (i < 0 || i >= times.length - 1) i = 0;
      segment = (times[i], times[i + 1]);
      _lembrado = times[i];
    }
    final foraDoTrecho = sobCabecote == null;

    // Onde o cabecote esta DENTRO do trecho, 0..1 — o ponto que corre
    // sobre a curva enquanto a animacao toca.
    double? percorrido;
    if (!foraDoTrecho && segment != null) {
      final span = (segment.$2 - segment.$1).inMicroseconds;
      if (span > 0) {
        percorrido = ((local - segment.$1).inMicroseconds / span).clamp(
          0.0,
          1.0,
        );
      }
    }

    final ease = segment == null ? null : _easeOf(layer, segment.$1);
    return ColoredBox(
      color: AmColors.panel,
      child: segment == null || ease == null
          ? Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const AppText('Crie pelo menos 2 keyframes\npara editar a curva.',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: AmColors.muted, fontSize: 13),
                  ),
                  const SizedBox(height: 12),
                  CupertinoButton(
                    onPressed: widget.onBack,
                    child: const AppText(
                      'Voltar',
                      style: TextStyle(color: AmColors.accent),
                    ),
                  ),
                ],
              ),
            )
          : Row(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // Trilho esquerdo: voltar / inverter / menu.
                SizedBox(
                  width: 44,
                  child: LayoutBuilder(
                    builder: (context, constraints) => SingleChildScrollView(
                      child: SizedBox(
                        height: constraints.maxHeight < 202
                            ? 202
                            : constraints.maxHeight,
                        child: Column(
                          children: [
                            const SizedBox(height: 6),
                            AmRailButton(
                              key: const ValueKey('curve-back'),
                              onTap: widget.onBack,
                              child: const Icon(
                                CupertinoIcons.chevron_back,
                                size: 24,
                                color: Colors.white,
                              ),
                            ),
                            const Spacer(),
                            AmRailButton(
                              key: const ValueKey('curve-inverter'),
                              onTap: () {
                                // INVERTER: o fim vira o comeco. A bezier
                                // espelha as alcas; quique e elastico
                                // trocam de ponta; o resto e simetrico.
                                final invertida = ease.invertida;
                                if (invertida == null) {
                                  AureaSnack.show(
                                    context,
                                    'Esta curva é igual nos dois sentidos',
                                  );
                                  return;
                                }
                                controller.setSegmentEase(
                                  id,
                                  widget.prop,
                                  segment!.$1,
                                  invertida,
                                );
                              },
                              child: const Icon(
                                CupertinoIcons.arrow_right_arrow_left,
                                size: 20,
                                color: Colors.white,
                              ),
                            ),
                            const SizedBox(height: 4),
                            PopupMenuButton<String>(
                              tooltip: _overshoot
                                  ? 'Opções da curva · overshoot ligado'
                                  : 'Opções da curva',
                              icon: const Icon(
                                Icons.more_horiz,
                                size: 22,
                                color: Colors.white,
                              ),
                              color: AmColors.panelHigh,
                              itemBuilder: (_) => [
                                const PopupMenuItem(
                                  value: 'copy',
                                  child: AppText('Copiar curva'),
                                ),
                                PopupMenuItem(
                                  value: 'paste',
                                  enabled: EasingClipboard.valor != null,
                                  child: const AppText('Colar curva'),
                                ),
                                const PopupMenuItem(
                                  value: 'all',
                                  child: AppText('Aplicar em todos os segmentos'),
                                ),
                                CheckedPopupMenuItem(
                                  value: 'overshoot',
                                  checked: _overshoot,
                                  child: const AppText('Overshoot'),
                                ),
                                CheckedPopupMenuItem(
                                  value: 'speed',
                                  checked: _velocidade,
                                  child: const AppText('Gráfico de velocidade'),
                                ),
                                const PopupMenuDivider(),
                                const PopupMenuItem(
                                  value: 'loop-none',
                                  child: AppText('Loop: nenhum'),
                                ),
                                const PopupMenuItem(
                                  value: 'loop-cycle',
                                  child: AppText('Loop: repetir'),
                                ),
                                const PopupMenuItem(
                                  value: 'loop-pingPong',
                                  child: AppText('Loop: vai e volta'),
                                ),
                              ],
                              onSelected: (action) {
                                switch (action) {
                                  case 'copy':
                                    setState(
                                      () => EasingClipboard.valor = ease,
                                    );
                                  case 'paste':
                                    if (EasingClipboard.valor != null) {
                                      controller.setSegmentEase(
                                        id,
                                        widget.prop,
                                        segment!.$1,
                                        EasingClipboard.valor!,
                                      );
                                    }
                                  case 'all':
                                    controller.applyEaseToAllSegments(
                                      id,
                                      widget.prop,
                                      ease,
                                    );
                                  case 'overshoot':
                                    setState(() => _overshoot = !_overshoot);
                                  case 'speed':
                                    setState(() => _velocidade = !_velocidade);
                                  case 'loop-none':
                                    controller.setPropertyLoop(
                                      id,
                                      widget.prop,
                                      const LoopSpec(),
                                    );
                                  case 'loop-cycle':
                                    controller.setPropertyLoop(
                                      id,
                                      widget.prop,
                                      const LoopSpec(mode: LoopMode.cycle),
                                    );
                                  case 'loop-pingPong':
                                    controller.setPropertyLoop(
                                      id,
                                      widget.prop,
                                      const LoopSpec(mode: LoopMode.pingPong),
                                    );
                                }
                              },
                            ),
                            const SizedBox(height: 8),
                          ],
                        ),
                      ),
                    ),
                  ),
                ),
                // Miolo: Gráfico Bezier + Rodapé com Chevrons
                Expanded(
                  child: Column(
                    children: [
                      Expanded(
                        child: Opacity(
                          opacity: foraDoTrecho ? 0.45 : 1,
                          child: _velocidade
                              ? _SpeedGraph(
                                  ease: ease,
                                  percorrido: percorrido,
                                  onGestoInicio: controller.beginGesture,
                                  onGestoFim: controller.endGesture,
                                  onBezierChanged: (e) =>
                                      controller.setSegmentEase(
                                        id,
                                        widget.prop,
                                        segment!.$1,
                                        e,
                                      ),
                                )
                              : _CurveGraph(
                                  key: const ValueKey('curve-edit-area'),
                                  ease: ease,
                                  overshootEnabled: _overshoot,
                                  percorrido: percorrido,
                                  onGestoInicio: controller.beginGesture,
                                  onGestoFim: controller.endGesture,
                                  onBezierChanged: (e) =>
                                      controller.setSegmentEase(
                                        id,
                                        widget.prop,
                                        segment!.$1,
                                        e,
                                      ),
                                ),
                        ),
                      ),
                      // Rodapé: "<  Efeito Ease de Cúbico-Bezier  >"
                      SizedBox(
                        height: 32,
                        child: Row(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            CupertinoButton(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 4,
                              ),
                              onPressed: () => _jumpSegment(layer, -1),
                              child: const Icon(
                                CupertinoIcons.chevron_left,
                                size: 16,
                                color: Colors.white,
                              ),
                            ),
                            const SizedBox(width: 4),
                            Flexible(
                              child: AppText(
                                foraDoTrecho
                                    ? 'Trecho ${times.indexOf(segment.$1) + 1} \u2192 ${times.indexOf(segment.$1) + 2}'
                                    : _nomeDaCurva(ease),
                                textAlign: TextAlign.center,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(
                                  fontSize: 12,
                                  fontWeight: FontWeight.w600,
                                  color: Colors.white,
                                ),
                              ),
                            ),
                            const SizedBox(width: 4),
                            CupertinoButton(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 4,
                              ),
                              onPressed: () => _jumpSegment(layer, 1),
                              child: const Icon(
                                CupertinoIcons.chevron_right,
                                size: 16,
                                color: Colors.white,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
                // TRILHO DIREITO: as familias da curva em abas, quatro a
                // seis presets cada. A aba nasce na familia do trecho.
                _FamiliasDaCurva(
                  ease: ease,
                  onEscolher: (e) => controller.setSegmentEase(
                    id,
                    widget.prop,
                    segment!.$1,
                    e,
                  ),
                ),
              ],
            ),
    );
  }

  static const _presets = <({String nome, Easing ease})>[
    (nome: 'Linear', ease: Easing.linear),
    (nome: 'Ease in', ease: Easing.easeIn),
    (nome: 'Ease out', ease: Easing.easeOut),
    (nome: 'Ease in-out', ease: Easing.easeInOut),
    (nome: 'Overshoot', ease: Easing.overshoot),
    (nome: 'Quicar', ease: Easing.bounce),
    (nome: 'Elástico', ease: Easing.elastic),
    (nome: 'Degraus', ease: Easing(type: EasingType.steps)),
    (nome: 'Cíclico', ease: Easing(type: EasingType.cyclic)),
    (nome: 'Apple padrão', ease: Easing.appleStandard),
    (nome: 'Apple entrada', ease: Easing.appleEntrance),
    (nome: 'Apple saída', ease: Easing.appleExit),
    (nome: 'Mola interface', ease: Easing.interfaceSpring),
    (nome: 'Mola suave', ease: Easing.softSpring),
  ];

  /// O NOME DO TRECHO no rodape: o preset quando e um, "(personalizada)"
  /// quando as alcas ja sairam de qualquer um.
  static String _nomeDaCurva(Easing e) {
    for (final familia in _familiasDaCurva) {
      for (final (nome, preset) in familia.presets) {
        if (_samePreset(e, preset)) return nome;
      }
    }
    return e.type == EasingType.cubicBezier
        ? 'Bézier (personalizada)'
        : '${e.label} (personalizada)';
  }

  static bool _samePreset(Easing a, Easing b) {
    if (a.type != b.type) return false;
    if (a.type == EasingType.spring) {
      return (a.response - b.response).abs() < 0.001 &&
          (a.damping - b.damping).abs() < 0.001 &&
          (a.initialVelocity - b.initialVelocity).abs() < 0.001;
    }
    if (a.type != EasingType.cubicBezier) return true;
    return (a.x1 - b.x1).abs() < 0.01 &&
        (a.y1 - b.y1).abs() < 0.01 &&
        (a.x2 - b.x2).abs() < 0.01 &&
        (a.y2 - b.y2).abs() < 0.01;
  }
}

/// Curve editor de uma trilha do MODULO GRADE (inclui 'transition', o
/// morph): sheet persistente — o preview continua visivel, o transporte
/// escolhe o segmento e as alcas/presets editam a curva daquele trecho.
Future<void> showGridCurveSheet(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback,
  String nullId,
  String paramKey,
  String label, {
  VoidCallback? onClosed,
}) {
  final controller = ref.read(editorControllerProvider.notifier);
  return showTrackCurveSheet(
    context,
    ref,
    playback,
    label: label,
    layerId: nullId,
    trackOf: (layer) => layer is NullLayer && layer.grid != null
        ? gridTrackOf(layer.grid!, paramKey)
        : null,
    onSetEase: (segStart, ease) =>
        controller.setGridSegmentEase(nullId, paramKey, segStart, ease),
    onSetEaseAll: (ease) =>
        controller.applyEaseToAllGridSegments(nullId, paramKey, ease),
    onClosed: onClosed,
  );
}

/// Curve editor GENERICO de qualquer trilha animavel (grade, parametros
/// de forma...): quem chama diz como achar a trilha e como gravar o
/// easing; o sheet cuida de segmento, alcas e presets.
Future<void> showTrackCurveSheet(
  BuildContext context,
  WidgetRef ref,
  PlaybackController playback, {
  required String label,
  required String layerId,
  required AnimatedDouble? Function(Layer layer) trackOf,
  required void Function(Duration segStartLocal, Easing ease) onSetEase,
  required void Function(Easing ease) onSetEaseAll,
  VoidCallback? onClosed,
}) async {
  final myGen = paramSheetGeneration + 1;
  var aplicarEmTodos = false;
  await showParamSheet(
    context,
    heightFactor: 0.5,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) =>
          ValueListenableBuilder<Duration>(
            valueListenable: playback.time,
            builder: (sheetContext, t, _) {
              final project = ref.read(editorControllerProvider);
              final layer = project.layerById(layerId);
              if (layer == null) return const SizedBox.shrink();
              final track = trackOf(layer);
              if (track == null) return const SizedBox.shrink();
              final local = layer.localTime(t);
              final times = [for (final k in track.keyframes) k.time];

              (Duration, Duration)? seg;
              for (var i = 0; i < times.length - 1; i++) {
                if (local >= times[i] && local < times[i + 1]) {
                  seg = (times[i], times[i + 1]);
                  break;
                }
              }

              void jump(int dir) {
                if (times.length < 2) return;
                final mids = [
                  for (var i = 0; i < times.length - 1; i++)
                    times[i] + (times[i + 1] - times[i]) ~/ 2,
                ];
                var idx = 0;
                for (var i = 0; i < mids.length; i++) {
                  if (local >= times[i]) idx = i;
                }
                final target = (idx + dir).clamp(0, mids.length - 1);
                playback.seek(layer.startTime + mids[target]);
              }

              final ease = seg == null ? null : track.easeAt(seg.$1);

              return SafeArea(
                // Scroll: em tela baixa o sheet e capado para nao cobrir o
                // preview — o conteudo rola em vez de estourar.
                child: SingleChildScrollView(
                  padding: const EdgeInsets.fromLTRB(18, 12, 18, 10),
                  child: Column(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      AppTextMoldado(
                        'Curva — {0}', [label],
                        style: const TextStyle(
                          fontSize: 16,
                          fontWeight: FontWeight.w700,
                          color: AmColors.text,
                        ),
                      ),
                      SheetTransport(
                        playback: playback,
                        duration: project.duration,
                        fps: project.fps,
                      ),
                      const SizedBox(height: 6),
                      if (seg == null || ease == null)
                        Container(
                          height: 150,
                          alignment: Alignment.center,
                          child: Column(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              const AppText('Crie 2+ keyframes neste parametro e leve o\n'
                                'playhead para DENTRO do trecho entre eles.',
                                textAlign: TextAlign.center,
                                style: TextStyle(
                                  color: AmColors.muted,
                                  fontSize: 13,
                                ),
                              ),
                              if (times.length >= 2)
                                CupertinoButton(
                                  onPressed: () {
                                    playback.seek(
                                      layer.startTime +
                                          times[0] +
                                          (times[1] - times[0]) ~/ 2,
                                    );
                                  },
                                  child: const AppText('Ir ao primeiro trecho',
                                    style: TextStyle(
                                      color: AmColors.accent,
                                      fontSize: 14,
                                    ),
                                  ),
                                ),
                            ],
                          ),
                        )
                      else ...[
                        SizedBox(
                          height: 220,
                          child: Row(
                            crossAxisAlignment: CrossAxisAlignment.stretch,
                            children: [
                              CupertinoButton(
                                padding: const EdgeInsets.all(6),
                                onPressed: () => jump(-1),
                                child: const Icon(
                                  CupertinoIcons.chevron_left,
                                  size: 18,
                                  color: AmColors.muted,
                                ),
                              ),
                              Expanded(
                                child: _CurveGraph(
                                  ease: ease,
                                  overshootEnabled: true,
                                  onBezierChanged: (e) {
                                    onSetEase(seg!.$1, e);
                                    setSheetState(() {});
                                  },
                                ),
                              ),
                              CupertinoButton(
                                padding: const EdgeInsets.all(6),
                                onPressed: () => jump(1),
                                child: const Icon(
                                  CupertinoIcons.chevron_right,
                                  size: 18,
                                  color: AmColors.muted,
                                ),
                              ),
                            ],
                          ),
                        ),
                        const SizedBox(height: 6),
                        SizedBox(
                          height: 84,
                          child: ListView(
                            scrollDirection: Axis.horizontal,
                            children: [
                              SizedBox(
                                width: 112,
                                child: Padding(
                                  padding: const EdgeInsets.only(right: 8),
                                  child: _ScopeToggle(
                                    todos: aplicarEmTodos,
                                    horizontal: true,
                                    onChanged: (v) {
                                      aplicarEmTodos = v;
                                      setSheetState(() {});
                                    },
                                  ),
                                ),
                              ),
                              for (final preset in _CurvePanelState._presets)
                                SizedBox(
                                  width: 74,
                                  child: Padding(
                                    padding: const EdgeInsets.only(right: 8),
                                    child: _PresetTile(
                                      ease: preset.ease,
                                      label: preset.nome,
                                      selected: _CurvePanelState._samePreset(
                                        ease,
                                        preset.ease,
                                      ),
                                      onTap: () {
                                        if (aplicarEmTodos) {
                                          onSetEaseAll(preset.ease);
                                        } else {
                                          onSetEase(seg!.$1, preset.ease);
                                        }
                                        setSheetState(() {});
                                      },
                                    ),
                                  ),
                                ),
                              CupertinoButton(
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 10,
                                ),
                                onPressed: () {
                                  EasingClipboard.valor = ease;
                                  setSheetState(() {});
                                },
                                child: const AppText('Copiar',
                                  style: TextStyle(
                                    fontSize: 12,
                                    color: AmColors.accent,
                                  ),
                                ),
                              ),
                              CupertinoButton(
                                padding: const EdgeInsets.symmetric(
                                  horizontal: 10,
                                ),
                                onPressed: EasingClipboard.valor == null
                                    ? null
                                    : () {
                                        onSetEase(
                                          seg!.$1,
                                          EasingClipboard.valor!,
                                        );
                                        setSheetState(() {});
                                      },
                                child: const AppText('Colar',
                                  style: TextStyle(
                                    fontSize: 12,
                                    color: AmColors.accent,
                                  ),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ],
                    ],
                  ),
                ),
              );
            },
          ),
    ),
  );
  // Fechou o editor de curvas: devolve o painel que o abriu — o usuario
  // nunca "cai" de volta na timeline sem aviso. So reabre se NENHUM
  // outro sheet tomou o lugar nesse meio-tempo.
  if (paramSheetGeneration == myGen) onClosed?.call();
}

/// ONDE FICAM AS ALCAS AMARELAS de cada familia parametrica, e o que
/// cada uma escreve.
///
/// A alca A anda na HORIZONTAL e conta as repeticoes: quanto mais para
/// a esquerda, mais oscilacoes cabem no trecho. A alca B anda na
/// VERTICAL e regula a forca — quanto o elastico passa do ponto, quanto
/// o quique perde de altura, quanto o ciclo e suave. As duas ficam
/// sempre EM CIMA do que controlam, para o dedo entender sem legenda.
abstract final class AlcasParametricas {
  static const int maximo = 12;

  /// Onde a alca A se apoia na curva: no primeiro pico (elastico), no
  /// primeiro toque no chao (quicar) ou no fim do primeiro ciclo.
  static double _fatorDeA(EasingType t) => switch (t) {
    EasingType.elastic => 0.75,
    EasingType.bounce => 0.5,
    _ => 1.0,
  };

  static List<(double, double)> de(Easing e) {
    final n = (e.count < 1 ? 1 : e.count).clamp(1, maximo);
    final xA = (_fatorDeA(e.type) / n).clamp(0.04, 0.96);
    switch (e.type) {
      case EasingType.elastic:
        return [(xA, e.transform(xA)), (0.5, 1 + 0.45 * e.intensity)];
      case EasingType.bounce:
        return [(xA, e.transform(xA)), (0.5, 1 - 0.6 * e.intensity)];
      case EasingType.cyclic:
        return [(xA, e.transform(xA)), (0.5, e.smooth.clamp(0.0, 1.0))];
      case EasingType.steps:
      case EasingType.elasticSteps:
        return [(xA, e.transform(xA))];
      default:
        return const [];
    }
  }

  /// A alca A moveu para [x]: recalcula as repeticoes.
  static Easing comAlcaA(Easing e, double x) {
    final xx = x.clamp(0.04, 0.96);
    final n = (_fatorDeA(e.type) / xx).round().clamp(1, maximo);
    return e.copyWith(count: n);
  }

  /// A alca B moveu para [y]: recalcula a forca.
  static Easing comAlcaB(Easing e, double y) => switch (e.type) {
    EasingType.elastic => e.copyWith(
      intensity: ((y - 1) / 0.45).clamp(0.05, 1.0),
    ),
    EasingType.bounce => e.copyWith(
      intensity: ((1 - y) / 0.6).clamp(0.05, 1.0),
    ),
    EasingType.cyclic => e.copyWith(smooth: y.clamp(0.0, 1.0)),
    _ => e,
  };
}

class _CurveGraph extends StatefulWidget {
  const _CurveGraph({
    super.key,
    required this.ease,
    required this.overshootEnabled,
    required this.onBezierChanged,
    this.onGestoInicio,
    this.onGestoFim,
    this.percorrido,
  });

  final Easing ease;
  final bool overshootEnabled;
  final ValueChanged<Easing> onBezierChanged;

  /// Comeco e fim do ARRASTO, para o desfazer virar um passo so.
  final VoidCallback? onGestoInicio;
  final VoidCallback? onGestoFim;

  /// 0..1: onde o cabecote esta dentro do trecho. Nulo = esta fora.
  final double? percorrido;

  /// A FAIXA VERTICAL SEGUE A CURVA.
  ///
  /// Era sempre de -0,5 a 1,5 — metade da altura reservada para um
  /// overshoot que quase nunca existe, e a curva normal espremida no
  /// meio. Sem overshoot, a faixa fecha em volta de 0..1 e a curva
  /// ocupa o grafico; com overshoot ligado, ou com uma alca que ja
  /// passou dos limites, a faixa abre para caber.
  double get _yMin =>
      overshootEnabled || ease.y1 < 0 || ease.y2 < 0 ? -0.5 : -0.12;
  double get _yMax =>
      overshootEnabled ||
          ease.y1 > 1 ||
          ease.y2 > 1 ||
          ease.type == EasingType.elastic
      ? 1.5
      : 1.12;

  Offset _toPlot(Size size, double x, double y) => Offset(
    x * size.width,
    size.height - (y - _yMin) / (_yMax - _yMin) * size.height,
  );

  (double, double) _fromPlot(Size size, Offset p) => (
    (p.dx / size.width).clamp(0.0, 1.0),
    _yMin + (size.height - p.dy) / size.height * (_yMax - _yMin),
  );

  @override
  State<_CurveGraph> createState() => _CurveGraphState();
}

class _CurveGraphState extends State<_CurveGraph> {
  /// QUAL ALCA ESTA NA MAO — decidida uma vez, no toque.
  ///
  /// Este campo e a correcao do bug que os testadores descreveram como
  /// "a curva salta". A alca era escolhida a CADA atualizacao do
  /// arrasto, pela proximidade do dedo: bastava o dedo cruzar o meio do
  /// grafico para o gesto largar a alca que estava movendo e agarrar a
  /// outra. Pior, as posicoes usadas na comparacao vinham do quadro
  /// ANTERIOR, entao o resultado dependia de o widget ter reconstruido
  /// a tempo — o mesmo gesto dava resultados diferentes.
  ///
  /// Agora quem se decide e o toque. O resto do arrasto obedece.
  int? _alca;

  @override
  Widget build(BuildContext context) {
    final ease = widget.ease;
    return LayoutBuilder(
      builder: (context, constraints) {
        final size = Size(constraints.maxWidth, constraints.maxHeight);
        final h1 = widget._toPlot(size, ease.x1, ease.y1);
        final h2 = widget._toPlot(size, ease.x2, ease.y2);

        void inicio(DragStartDetails d) {
          final p = d.localPosition;
          _alca = (p - h1).distanceSquared <= (p - h2).distanceSquared ? 1 : 2;
          widget.onGestoInicio?.call();
        }

        void drag(DragUpdateDetails d) {
          final alca = _alca;
          if (alca == null) return;
          var (x, y) = widget._fromPlot(size, d.localPosition);
          if (!widget.overshootEnabled) y = y.clamp(0.0, 1.0);
          // NENHUM GESTO PODE CORROMPER A CURVA. Um NaN aqui atravessa
          // o projeto inteiro e reaparece como animacao que nao avalia.
          if (x.isNaN || y.isNaN || !x.isFinite || !y.isFinite) return;
          widget.onBezierChanged(
            alca == 1
                ? ease.copyWith(x1: x, y1: y)
                : ease.copyWith(x2: x, y2: y),
          );
        }

        void fim() {
          _alca = null;
          widget.onGestoFim?.call();
        }

        // AS ALCAS AMARELAS DAS FAMILIAS PARAMETRICAS (Elastico, Quicar,
        // Ciclico, degraus). "Voce consegue colocar pra editar o grafico
        // de elastico?" — o pedido dos testadores. Cada alca escreve num
        // numero real do `Easing` (`count`, `intensity`, `smooth`) e a
        // curva desenhada, a previa e a exportacao mudam juntas.
        final alcas = AlcasParametricas.de(ease);
        final parametrico = alcas.isNotEmpty;
        final hA = parametrico ? widget._toPlot(size, alcas[0].$1, alcas[0].$2) : null;
        final hB = alcas.length > 1
            ? widget._toPlot(size, alcas[1].$1, alcas[1].$2)
            : null;

        void inicioParam(DragStartDetails d) {
          final p = d.localPosition;
          _alca = hB == null ||
                  (p - hA!).distanceSquared <= (p - hB).distanceSquared
              ? 1
              : 2;
          widget.onGestoInicio?.call();
        }

        void dragParam(DragUpdateDetails d) {
          final alca = _alca;
          if (alca == null) return;
          final (x, y) = widget._fromPlot(size, d.localPosition);
          if (x.isNaN || y.isNaN || !x.isFinite || !y.isFinite) return;
          widget.onBezierChanged(
            alca == 1
                ? AlcasParametricas.comAlcaA(ease, x)
                : AlcasParametricas.comAlcaB(ease, y),
          );
        }

        final enabled = ease.type == EasingType.cubicBezier;
        // Reconhecedores vertical E horizontal (nao pan): dentro de um
        // bottom sheet persistente, o pan PERDE a arena de gestos para o
        // drag-de-fechar vertical do sheet — a alca "nao mexia" e o
        // gesto arrastava o sheet. Recognizer igual em no mais fundo
        // ganha a arena.
        final onStart = enabled ? inicio : (parametrico ? inicioParam : null);
        final onUpdate = enabled ? drag : (parametrico ? dragParam : null);
        final ativo = enabled || parametrico;
        return GestureDetector(
          onVerticalDragStart: onStart,
          onVerticalDragUpdate: onUpdate,
          onVerticalDragEnd: ativo ? (_) => fim() : null,
          onVerticalDragCancel: ativo ? fim : null,
          onHorizontalDragStart: onStart,
          onHorizontalDragUpdate: onUpdate,
          onHorizontalDragEnd: ativo ? (_) => fim() : null,
          onHorizontalDragCancel: ativo ? fim : null,
          child: CustomPaint(
            size: size,
            painter: _AmCurvePainter(
              ease: ease,
              yMin: widget._yMin,
              yMax: widget._yMax,
              percorrido: widget.percorrido,
            ),
          ),
        );
      },
    );
  }
}

/// GRAFICO DE VELOCIDADE: a derivada da bezier de easing, editavel.
///
/// Dois pontos: a velocidade de SAIDA do keyframe inicial e a de CHEGADA
/// no final, cada um com a sua influencia (quanto do trecho ele domina).
/// Numa bezier de easing com alcas (x1,y1) e (x2,y2), a velocidade
/// inicial e y1/x1, a final e (1-y2)/(1-x2), e as influencias sao x1 e
/// 1-x2. Arrastar um ponto na horizontal muda a influencia; na vertical,
/// a velocidade. A curva de VALOR e reconstruida na hora — e a mesma
/// bezier, entao trocar de grafico nunca perde nada.
class _SpeedGraph extends StatefulWidget {
  const _SpeedGraph({
    required this.ease,
    required this.onBezierChanged,
    this.onGestoInicio,
    this.onGestoFim,
    this.percorrido,
  });

  final Easing ease;
  final ValueChanged<Easing> onBezierChanged;
  final VoidCallback? onGestoInicio;
  final VoidCallback? onGestoFim;
  final double? percorrido;

  /// Velocidade maxima mostrada, em "vezes a velocidade media".
  static const double _vMax = 4.0;

  // A velocidade nas pontas vem da mesma derivada analitica do grafico:
  // as alcas caem EM CIMA da curva, inclusive com a alca degenerada.
  static double _vIni(Easing e) => e.speedAt(0).clamp(0.0, _vMax);
  static double _vFim(Easing e) => e.speedAt(1).clamp(0.0, _vMax);

  @override
  State<_SpeedGraph> createState() => _SpeedGraphState();
}

class _SpeedGraphState extends State<_SpeedGraph> {
  /// O mesmo de [_CurveGraphState._alca]: o ponto agarrado se decide no
  /// toque, e nao a cada atualizacao do arrasto.
  int? _ponto;

  @override
  Widget build(BuildContext context) {
    final ease = widget.ease;
    return LayoutBuilder(
      builder: (context, c) {
        final size = Size(c.maxWidth, c.maxHeight);
        Offset plot(double x, double v) => Offset(
          x * size.width,
          size.height - v / _SpeedGraph._vMax * size.height,
        );

        final pIni = plot(ease.x1, _SpeedGraph._vIni(ease));
        final pFim = plot(ease.x2, _SpeedGraph._vFim(ease));

        void inicio(DragStartDetails d) {
          final p = d.localPosition;
          _ponto = (p - pIni).distanceSquared <= (p - pFim).distanceSquared
              ? 1
              : 2;
          widget.onGestoInicio?.call();
        }

        void drag(DragUpdateDetails d) {
          final ponto = _ponto;
          if (ponto == null) return;
          final p = d.localPosition;
          final pertoIni = ponto == 1;
          final x = (p.dx / size.width).clamp(0.02, 0.98);
          final v = ((size.height - p.dy) / size.height * _SpeedGraph._vMax)
              .clamp(0.0, _SpeedGraph._vMax);
          if (x.isNaN || v.isNaN || !x.isFinite || !v.isFinite) return;
          if (pertoIni) {
            // influencia = x1; velocidade inicial = y1/x1 -> y1 = v * x1.
            final x1 = math.min(x, ease.x2 - 0.02);
            widget.onBezierChanged(
              ease.copyWith(x1: x1, y1: (v * x1).clamp(-2.0, 2.0)),
            );
          } else {
            // influencia = 1-x2; velocidade final = (1-y2)/(1-x2).
            final x2 = math.max(x, ease.x1 + 0.02);
            widget.onBezierChanged(
              ease.copyWith(x2: x2, y2: (1 - v * (1 - x2)).clamp(-1.0, 3.0)),
            );
          }
        }

        void fim() {
          _ponto = null;
          widget.onGestoFim?.call();
        }

        final editavel = ease.type == EasingType.cubicBezier;
        return GestureDetector(
          onVerticalDragStart: editavel ? inicio : null,
          onVerticalDragUpdate: editavel ? drag : null,
          onVerticalDragEnd: editavel ? (_) => fim() : null,
          onVerticalDragCancel: editavel ? fim : null,
          onHorizontalDragStart: editavel ? inicio : null,
          onHorizontalDragUpdate: editavel ? drag : null,
          onHorizontalDragEnd: editavel ? (_) => fim() : null,
          onHorizontalDragCancel: editavel ? fim : null,
          child: CustomPaint(
            size: size,
            painter: _SpeedPainter(
              ease: ease,
              vMax: _SpeedGraph._vMax,
              pIni: pIni,
              pFim: pFim,
              percorrido: widget.percorrido,
            ),
          ),
        );
      },
    );
  }
}

class _SpeedPainter extends CustomPainter {
  const _SpeedPainter({
    required this.ease,
    required this.vMax,
    required this.pIni,
    required this.pFim,
    this.percorrido,
  });

  final Easing ease;
  final double vMax;
  final Offset pIni;
  final Offset pFim;
  final double? percorrido;

  @override
  void paint(Canvas canvas, Size size) {
    // Grade, com a linha da velocidade MEDIA (1x) destacada: e a
    // referencia — acima dela o movimento esta rapido, abaixo, lento.
    final grid = Paint()
      ..color = const Color(0xFF34405A)
      ..strokeWidth = 1;
    for (var i = 1; i < 8; i++) {
      final x = size.width * i / 8;
      for (var y = 0.0; y < size.height; y += 7) {
        canvas.drawLine(Offset(x, y), Offset(x, y + 2.5), grid);
      }
    }
    final yMedia = size.height - 1 / vMax * size.height;
    final media = Paint()
      ..color = Colors.white54
      ..strokeWidth = 1;
    for (var x = 0.0; x < size.width; x += 9) {
      canvas.drawLine(Offset(x, yMedia), Offset(x + 4.5, yMedia), media);
    }

    // A derivada ANALITICA da curva de valor (ver Easing.speedAt): e o
    // que faz o grafico sair liso em vez de serrilhado.
    Offset plot(double x, double v) => Offset(
      x * size.width,
      size.height - v.clamp(0, vMax) / vMax * size.height,
    );
    final path = Path();
    const n = 96;
    for (var i = 0; i <= n; i++) {
      final t = i / n;
      final p = plot(t, ease.speedAt(t));
      if (i == 0) {
        path.moveTo(p.dx, p.dy);
      } else {
        path.lineTo(p.dx, p.dy);
      }
    }
    // Area sob a curva: velocidade x tempo = deslocamento. Ver a area e
    // ver que o trecho inteiro percorre o mesmo caminho, so redistribui.
    final area = Path.from(path)
      ..lineTo(size.width, size.height)
      ..lineTo(0, size.height)
      ..close();
    canvas.drawPath(
      area,
      Paint()..color = AmColors.accent.withValues(alpha: 0.12),
    );
    canvas.drawPath(
      path,
      Paint()
        ..color = Colors.white
        ..strokeWidth = 4
        ..style = PaintingStyle.stroke
        ..strokeCap = StrokeCap.round,
    );

    // O ponto que corre, na velocidade do instante.
    final andando = percorrido;
    if (andando != null) {
      final p = plot(andando, ease.speedAt(andando));
      canvas.drawLine(
        Offset(p.dx, 0),
        Offset(p.dx, size.height),
        Paint()..color = AmColors.pink.withValues(alpha: 0.35),
      );
      canvas.drawCircle(p, 8, Paint()..color = AmColors.pink);
    }

    // As duas alcas: influencia (x) e velocidade (y) de cada ponta.
    if (ease.type == EasingType.cubicBezier) {
      final alca = Paint()..color = Colors.white;
      final guia = Paint()
        ..color = AmColors.tealBright
        ..strokeWidth = 1.2;
      canvas.drawLine(Offset(0, pIni.dy), pIni, guia);
      canvas.drawLine(pFim, Offset(size.width, pFim.dy), guia);
      canvas.drawCircle(pIni, 13, alca);
      canvas.drawCircle(pFim, 13, alca);
    }
  }

  @override
  bool shouldRepaint(_SpeedPainter old) =>
      old.ease != ease || old.percorrido != percorrido;
}

class _AmCurvePainter extends CustomPainter {
  const _AmCurvePainter({
    required this.ease,
    required this.yMin,
    required this.yMax,
    this.percorrido,
  });

  final Easing ease;
  final double yMin;
  final double yMax;

  /// Onde o cabecote esta dentro do trecho, 0..1. Nulo = fora.
  final double? percorrido;

  Offset _pt(Size size, double x, double y) => Offset(
    x * size.width,
    size.height - (y - yMin) / (yMax - yMin) * size.height,
  );

  @override
  void paint(Canvas canvas, Size size) {
    // Grade pontilhada.
    final grid = Paint()
      ..color = const Color(0xFF34405A)
      ..strokeWidth = 1;
    for (var i = 1; i < 8; i++) {
      final x = size.width * i / 8;
      for (var y = 0.0; y < size.height; y += 7) {
        canvas.drawLine(Offset(x, y), Offset(x, y + 2.5), grid);
      }
    }
    for (var i = 1; i < 8; i++) {
      final y = size.height * i / 8;
      for (var x = 0.0; x < size.width; x += 7) {
        canvas.drawLine(Offset(x, y), Offset(x + 2.5, y), grid);
      }
    }
    // Limites (y=0 / y=1) tracejados claros.
    final dash = Paint()
      ..color = Colors.white24
      ..strokeWidth = 1;
    for (final y in [0.0, 1.0]) {
      final py = _pt(size, 0, y).dy;
      for (var x = 0.0; x < size.width; x += 9) {
        canvas.drawLine(Offset(x, py), Offset(x + 4.5, py), dash);
      }
    }

    // O PREENCHIMENTO SOB A CURVA
    final base = _pt(size, 0, 0).dy;
    final area = Path()..moveTo(0, base);
    for (var i = 0; i <= 72; i++) {
      final t = i / 72;
      final q = _pt(size, t, ease.transform(t));
      area.lineTo(q.dx, q.dy);
    }
    area
      ..lineTo(size.width, base)
      ..close();
    canvas.drawPath(
      area,
      Paint()..color = AmColors.accent.withValues(alpha: 0.10),
    );

    // Curva: verde vibrante contínuo estilo Alight Motion
    Path buildPath(double from, double to) {
      final path = Path();
      var first = true;
      for (var i = 0; i <= 72; i++) {
        final t = from + (to - from) * i / 72;
        final p = _pt(size, t, ease.transform(t));
        if (first) {
          path.moveTo(p.dx, p.dy);
          first = false;
        } else {
          path.lineTo(p.dx, p.dy);
        }
      }
      return path;
    }

    canvas.drawPath(
      buildPath(0, 1),
      Paint()
        ..color = const Color(0xFF1ED6B1)
        ..strokeWidth = 3.5
        ..style = PaintingStyle.stroke
        ..strokeCap = StrokeCap.round,
    );

    // O PONTO QUE CORRE, na posicao do instante atual.
    final andando = percorrido;
    if (andando != null) {
      final p = _pt(size, andando, ease.transform(andando));
      final guia = Paint()
        ..color = AmColors.pink.withValues(alpha: 0.35)
        ..strokeWidth = 1;
      canvas.drawLine(Offset(p.dx, 0), Offset(p.dx, size.height), guia);
      canvas.drawCircle(p, 8, Paint()..color = AmColors.pink);
      canvas.drawCircle(
        p,
        8,
        Paint()
          ..color = Colors.white
          ..strokeWidth = 2
          ..style = PaintingStyle.stroke,
      );
    }

    // Pontos das extremidades + alcas com linhas brancas e circulos brancos solidos
    final p0 = _pt(size, 0, 0), p1 = _pt(size, 1, 1);
    if (ease.type == EasingType.cubicBezier) {
      final h1 = _pt(size, ease.x1, ease.y1);
      final h2 = _pt(size, ease.x2, ease.y2);

      // Linhas verticais tracejadas caindo das alças até a linha de base
      final vDash = Paint()
        ..color = Colors.white30
        ..strokeWidth = 1;
      for (final h in [h1, h2]) {
        final minY = math.min(h.dy, base);
        final maxY = math.max(h.dy, base);
        for (var y = minY; y < maxY; y += 6) {
          canvas.drawLine(
            Offset(h.dx, y),
            Offset(h.dx, math.min(y + 3, maxY)),
            vDash,
          );
        }
      }

      // Linhas das alças: brancas sólidas e grossas
      final tangente = Paint()
        ..color = Colors.white
        ..strokeWidth = 2.5;
      canvas.drawLine(p0, h1, tangente);
      canvas.drawLine(p1, h2, tangente);

      // Bolas brancas sólidas nas pontas das alças
      final knobPaint = Paint()..color = Colors.white;
      for (final h in [h1, h2]) {
        canvas.drawCircle(h, 11, knobPaint);
      }
    }

    // AS ALCAS AMARELAS das familias parametricas, com as guias
    // pontilhadas ate a base — o mesmo desenho da referencia.
    final alcas = AlcasParametricas.de(ease);
    if (alcas.isNotEmpty) {
      final guia = Paint()
        ..color = const Color(0xFFFFD84D).withValues(alpha: .55)
        ..strokeWidth = 1;
      final bola = Paint()..color = const Color(0xFFFFD84D);
      final aro = Paint()
        ..color = Colors.white
        ..strokeWidth = 1.5
        ..style = PaintingStyle.stroke;
      for (final (x, y) in alcas) {
        final h = _pt(size, x, y);
        final minY = math.min(h.dy, base);
        final maxY = math.max(h.dy, base);
        for (var yy = minY; yy < maxY; yy += 6) {
          canvas.drawLine(
            Offset(h.dx, yy),
            Offset(h.dx, math.min(yy + 3, maxY)),
            guia,
          );
        }
        canvas.drawCircle(h, 10, bola);
        canvas.drawCircle(h, 10, aro);
      }
    }

    // Pontos de ancoragem verdes nos cantos
    final endDot = Paint()..color = const Color(0xFF1ED6B1);
    canvas.drawCircle(p0, 4.5, endDot);
    canvas.drawCircle(p1, 4.5, endDot);
  }

  @override
  bool shouldRepaint(_AmCurvePainter old) =>
      old.ease != ease || old.percorrido != percorrido;
}

class _PresetTile extends StatelessWidget {
  const _PresetTile({
    required this.ease,
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final Easing ease;
  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: Container(
        width: 74,
        margin: const EdgeInsets.only(right: 6),
        decoration: BoxDecoration(
          color: selected ? AmColors.panelHigh : AmColors.bg,
          borderRadius: BorderRadius.circular(10),
          border: selected
              ? Border.all(color: AmColors.accent, width: 1.5)
              : null,
        ),
        child: Column(
          children: [
            Expanded(
              child: CustomPaint(
                painter: _PresetThumbPainter(ease: ease, selected: selected),
                child: const SizedBox.expand(),
              ),
            ),
            Padding(
              padding: const EdgeInsets.fromLTRB(3, 0, 3, 3),
              child: AppText(
                label,
                // UMA LINHA. Duas linhas de corpo 11 nao cabem nos 64 px
                // da faixa junto com a miniatura — estouravam por dois
                // pixels — e um nome cortado com reticencias ainda se le;
                // uma faixa amarela de estouro, nao.
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                textAlign: TextAlign.center,
                style: TextStyle(
                  fontSize: 11,
                  height: 1.05,
                  fontWeight: FontWeight.w600,
                  color: selected ? AmColors.accent : AmColors.muted,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Escolha explícita exigida antes de aplicar um preset: só o trecho sob o
/// playhead ou todos os segmentos da propriedade.
class _ScopeToggle extends StatelessWidget {
  const _ScopeToggle({
    required this.todos,
    required this.onChanged,
    this.horizontal = false,
  });

  final bool todos;
  final ValueChanged<bool> onChanged;
  final bool horizontal;

  @override
  Widget build(BuildContext context) {
    Widget item(String text, bool value) => GestureDetector(
      onTap: () => onChanged(value),
      child: Container(
        alignment: Alignment.center,
        // Vertical 5: duas destas empilhadas tem de caber nos 64 px da
        // faixa de presets. Com 7 estouravam por dois pixels.
        padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 5),
        decoration: BoxDecoration(
          color: todos == value ? AmColors.accent : AmColors.bg,
          borderRadius: BorderRadius.circular(8),
        ),
        child: AppText(
          text,
          maxLines: 1,
          style: TextStyle(
            fontSize: 11,
            fontWeight: FontWeight.w700,
            color: todos == value ? AmColors.bg : AmColors.muted,
          ),
        ),
      ),
    );
    final children = [item('Trecho', false), item('Todos', true)];
    return horizontal
        ? Row(
            children: [
              Expanded(child: children[0]),
              const SizedBox(width: 4),
              Expanded(child: children[1]),
            ],
          )
        : Column(
            children: [children[0], const SizedBox(height: 4), children[1]],
          );
  }
}

class _PresetThumbPainter extends CustomPainter {
  const _PresetThumbPainter({required this.ease, required this.selected});

  final Easing ease;
  final bool selected;

  @override
  void paint(Canvas canvas, Size size) {
    const pad = 6.0;
    final w = size.width - pad * 2;
    final h = size.height - pad * 2;
    final paint = Paint()
      ..color = selected ? AmColors.accent : Colors.white70
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;
    final path = Path();
    for (var i = 0; i <= 40; i++) {
      final t = i / 40;
      final v = ease.transform(t).clamp(-0.3, 1.3);
      final p = Offset(pad + t * w, pad + h - v * h);
      if (i == 0) {
        path.moveTo(p.dx, p.dy);
      } else {
        path.lineTo(p.dx, p.dy);
      }
    }
    canvas.drawPath(path, paint);
    final dot = Paint()..color = selected ? AmColors.accent : Colors.white;
    canvas.drawCircle(Offset(pad, pad + h), 2.5, dot);
    canvas.drawCircle(Offset(pad + w, pad), 2.5, dot);
  }

  @override
  bool shouldRepaint(_PresetThumbPainter old) =>
      old.ease != ease || old.selected != selected;
}

class _AmCurvePresetCard extends StatelessWidget {
  const _AmCurvePresetCard({
    required this.ease,
    required this.selected,
    required this.onTap,
  });

  final Easing ease;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Container(
        width: 38,
        height: 38,
        decoration: BoxDecoration(
          color: const Color(0xFF1E222D),
          borderRadius: BorderRadius.circular(8),
          border: Border.all(
            color: selected ? const Color(0xFF1ED6B1) : const Color(0xFF333B4F),
            width: selected ? 1.8 : 1,
          ),
        ),
        child: CustomPaint(
          painter: _PresetThumbPainter(ease: ease, selected: selected),
        ),
      ),
    );
  }
}

/// AS FAMILIAS DA CURVA (v1.1.1): cada uma com os seus presets.
const _familiasDaCurva =
    <({String nome, IconData icone, List<(String, Easing)> presets})>[
      (
        nome: 'Bézier',
        icone: CupertinoIcons.scribble,
        presets: [
          ('Linear', Easing.linear),
          ('Suave na entrada', Easing.easeIn),
          ('Suave na saída', Easing.easeOut),
          ('Suave nas duas pontas', Easing.easeInOut),
        ],
      ),
      (
        nome: 'Quique',
        icone: CupertinoIcons.sportscourt,
        presets: [
          ('Quique na saída', Easing.bounce),
          ('Quique na entrada', Easing.bounceIn),
          ('Elástico na saída', Easing.elastic),
          ('Elástico na entrada', Easing.elasticIn),
        ],
      ),
      (
        nome: 'Degraus',
        icone: CupertinoIcons.chart_bar_alt_fill,
        presets: [
          ('Degraus', Easing.steps),
          ('Degraus aleatórios', Easing.stepsRandom),
          ('Degraus elásticos', Easing.elasticSteps),
          ('Manter', Easing.hold),
        ],
      ),
      (
        nome: 'Outras',
        icone: CupertinoIcons.waveform_path,
        presets: [
          ('Oscilar', Easing.oscillate),
          ('Cíclica', Easing.cyclic),
          ('Aleatória', Easing.random),
          ('Repetir', Easing.repeat),
          ('Dente de serra', Easing.sawtooth),
          ('Mola', Easing.interfaceSpring),
        ],
      ),
    ];

int _familiaDe(Easing e) => switch (e.type) {
  EasingType.cubicBezier => 0,
  EasingType.bounce ||
  EasingType.bounceIn ||
  EasingType.elastic ||
  EasingType.elasticIn => 1,
  EasingType.steps ||
  EasingType.stepsRandom ||
  EasingType.elasticSteps ||
  EasingType.hold => 2,
  _ => 3,
};

class _FamiliasDaCurva extends StatefulWidget {
  const _FamiliasDaCurva({required this.ease, required this.onEscolher});

  final Easing ease;
  final ValueChanged<Easing> onEscolher;

  @override
  State<_FamiliasDaCurva> createState() => _FamiliasDaCurvaState();
}

class _FamiliasDaCurvaState extends State<_FamiliasDaCurva> {
  late int _aba = _familiaDe(widget.ease);

  @override
  Widget build(BuildContext context) {
    final familia = _familiasDaCurva[_aba];
    return SizedBox(
      width: 132,
      child: Row(
        children: [
          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(2, 6, 2, 6),
              child: Wrap(
                alignment: WrapAlignment.center,
                spacing: 8,
                runSpacing: 8,
                children: [
                  for (final (nome, preset) in familia.presets)
                    Tooltip(
                      message: nome,
                      child: KeyedSubtree(
                        key: ValueKey('curva-preset-$nome'),
                        child: _AmCurvePresetCard(
                          ease: preset,
                          selected: _CurvePanelState._samePreset(
                            widget.ease,
                            preset,
                          ),
                          onTap: () => widget.onEscolher(preset),
                        ),
                      ),
                    ),
                ],
              ),
            ),
          ),
          // AS ABAS VERTICAIS: a familia aberta fica acesa.
          Container(
            width: 34,
            color: AmColors.panelHigh,
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                for (var i = 0; i < _familiasDaCurva.length; i++)
                  Tooltip(
                    message: _familiasDaCurva[i].nome,
                    child: GestureDetector(
                      key: ValueKey('curva-familia-$i'),
                      behavior: HitTestBehavior.opaque,
                      onTap: () => setState(() => _aba = i),
                      child: SizedBox(
                        width: 34,
                        height: 40,
                        child: Icon(
                          _familiasDaCurva[i].icone,
                          size: 17,
                          color: i == _aba ? AmColors.accent : AmColors.muted,
                        ),
                      ),
                    ),
                  ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}
