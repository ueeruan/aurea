import 'package:aurea/src/core/l10n/app_language.dart';

import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/ui/editor_session.dart';
import '../../application/playback_controller.dart';
import '../../domain/angulo.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'animador_sheet.dart';
import 'am_widgets.dart';
import 'area_de_arrasto.dart';
import 'panel_chrome.dart';
import '../context/parameter_row.dart';
import '../../application/ui/pro_mode.dart';
import 'property_keyframe_context.dart';
import '../widgets/painel_de_transformacao.dart';
import '../widgets/rails_do_painel.dart';

export '../../application/ui/editor_session.dart'
    show TransformTool, propOfTool;

/// Painel "Movimentacao e transformacao": trilho esquerdo (voltar,
/// keyframe, curva), controle central com navegacao de keyframes
/// e sub-ferramentas a direita (posicao/rotacao/escala/skew/pivo).
/// O losango de uma linha: estado da propriedade neste instante.
KeyframeState _kf(
  WidgetRef ref,
  Layer layer,
  LayerProp prop,
  Duration t,
  VoidCallback? onCurve,
) {
  final local = layer.localTime(t).inMicroseconds;
  final times = keyframeTimesForProp(layer, prop);
  return KeyframeState(
    animated: times.isNotEmpty,
    here: times.any((us) => (us - local).abs() < 8000),
    onToggle: () => ref
        .read(editorControllerProvider.notifier)
        .toggleKeyframe(layer.id, t, prop),
    onCurve: times.isNotEmpty ? onCurve : null,
  );
}

/// O toque longo no valor (Pro) abre a expressao da propriedade.
/// ANIMAR SOZINHO: o toque longo no nome da propriedade oferece o
/// animador automatico — a propriedade balança sem keyframe nenhum.
VoidCallback _animador(
  BuildContext context,
  WidgetRef ref,
  Layer layer,
  LayerProp prop,
  String nome, {
  String unidade = '',
}) =>
    () => showAnimadorSheet(
      context,
      ref,
      layer.id,
      prop,
      nome: nome,
      unidade: unidade,
    );

VoidCallback? _expressao(
  BuildContext context,
  WidgetRef ref,
  Layer layer,
  LayerProp prop,
  String nome,
) {
  if (!ref.watch(proModeProvider)) return null;
  return () async {
    final controller = ref.read(editorControllerProvider.notifier);
    final atual = controller.propExpression(layer, prop);
    final erro = switch (prop) {
      LayerProp.opacity => layer.opacity.expressionError?.mensagem,
      LayerProp.rotation => layer.rotation.expressionError?.mensagem,
      LayerProp.scale => layer.scaleX.expressionError?.mensagem,
      LayerProp.skew => layer.skewX.expressionError?.mensagem,
      _ => null,
    };
    final r = await showExpressionEditor(
      context,
      atual: atual,
      erro: erro,
      nome: nome,
    );
    if (r == null) return;
    controller.setPropExpression(layer.id, prop, r);
  };
}

class TransformPanel extends ConsumerStatefulWidget {
  const TransformPanel({
    super.key,
    required this.playback,
    required this.tool,
    required this.onToolChanged,
    required this.onBack,
    required this.onOpenCurve,
  });

  final PlaybackController playback;
  final TransformTool tool;
  final ValueChanged<TransformTool> onToolChanged;
  final VoidCallback onBack;
  final void Function(LayerProp prop) onOpenCurve;

  @override
  ConsumerState<TransformPanel> createState() => _TransformPanelState();
}

class _TransformPanelState extends ConsumerState<TransformPanel> {
  bool _scaleLinked = true;
  final _bodyScroll = ScrollController();
  final _positionKey = GlobalKey<_PositionControlState>();

  @override
  void dispose() {
    _bodyScroll.dispose();
    super.dispose();
  }

  LayerProp get _prop => propOfTool(widget.tool);

  Widget _options(EditorController controller, String id, Layer layer) {
    final autoKey = ref.watch(autoKeyframeProvider);
    final linked =
        ref.watch(editorControllerProvider).linkFor(id, LayerProp.position) !=
        null;
    return PopupMenuButton<String>(
      tooltip: autoKey
          ? 'Opções de transformação · auto-key ligado'
          : 'Opções de transformação',
      icon: AmMenuIcon(
        ativo:
            autoKey ||
            layer.is3D ||
            linked ||
            widget.tool == TransformTool.pivot,
      ),
      color: AmColors.panelHigh,
      itemBuilder: (_) => [
        CheckedPopupMenuItem(
          value: '3d',
          checked: layer.is3D,
          child: const AppText('Transformação 3D'),
        ),
        if (widget.tool == TransformTool.position)
          PopupMenuItem(
            value: 'link',
            child: AppText(linked ? 'Desvincular posição' : 'Vincular posição'),
          ),
        CheckedPopupMenuItem(
          value: 'auto',
          checked: ref.read(autoKeyframeProvider),
          child: const AppText('Auto-key'),
        ),
        const PopupMenuItem(
          value: 'previous',
          child: AppText('Keyframe anterior'),
        ),
        const PopupMenuItem(value: 'next', child: AppText('Próximo keyframe')),
        const PopupMenuItem(
          value: 'reset',
          child: AppText('Resetar propriedade'),
        ),
        CheckedPopupMenuItem(
          value: 'pivot',
          checked: widget.tool == TransformTool.pivot,
          child: const AppText('Editar pivô'),
        ),
        CheckedPopupMenuItem(
          value: 'opacity',
          checked: widget.tool == TransformTool.opacity,
          child: const AppText('Opacidade'),
        ),
      ],
      onSelected: (value) {
        if (value == 'auto') {
          final setting = ref.read(autoKeyframeProvider.notifier);
          setting.state = !setting.state;
        } else if (value == '3d') {
          controller.toggle3D(id);
        } else if (value == 'link') {
          if (linked) {
            controller.unlinkProperty(id, LayerProp.position);
          } else {
            _positionKey.currentState?._pickLinkSource(
              context,
              ref,
              widget.playback.time.value,
            );
          }
        } else if (value == 'pivot') {
          widget.onToolChanged(TransformTool.pivot);
        } else if (value == 'opacity') {
          widget.onToolChanged(TransformTool.opacity);
        } else if (value == 'reset') {
          controller.resetProp(id, _prop);
        } else {
          final times = keyframeTimesForProp(layer, _prop).toList()..sort();
          final local = layer
              .localTime(widget.playback.time.value)
              .inMicroseconds;
          final target = value == 'previous'
              ? times.where((us) => us < local - 8000).lastOrNull
              : times.where((us) => us > local + 8000).firstOrNull;
          if (target != null) {
            widget.playback.pause();
            widget.playback.seek(
              layer.startTime + Duration(microseconds: target),
            );
          }
        }
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    ref.watch(autoKeyframeProvider);
    final project = ref.watch(projetoVisivelProvider);
    final id = ref.watch(selectedLayerProvider);
    final layer = id == null ? null : project.layerById(id);
    if (layer == null || id == null) {
      return const ColoredBox(color: AmColors.panel);
    }
    final controller = ref.read(editorControllerProvider.notifier);

    // O painel ESCUTA o relogio: sem isso, o `t` capturado no build fica
    // velho apos scrub na timeline e o diamante marcava keyframe no tempo
    // de quando o painel abriu (o bug do "keyframe fora do playhead").
    return ValueListenableBuilder<Duration>(
      valueListenable: widget.playback.time,
      builder: (context, t, _) {
        return PainelDeTransformacao(
          camada: layer,
          tempo: t,
          playback: widget.playback,
          aoVoltar: widget.onBack,
          mais: _options(controller, id, layer),
          alvoDoRail: (modo) {
            final prop = switch (modo) {
              ModoDeTransformacao.mover => LayerProp.position,
              ModoDeTransformacao.girar => LayerProp.rotation,
              ModoDeTransformacao.escalar => LayerProp.scale,
              ModoDeTransformacao.inclinar => LayerProp.skew,
            };
            final realLayer =
                ref.watch(editorControllerProvider).layerById(id) ?? layer;
            final local = realLayer.localTime(t);
            final times = keyframeTimesForProp(realLayer, prop);
            final hasKfHere = times.any(
              (us) => (us - local.inMicroseconds).abs() < 8000,
            );
            return AlvoDoRail(
              temKeyframeAqui: hasKfHere,
              animado: times.isNotEmpty,
              aoAlternarKeyframe: () => controller.toggleKeyframe(id, t, prop),
              aoAbrirCurva: times.length >= 2
                  ? () => widget.onOpenCurve(prop)
                  : null,
            );
          },
        );
      },
    );
  }

  // ignore: unused_element
  Widget _buildBody(
    BuildContext context,
    EditorController controller,
    String id,
    Layer layer,
    Duration t,
    bool hasKfHere,
    bool animated,
  ) {
    return Column(
      children: [
        Expanded(
          child: AmPanelChrome(
            compact: true,
            spacious: true,
            more: _options(controller, id, layer),
            onBack: widget.onBack,
            animado: animated,
            temKfAqui: hasKfHere,
            // Le o relogio NO TOQUE: o keyframe cai exatamente onde o cabecote
            // esta agora, nunca num tempo capturado antes.
            onCravar: () => controller.toggleKeyframe(
              id,
              widget.playback.time.value,
              _prop,
            ),
            onCurva: () => widget.onOpenCurve(_prop),
            abas: [
              ParamTab(
                id: TransformTool.position.name,
                label: 'Mover',
                icone: CupertinoIcons.move,
                animated: layer.position.isAnimated,
              ),
              ParamTab(
                id: TransformTool.rotation.name,
                label: 'Girar',
                icone: CupertinoIcons.rotate_right,
                animated:
                    layer.rotation.isAnimated ||
                    layer.rotationX.isAnimated ||
                    layer.rotationY.isAnimated,
              ),
              ParamTab(
                id: TransformTool.scale.name,
                label: 'Escalar',
                icone: CupertinoIcons.arrow_up_left_arrow_down_right,
                animated: layer.scaleX.isAnimated || layer.scaleY.isAnimated,
              ),
              ParamTab(
                id: TransformTool.skew.name,
                label: 'Inclinar',
                icone: CupertinoIcons.rectangle_expand_vertical,
                animated: layer.skewX.isAnimated || layer.skewY.isAnimated,
              ),
            ],
            abaAtiva: widget.tool == TransformTool.pivot
                ? TransformTool.position.name
                : widget.tool.name,
            onAba: (nome) => widget.onToolChanged(
              TransformTool.values.firstWhere((e) => e.name == nome),
            ),
            corpo: LayoutBuilder(
              builder: (context, constraints) => Scrollbar(
                controller: _bodyScroll,
                thumbVisibility:
                    constraints.maxHeight < (layer.is3D ? 340 : 160),
                child: SingleChildScrollView(
                  controller: _bodyScroll,
                  child: SizedBox(
                    height: math.max(
                      layer.is3D ? 340 : 160,
                      constraints.maxHeight,
                    ),
                    child: Padding(
                      padding: const EdgeInsets.fromLTRB(6, 6, 6, 2),
                      child: switch (widget.tool) {
                        TransformTool.position => _PositionControl(
                          key: _positionKey,
                          layer: layer,
                          playback: widget.playback,
                        ),
                        TransformTool.rotation => _RotationControl(
                          layer: layer,
                          playback: widget.playback,
                        ),
                        TransformTool.scale => _ScaleControl(
                          layer: layer,
                          playback: widget.playback,
                          linked: _scaleLinked,
                          onToggleLink: () =>
                              setState(() => _scaleLinked = !_scaleLinked),
                        ),
                        TransformTool.skew => _SkewControl(
                          layer: layer,
                          playback: widget.playback,
                        ),
                        TransformTool.pivot => _PivotControl(
                          layer: layer,
                          playback: widget.playback,
                        ),
                        TransformTool.opacity => _OpacityControl(
                          layer: layer,
                          playback: widget.playback,
                        ),
                      },
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }

  /// O "mais" do trilho: o que nao merece botao proprio.
}

class _PositionControl extends ConsumerStatefulWidget {
  const _PositionControl({
    super.key,
    required this.layer,
    required this.playback,
  });

  final Layer layer;
  final PlaybackController playback;

  @override
  ConsumerState<_PositionControl> createState() => _PositionControlState();
}

class _PositionControlState extends ConsumerState<_PositionControl> {
  Layer get layer => widget.layer;
  PlaybackController get playback => widget.playback;

  Offset _dragStart = Offset.zero;
  Offset _accum = Offset.zero;

  Future<void> _pickLinkSource(
    BuildContext context,
    WidgetRef ref,
    Duration t,
  ) async {
    final project = ref.read(editorControllerProvider);
    final controller = ref.read(editorControllerProvider.notifier);
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: AmColors.panel,
      builder: (sheetContext) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Padding(
              padding: EdgeInsets.all(14),
              child: AppText(
                'Seguir a posicao de...',
                style: TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.w700,
                  color: AmColors.text,
                ),
              ),
            ),
            for (final other in project.layers)
              if (other.id != layer.id)
                ListTile(
                  title: AppText(
                    other.name,
                    style: const TextStyle(color: AmColors.text),
                  ),
                  onTap: () {
                    controller.linkProperty(
                      layer.id,
                      LayerProp.position,
                      other.id,
                      t,
                    );
                    Navigator.of(sheetContext).pop();
                  },
                ),
            const SizedBox(height: 8),
          ],
        ),
      ),
    );
  }

  /// Arrasto com trava de eixo (gesto claramente horizontal/vertical nao
  /// "sai torto") + snap no centro da composicao.
  void _onPadUpdate(Offset delta) {
    final controller = ref.read(editorControllerProvider.notifier);
    final project = ref.read(editorControllerProvider);
    final t = playback.time.value;
    _accum += delta * 2;

    var target = _dragStart + _accum;
    final adx = _accum.dx.abs();
    final ady = _accum.dy.abs();
    if (adx > 24 || ady > 24) {
      if (adx > ady * 2.5) {
        target = Offset(target.dx, _dragStart.dy);
      } else if (ady > adx * 2.5) {
        target = Offset(_dragStart.dx, target.dy);
      }
    }
    final cx = project.outputWidth / 2;
    final cy = project.outputHeight / 2;
    if ((target.dx - cx).abs() < 16) target = Offset(cx, target.dy);
    if ((target.dy - cy).abs() < 16) target = Offset(target.dx, cy);

    controller.editPosition(layer.id, t, target);
  }

  @override
  Widget build(BuildContext context) {
    final ref = this.ref;
    final t = playback.time.value;
    final local = layer.localTime(t);
    final pos = layer.position.valueAt(local);
    final z = layer.positionZ.valueAt(local);
    final controller = ref.read(editorControllerProvider.notifier);

    return Column(
      children: [
        ParameterPointRow(
          compact: true,
          label: translate(context, 'Posição'),
          x: pos.dx,
          y: pos.dy,
          z: z,
          onX: (v) => controller.editPosition(layer.id, t, Offset(v, pos.dy)),
          onY: (v) => controller.editPosition(layer.id, t, Offset(pos.dx, v)),
          onZ: (v) => controller.editPositionZ(layer.id, t, v),
          keyframe: _kf(ref, layer, LayerProp.position, t, null),
          onReset: () => controller.resetProp(layer.id, LayerProp.position),
          onAnimador: _animador(
            context,
            ref,
            layer,
            LayerProp.position,
            'a posição',
            unidade: 'px',
          ),
        ),
        const SizedBox(height: 4),
        // A REGUA DE PROFUNDIDADE VEM ANTES DA ALMOFADA. Ela ficava
        // depois, abaixo da dobra num painel curto: "nao to sentindo o Z
        // de profundidade" — porque o controle que da o Z nem aparecia
        // sem rolar. Agora e a segunda coisa do painel quando o 3D esta
        // ligado.
        // SEMPRE VISIVEL: "a camada Z ainda nao ta funcionando" — a regua
        // so existia com o 3D ja ligado, e numa camada comum nao havia onde
        // mexer. Mexer aqui liga o 3D da camada (editPositionZ).
        ...[
          Padding(
            padding: const EdgeInsets.fromLTRB(4, 0, 4, 2),
            child: Row(
              children: [
                const AppText(
                  'Profundidade',
                  style: TextStyle(fontSize: 11, color: AmColors.muted),
                ),
                const Spacer(),
                AppText(
                  '${z.round()}',
                  style: const TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w600,
                    color: AmColors.accent,
                  ),
                ),
              ],
            ),
          ),
          AmTickRuler(
            value: z,
            min: -1000,
            max: 4000,
            unitsPerPixel: 4,
            height: 34,
            onChanged: (v) => controller.editPositionZ(layer.id, t, v),
          ),
          const SizedBox(height: 4),
        ],
        Expanded(
          child: AreaDeArrasto(
            key: const ValueKey('position-drag-pad'),
            onStart: (_) {
              _dragStart = layer.position.valueAt(
                layer.localTime(playback.time.value),
              );
              _accum = Offset.zero;
            },
            onUpdate: (_, delta) => _onPadUpdate(delta),
            child: Container(
              decoration: BoxDecoration(color: AmColors.panel),
              child: const Center(
                child: AppText(
                  'Deslize aqui para mover a camada',
                  textAlign: TextAlign.center,
                  style: TextStyle(
                    fontSize: 12,
                    height: 1.2,
                    color: AmColors.muted,
                  ),
                ),
              ),
            ),
          ),
        ),
        const SizedBox(height: 4),
      ],
    );
  }
}

class _PivotControl extends ConsumerWidget {
  const _PivotControl({required this.layer, required this.playback});

  final Layer layer;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final t = playback.time.value;
    final pivot = layer.pivot.valueAt(layer.localTime(t));
    final controller = ref.read(editorControllerProvider.notifier);

    return Column(
      children: [
        ParameterPointRow(
          compact: true,
          label: translate(context, 'Pivô'),
          x: pivot.dx,
          y: pivot.dy,
          onX: (v) => controller.editPivot(layer.id, t, Offset(v, pivot.dy)),
          onY: (v) => controller.editPivot(layer.id, t, Offset(pivot.dx, v)),
          keyframe: _kf(ref, layer, LayerProp.pivot, t, null),
          onReset: () => controller.editPivot(layer.id, t, Offset.zero),
        ),
        const SizedBox(height: 10),
        Expanded(
          child: AreaDeArrasto(
            key: const ValueKey('pivot-drag-pad'),
            onUpdate: (_, delta) =>
                controller.editPivot(layer.id, t, pivot + delta * 2),
            onDoubleTap: () => controller.editPivot(layer.id, t, Offset.zero),
            child: Container(
              decoration: BoxDecoration(color: AmColors.panel),
              child: const Center(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Icon(
                      Icons.filter_center_focus,
                      size: 24,
                      color: AmColors.muted,
                    ),
                    SizedBox(height: 6),
                    AppText(
                      'Arraste o ponto de giro\n(toque duplo = centro)',
                      textAlign: TextAlign.center,
                      style: TextStyle(
                        fontSize: 12,
                        height: 1.2,
                        color: AmColors.muted,
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class _RotationControl extends ConsumerStatefulWidget {
  const _RotationControl({required this.layer, required this.playback});

  final Layer layer;
  final PlaybackController playback;

  @override
  ConsumerState<_RotationControl> createState() => _RotationControlState();
}

/// O DIAL E UM CONTADOR, NAO UM MOSTRADOR.
///
/// O dedo entrega, por atan2, um angulo entre -180 e 180. Antes esse
/// angulo era ESCRITO na camada: ao cruzar o lado esquerdo do dial o
/// valor saltava de 180 para -180, a animacao entre dois keyframes
/// "voltava ate o zero" pelo caminho contrario, e nunca havia como
/// passar de uma volta — o relato do beta, palavra por palavra.
///
/// Agora o valor da camada e o ACUMULADO dos giros do dedo desde o
/// comeco do gesto (ver [deltaDeAngulo]): cruza o limite sem saltar e
/// conta voltas (o mostrador diz "Nx"). Toque seco poe o angulo tocado
/// na volta em que a camada ja esta. Os chips dao voltas inteiras e
/// quartos sem precisar circular o dedo.
class _RotationControlState extends ConsumerState<_RotationControl> {
  double? _anguloDoDedo;
  double _acumulado = 0;

  double _angulo(Offset localPos, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    final v = localPos - center;
    return math.atan2(v.dy, v.dx) * 180 / math.pi;
  }

  /// O dedo POUSOU (antes de o arrasto ser reconhecido): e daqui que o
  /// giro conta, senao a folga de reconhecimento do gesto some do valor.
  void _pousar(Offset pos, Size size, Duration local) {
    _anguloDoDedo = _angulo(pos, size);
    _acumulado = widget.layer.rotation.valueAt(local);
  }

  void _comecar(Offset pos, Size size, Duration local) {
    if (_anguloDoDedo == null) _pousar(pos, size, local);
    ref.read(editorControllerProvider.notifier).beginGesture();
  }

  void _arrastar(Offset pos, Size size, Duration t) {
    final a = _angulo(pos, size);
    final anterior = _anguloDoDedo;
    _anguloDoDedo = a;
    if (anterior == null) return;
    _acumulado += deltaDeAngulo(anterior, a);
    ref
        .read(editorControllerProvider.notifier)
        .editRotation(widget.layer.id, t, _acumulado);
  }

  void _terminar() {
    _anguloDoDedo = null;
    ref.read(editorControllerProvider.notifier).endGesture();
  }

  void _tocar(Offset pos, Size size, Duration t, double atual) {
    final a = _angulo(pos, size);
    final alvo = anguloMaisProximo(a, atual);
    _anguloDoDedo = a;
    _acumulado = alvo;
    ref
        .read(editorControllerProvider.notifier)
        .editRotation(widget.layer.id, t, alvo);
  }

  @override
  Widget build(BuildContext context) {
    final layer = widget.layer;
    final playback = widget.playback;
    final t = playback.time.value;
    final local = layer.localTime(t);
    final deg = layer.rotation.valueAt(local);
    // Contador de voltas (aceita >360).
    final turns = (deg / 360).truncate();
    final controller = ref.read(editorControllerProvider.notifier);

    final dial = LayoutBuilder(
      builder: (context, constraints) {
        final size = Size(constraints.maxWidth, constraints.maxHeight);
        final radius = math.min(size.width, size.height) / 2 - 16;
        return AreaDeArrasto(
          key: const ValueKey('rotation-dial'),
          onStart: (p) {
            _pousar(p, size, local);
            _comecar(p, size, local);
          },
          onUpdate: (p, _) => _arrastar(p, size, t),
          onEnd: _terminar,
          // Toque seco: o angulo tocado, na volta em que a camada esta.
          onTap: (p) => _tocar(p, size, t, deg),
          child: Stack(
            alignment: Alignment.center,
            children: [
              CustomPaint(
                size: size,
                painter: _DialPainter(radius: radius),
              ),
              GestureDetector(
                key: const ValueKey('rotation-valor'),
                behavior: HitTestBehavior.opaque,
                onTap: () async {
                  final v = await showNumberInput(
                    context,
                    value: deg,
                    unit: '°',
                    decimals: 1,
                  );
                  if (v != null) controller.editRotation(layer.id, t, v);
                },
                child: Container(
                  width: 190,
                  height: 62,
                  alignment: Alignment.center,
                  decoration: BoxDecoration(
                    color: AmColors.chip,
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: AppText(
                    turns == 0
                        ? '${amNumber(deg, 0)}°'
                        : '${amNumber(deg % 360, 0)}°, ${turns}x',
                    style: const TextStyle(
                      fontSize: 25,
                      fontWeight: FontWeight.w600,
                      color: AmColors.accent,
                    ),
                  ),
                ),
              ),
              Transform.translate(
                offset: Offset(
                  math.cos(deg * math.pi / 180) * radius,
                  math.sin(deg * math.pi / 180) * radius,
                ),
                child: Container(
                  width: 40,
                  height: 40,
                  decoration: const BoxDecoration(
                    color: Colors.white,
                    shape: BoxShape.circle,
                  ),
                ),
              ),
            ],
          ),
        );
      },
    );

    // VOLTAS E QUARTOS sem circular o dedo: e o que faz "gira duas
    // vezes" caber em dois toques.
    Widget volta(String rotulo, double delta, {String? chave}) =>
        GestureDetector(
          key: chave == null ? null : ValueKey(chave),
          behavior: HitTestBehavior.opaque,
          onTap: () => controller.editRotation(layer.id, t, deg + delta),
          child: Container(
            margin: const EdgeInsets.symmetric(horizontal: 4),
            padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 7),
            decoration: BoxDecoration(
              color: AmColors.chip,
              borderRadius: BorderRadius.circular(999),
            ),
            child: AppText(
              rotulo,
              style: const TextStyle(fontSize: 12, color: AmColors.accent),
            ),
          ),
        );
    // FittedBox: em tela estreita a fileira encolhe em vez de estourar.
    final voltas = FittedBox(
      fit: BoxFit.scaleDown,
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          volta('−1 volta', -360, chave: 'rotation-turn-minus'),
          volta('−90°', -90),
          volta('+90°', 90),
          volta('+1 volta', 360, chave: 'rotation-turn-plus'),
        ],
      ),
    );

    if (!layer.is3D) {
      return Column(
        children: [
          Expanded(child: dial),
          const SizedBox(height: 6),
          voltas,
        ],
      );
    }

    // Camada 3D: alem do dial (eixo Z), reguas para girar em X e Y.
    final rx = layer.rotationX.valueAt(local);
    final ry = layer.rotationY.valueAt(local);
    return Column(
      children: [
        Expanded(child: dial),
        const SizedBox(height: 4),
        voltas,
        const SizedBox(height: 6),
        Row(
          children: [
            const SizedBox(
              width: 48,
              child: AppText(
                '3D X',
                textAlign: TextAlign.center,
                style: TextStyle(fontSize: 13, color: AmColors.muted),
              ),
            ),
            Expanded(
              child: AmTickRuler(
                value: rx,
                min: -1080,
                max: 1080,
                unitsPerPixel: 0.5,
                height: 52,
                onChanged: (v) => controller.editRotationX(layer.id, t, v),
              ),
            ),
            SizedBox(
              width: 70,
              child: AppText(
                '${amNumber(rx, 0)}°',
                textAlign: TextAlign.center,
                style: const TextStyle(fontSize: 15, color: AmColors.accent),
              ),
            ),
          ],
        ),
        const SizedBox(height: 4),
        Row(
          children: [
            const SizedBox(
              width: 48,
              child: AppText(
                '3D Y',
                textAlign: TextAlign.center,
                style: TextStyle(fontSize: 13, color: AmColors.muted),
              ),
            ),
            Expanded(
              child: AmTickRuler(
                value: ry,
                min: -1080,
                max: 1080,
                unitsPerPixel: 0.5,
                accentCenter: false,
                height: 52,
                onChanged: (v) => controller.editRotationY(layer.id, t, v),
              ),
            ),
            SizedBox(
              width: 70,
              child: AppText(
                '${amNumber(ry, 0)}°',
                textAlign: TextAlign.center,
                style: const TextStyle(fontSize: 15, color: AmColors.accent),
              ),
            ),
          ],
        ),
      ],
    );
  }
}

class _DialPainter extends CustomPainter {
  const _DialPainter({required this.radius});

  final double radius;

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width / 2, size.height / 2);
    canvas.drawCircle(
      center,
      radius,
      Paint()
        ..color = const Color(0xFF3A4660)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2,
    );
  }

  @override
  bool shouldRepaint(_DialPainter old) => old.radius != radius;
}

class _ScaleControl extends ConsumerWidget {
  const _ScaleControl({
    required this.layer,
    required this.playback,
    required this.linked,
    required this.onToggleLink,
  });

  final Layer layer;
  final PlaybackController playback;
  final bool linked;
  final VoidCallback onToggleLink;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final t = playback.time.value;
    final local = layer.localTime(t);
    final sx = layer.scaleX.valueAt(local);
    final sy = layer.scaleY.valueAt(local);
    final controller = ref.read(editorControllerProvider.notifier);

    void aplicar(bool largura, double v) => linked
        ? controller.editScaleUniform(layer.id, t, v / 100)
        : largura
        ? controller.editScaleX(layer.id, t, v / 100)
        : controller.editScaleY(layer.id, t, v / 100);

    return Column(
      children: [
        Row(
          children: [
            Expanded(
              child: ParameterRow(
                label: 'Largura',
                value: sx * 100,
                min: 1,
                max: 2000,
                unitsPerPixel: 0.6,
                unit: '%',
                accentCenter: true,
                rulerKey: const ValueKey('scale-width-ruler'),
                valueKey: const ValueKey('scale-width-valor'),
                keyframe: _kf(ref, layer, LayerProp.scale, t, null),
                expression: layer.scaleX.expression,
                onExpression: _expressao(
                  context,
                  ref,
                  layer,
                  LayerProp.scale,
                  'Escala',
                ),
                onAnimador: _animador(
                  context,
                  ref,
                  layer,
                  LayerProp.scale,
                  'a escala',
                  unidade: '%',
                ),
                onReset: () => controller.resetProp(layer.id, LayerProp.scale),
                onChanged: (v) => aplicar(true, v),
              ),
            ),
            Tooltip(
              message: linked ? 'Proporção travada' : 'Proporção livre',
              child: GestureDetector(
                onTap: onToggleLink,
                child: Container(
                  width: 32,
                  height: 32,
                  margin: const EdgeInsets.only(left: 4),
                  decoration: BoxDecoration(
                    color: linked ? const Color(0xFFE9EDF2) : AmColors.chip,
                    borderRadius: BorderRadius.circular(10),
                  ),
                  child: Icon(
                    CupertinoIcons.link,
                    size: 20,
                    color: linked ? const Color(0xFF12151A) : AmColors.muted,
                  ),
                ),
              ),
            ),
          ],
        ),
        Padding(
          padding: const EdgeInsets.only(right: 36),
          child: ParameterRow(
            label: 'Altura',
            value: sy * 100,
            min: 1,
            max: 2000,
            unitsPerPixel: 0.6,
            unit: '%',
            rulerKey: const ValueKey('scale-height-ruler'),
            valueKey: const ValueKey('scale-height-valor'),
            onChanged: (v) => aplicar(false, v),
          ),
        ),
        const Spacer(),
      ],
    );
  }
}

class _SkewControl extends ConsumerWidget {
  const _SkewControl({required this.layer, required this.playback});

  final Layer layer;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final t = playback.time.value;
    final local = layer.localTime(t);
    final kx = layer.skewX.valueAt(local);
    final ky = layer.skewY.valueAt(local);
    final controller = ref.read(editorControllerProvider.notifier);

    return Column(
      children: [
        ParameterRow(
          label: 'Inclinar X',
          value: kx,
          min: -80,
          max: 80,
          unitsPerPixel: 0.25,
          unit: '°',
          accentCenter: true,
          keyframe: _kf(ref, layer, LayerProp.skew, t, null),
          expression: layer.skewX.expression,
          onExpression: _expressao(
            context,
            ref,
            layer,
            LayerProp.skew,
            'Inclinar',
          ),
          onAnimador: _animador(
            context,
            ref,
            layer,
            LayerProp.skew,
            'a inclinação',
            unidade: '°',
          ),
          onReset: () => controller.resetProp(layer.id, LayerProp.skew),
          onChanged: (v) => controller.editSkewX(layer.id, t, v),
        ),
        ParameterRow(
          label: 'Inclinar Y',
          value: ky,
          min: -80,
          max: 80,
          unitsPerPixel: 0.25,
          unit: '°',
          onChanged: (v) => controller.editSkewY(layer.id, t, v),
        ),
        const Spacer(),
      ],
    );
  }
}

/// Opacidade da camada: uma regua de 0 a 100, com o keyframe cravado
/// pelo trilho como qualquer outra propriedade.
class _OpacityControl extends ConsumerWidget {
  const _OpacityControl({required this.layer, required this.playback});

  final Layer layer;
  final PlaybackController playback;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final t = playback.time.value;
    final local = layer.localTime(t);
    final op = layer.opacity.valueAt(local).clamp(0.0, 1.0);
    final controller = ref.read(editorControllerProvider.notifier);

    return Column(
      children: [
        ParameterRow(
          label: 'Opacidade',
          value: op * 100,
          min: 0,
          max: 100,
          unitsPerPixel: 0.35,
          decimals: 0,
          unit: '%',
          valueKey: const ValueKey('opacidade-valor'),
          keyframe: _kf(ref, layer, LayerProp.opacity, t, null),
          expression: layer.opacity.expression,
          onExpression: _expressao(
            context,
            ref,
            layer,
            LayerProp.opacity,
            'Opacidade',
          ),
          onAnimador: _animador(
            context,
            ref,
            layer,
            LayerProp.opacity,
            'a opacidade',
            unidade: '%',
          ),
          onReset: () => controller.resetProp(layer.id, LayerProp.opacity),
          onChanged: (v) => controller.editOpacity(
            layer.id,
            playback.time.value,
            (v / 100).clamp(0.0, 1.0),
          ),
        ),
        const SizedBox(height: 6),
        // A regua larga continua: arrasto grosso com o dedo inteiro.
        Expanded(
          child: AmTickRuler(
            value: op * 100,
            min: 0,
            max: 100,
            unitsPerPixel: 0.35,
            accentCenter: false,
            height: double.infinity,
            onChanged: (v) => controller.editOpacity(
              layer.id,
              playback.time.value,
              (v / 100).clamp(0.0, 1.0),
            ),
          ),
        ),
      ],
    );
  }
}
