import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/scheduler.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

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
  bool _aplicarEmTodos = false;

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
                  const Text(
                    'Crie pelo menos 2 keyframes\npara editar a curva.',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: AmColors.muted, fontSize: 13),
                  ),
                  const SizedBox(height: 12),
                  CupertinoButton(
                    onPressed: widget.onBack,
                    child: const Text(
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
                Column(
                  children: [
                    Flexible(
                      child: AmRailButton(
                        onTap: widget.onBack,
                        child: const Icon(
                          CupertinoIcons.chevron_back,
                          size: 24,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    const Spacer(),
                    Flexible(
                      child: AmRailButton(
                        onTap: () {
                          // Inverte a curva (espelha as alcas).
                          controller.setSegmentEase(
                            id,
                            widget.prop,
                            segment!.$1,
                            ease.copyWith(
                              x1: (1 - ease.x2).clamp(0.0, 1.0),
                              y1: 1 - ease.y2,
                              x2: (1 - ease.x1).clamp(0.0, 1.0),
                              y2: 1 - ease.y1,
                            ),
                          );
                        },
                        child: const Icon(
                          CupertinoIcons.arrow_2_squarepath,
                          size: 22,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                    // VALOR / VELOCIDADE: o mesmo segmento, visto pela
                    // derivada. E onde a curva em S se ajusta de verdade.
                    Flexible(
                      child: AmRailButton(
                        selected: _velocidade,
                        onTap: () => setState(() => _velocidade = !_velocidade),
                        child: Icon(
                          CupertinoIcons.speedometer,
                          size: 20,
                          color: _velocidade ? AmColors.accent : AmColors.text,
                        ),
                      ),
                    ),
                    Flexible(
                      child: PopupMenuButton<String>(
                        tooltip: 'Opções da curva',
                        icon: const Icon(
                          CupertinoIcons.ellipsis,
                          color: AmColors.text,
                        ),
                        color: AmColors.panelHigh,
                        itemBuilder: (_) => [
                          const PopupMenuItem(
                            value: 'copy',
                            child: Text('Copiar curva'),
                          ),
                          PopupMenuItem(
                            value: 'paste',
                            enabled: EasingClipboard.valor != null,
                            child: const Text('Colar curva'),
                          ),
                          const PopupMenuItem(
                            value: 'all',
                            child: Text('Aplicar em todos os segmentos'),
                          ),
                          CheckedPopupMenuItem(
                            value: 'overshoot',
                            checked: _overshoot,
                            child: const Text('Overshoot'),
                          ),
                        ],
                        onSelected: (action) {
                          switch (action) {
                            case 'copy':
                              setState(() => EasingClipboard.valor = ease);
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
                          }
                        },
                      ),
                    ),
                    const SizedBox(height: 4),
                  ],
                ),
                // Grafico + navegacao.
                Expanded(
                  child: Column(
                    children: [
                      const SizedBox(height: 6),
                      Expanded(
                        child: Opacity(
                          // Esmaecido diz "isto nao e o que esta sob o
                          // cabecote agora" sem sumir com a curva.
                          opacity: foraDoTrecho ? 0.45 : 1,
                          child: _velocidade
                              ? _SpeedGraph(
                                  ease: ease,
                                  percorrido: percorrido,
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
                      SizedBox(
                        height: 32,
                        child: Row(
                          children: [
                            CupertinoButton(
                              padding: const EdgeInsets.all(8),
                              onPressed: () => _jumpSegment(layer, -1),
                              child: const Icon(
                                CupertinoIcons.chevron_left,
                                size: 18,
                                color: AmColors.muted,
                              ),
                            ),
                            Expanded(
                              child: Text(
                                foraDoTrecho
                                    ? 'Trecho ${times.indexOf(segment.$1) + 1}'
                                          '\u2192${times.indexOf(segment.$1) + 2}'
                                          ' \u00b7 ${ease.label}'
                                          ' (cabecote fora)'
                                    : 'Efeito Ease de ${ease.label}',
                                textAlign: TextAlign.center,
                                maxLines: 2,
                                style: const TextStyle(
                                  fontSize: 12,
                                  color: AmColors.muted,
                                ),
                              ),
                            ),
                            CupertinoButton(
                              padding: const EdgeInsets.all(8),
                              onPressed: () => _jumpSegment(layer, 1),
                              child: const Icon(
                                CupertinoIcons.chevron_right,
                                size: 18,
                                color: AmColors.muted,
                              ),
                            ),
                          ],
                        ),
                      ),
                    ],
                  ),
                ),
                // Thumbnails de preset a direita.
                SizedBox(
                  width: 64,
                  child: ListView(
                    padding: const EdgeInsets.fromLTRB(4, 6, 4, 6),
                    children: [
                      _ScopeToggle(
                        todos: _aplicarEmTodos,
                        onChanged: (v) => setState(() => _aplicarEmTodos = v),
                      ),
                      const SizedBox(height: 8),
                      for (final preset in _presets)
                        _PresetTile(
                          ease: preset.ease,
                          label: preset.nome,
                          selected: _samePreset(ease, preset.ease),
                          onTap: () => _aplicarEmTodos
                              ? controller.applyEaseToAllSegments(
                                  id,
                                  widget.prop,
                                  preset.ease,
                                )
                              : controller.setSegmentEase(
                                  id,
                                  widget.prop,
                                  segment!.$1,
                                  preset.ease,
                                ),
                        ),
                    ],
                  ),
                ),
              ],
            ),
    );
  }

  static const _presets = <({String nome, Easing ease})>[
    (nome: 'Apple padrão', ease: Easing.appleStandard),
    (nome: 'Apple entrada', ease: Easing.appleEntrance),
    (nome: 'Apple saída', ease: Easing.appleExit),
    (nome: 'Mola interface', ease: Easing.interfaceSpring),
    (nome: 'Mola suave', ease: Easing.softSpring),
    (nome: 'Linear', ease: Easing.linear),
    (nome: 'Ease in', ease: Easing.easeIn),
    (nome: 'Ease out', ease: Easing.easeOut),
    (nome: 'Ease in-out', ease: Easing.easeInOut),
    (nome: 'Overshoot', ease: Easing.overshoot),
    (nome: 'Quicar', ease: Easing.bounce),
    (nome: 'Elástico', ease: Easing.elastic),
    (nome: 'Degraus', ease: Easing(type: EasingType.steps)),
    (nome: 'Cíclico', ease: Easing(type: EasingType.cyclic)),
  ];

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
                      Text(
                        'Curva — $label',
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
                              const Text(
                                'Crie 2+ keyframes neste parametro e leve o\n'
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
                                  child: const Text(
                                    'Ir ao primeiro trecho',
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
                          height: 150,
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
                          height: 70,
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
                                child: const Text(
                                  'Copiar',
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
                                child: const Text(
                                  'Colar',
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

class _CurveGraph extends StatelessWidget {
  const _CurveGraph({
    super.key,
    required this.ease,
    required this.overshootEnabled,
    required this.onBezierChanged,
    this.percorrido,
  });

  final Easing ease;
  final bool overshootEnabled;
  final ValueChanged<Easing> onBezierChanged;

  /// 0..1: onde o cabecote esta dentro do trecho. Nulo = esta fora.
  final double? percorrido;

  static const double _yMin = -0.5;
  static const double _yMax = 1.5;

  Offset _toPlot(Size size, double x, double y) => Offset(
    x * size.width,
    size.height - (y - _yMin) / (_yMax - _yMin) * size.height,
  );

  (double, double) _fromPlot(Size size, Offset p) => (
    (p.dx / size.width).clamp(0.0, 1.0),
    _yMin + (size.height - p.dy) / size.height * (_yMax - _yMin),
  );

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final size = Size(constraints.maxWidth, constraints.maxHeight);
        final h1 = _toPlot(size, ease.x1, ease.y1);
        final h2 = _toPlot(size, ease.x2, ease.y2);

        void drag(DragUpdateDetails d) {
          final p = d.localPosition;
          final near1 = (p - h1).distanceSquared < (p - h2).distanceSquared;
          var (x, y) = _fromPlot(size, p);
          if (!overshootEnabled) y = y.clamp(0.0, 1.0);
          onBezierChanged(
            near1 ? ease.copyWith(x1: x, y1: y) : ease.copyWith(x2: x, y2: y),
          );
        }

        final enabled = ease.type == EasingType.cubicBezier;
        // Reconhecedores vertical E horizontal (nao pan): dentro de um
        // bottom sheet persistente, o pan PERDE a arena de gestos para o
        // drag-de-fechar vertical do sheet — a alca "nao mexia" e o
        // gesto arrastava o sheet. Recognizer igual em no mais fundo
        // ganha a arena.
        return GestureDetector(
          onVerticalDragUpdate: enabled ? drag : null,
          onHorizontalDragUpdate: enabled ? drag : null,
          child: CustomPaint(
            size: size,
            painter: _AmCurvePainter(
              ease: ease,
              yMin: _yMin,
              yMax: _yMax,
              percorrido: percorrido,
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
class _SpeedGraph extends StatelessWidget {
  const _SpeedGraph({
    required this.ease,
    required this.onBezierChanged,
    this.percorrido,
  });

  final Easing ease;
  final ValueChanged<Easing> onBezierChanged;
  final double? percorrido;

  /// Velocidade maxima mostrada, em "vezes a velocidade media".
  static const double _vMax = 4.0;

  // A velocidade nas pontas vem da mesma derivada analitica do grafico:
  // as alcas caem EM CIMA da curva, inclusive com a alca degenerada.
  static double _vIni(Easing e) => e.speedAt(0).clamp(0.0, _vMax);
  static double _vFim(Easing e) => e.speedAt(1).clamp(0.0, _vMax);

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, c) {
        final size = Size(c.maxWidth, c.maxHeight);
        Offset plot(double x, double v) =>
            Offset(x * size.width, size.height - v / _vMax * size.height);

        final pIni = plot(ease.x1, _vIni(ease));
        final pFim = plot(ease.x2, _vFim(ease));

        void drag(DragUpdateDetails d) {
          final p = d.localPosition;
          final pertoIni =
              (p - pIni).distanceSquared <= (p - pFim).distanceSquared;
          final x = (p.dx / size.width).clamp(0.02, 0.98);
          final v = ((size.height - p.dy) / size.height * _vMax).clamp(
            0.0,
            _vMax,
          );
          if (pertoIni) {
            // influencia = x1; velocidade inicial = y1/x1 -> y1 = v * x1.
            final x1 = math.min(x, ease.x2 - 0.02);
            onBezierChanged(
              ease.copyWith(x1: x1, y1: (v * x1).clamp(-2.0, 2.0)),
            );
          } else {
            // influencia = 1-x2; velocidade final = (1-y2)/(1-x2).
            final x2 = math.max(x, ease.x1 + 0.02);
            onBezierChanged(
              ease.copyWith(x2: x2, y2: (1 - v * (1 - x2)).clamp(-1.0, 3.0)),
            );
          }
        }

        final editavel = ease.type == EasingType.cubicBezier;
        return GestureDetector(
          onVerticalDragUpdate: editavel ? drag : null,
          onHorizontalDragUpdate: editavel ? drag : null,
          child: CustomPaint(
            size: size,
            painter: _SpeedPainter(
              ease: ease,
              vMax: _vMax,
              pIni: pIni,
              pFim: pFim,
              percorrido: percorrido,
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
      ..color = Colors.white54
      ..strokeWidth = 1;
    for (final y in [0.0, 1.0]) {
      final py = _pt(size, 0, y).dy;
      for (var x = 0.0; x < size.width; x += 9) {
        canvas.drawLine(Offset(x, py), Offset(x + 4.5, py), dash);
      }
    }

    // Curva: branca com o miolo verde (estilo AM).
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
        ..color = Colors.white
        ..strokeWidth = 4
        ..style = PaintingStyle.stroke
        ..strokeCap = StrokeCap.round,
    );
    canvas.drawPath(
      buildPath(0.18, 0.82),
      Paint()
        ..color = AmColors.accent
        ..strokeWidth = 4
        ..style = PaintingStyle.stroke
        ..strokeCap = StrokeCap.round,
    );

    // O PONTO QUE CORRE, na posicao do instante atual.
    //
    // Ver a curva sendo percorrida enquanto a animacao toca e o que
    // transforma o grafico em leitura: da para ver a aceleracao
    // acontecendo, em vez de deduzi-la do desenho parado.
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

    // Pontos das extremidades + alcas.
    final endDot = Paint()..color = AmColors.accent;
    canvas.drawCircle(_pt(size, 0, 0), 5, endDot);
    canvas.drawCircle(_pt(size, 1, 1), 5, endDot);
    if (ease.type == EasingType.cubicBezier) {
      final handle = Paint()..color = Colors.white;
      canvas.drawCircle(_pt(size, ease.x1, ease.y1), 13, handle);
      canvas.drawCircle(_pt(size, ease.x2, ease.y2), 13, handle);
    }
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
        height: 60,
        margin: const EdgeInsets.only(bottom: 8),
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
              padding: const EdgeInsets.fromLTRB(3, 0, 3, 4),
              child: Text(
                label,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                textAlign: TextAlign.center,
                style: TextStyle(
                  fontSize: 8.5,
                  height: 1,
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
        padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 5),
        decoration: BoxDecoration(
          color: todos == value ? AmColors.accent : AmColors.bg,
          borderRadius: BorderRadius.circular(6),
        ),
        child: Text(
          text,
          maxLines: 1,
          style: TextStyle(
            fontSize: 9,
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
    const pad = 10.0;
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
    canvas.drawCircle(Offset(pad, pad + h), 3, dot);
    canvas.drawCircle(Offset(pad + w, pad), 3, dot);
  }

  @override
  bool shouldRepaint(_PresetThumbPainter old) =>
      old.ease != ease || old.selected != selected;
}
