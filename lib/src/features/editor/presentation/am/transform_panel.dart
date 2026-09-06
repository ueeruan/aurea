import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'panel_chrome.dart';
import 'property_keyframe_context.dart';

enum TransformTool { position, rotation, scale, skew, pivot, opacity }

LayerProp propOfTool(TransformTool tool) => switch (tool) {
  TransformTool.position => LayerProp.position,
  TransformTool.rotation => LayerProp.rotation,
  TransformTool.scale => LayerProp.scale,
  TransformTool.skew => LayerProp.skew,
  TransformTool.pivot => LayerProp.pivot,
  TransformTool.opacity => LayerProp.opacity,
};

/// Painel "Movimentacao e transformacao": trilho esquerdo (voltar,
/// keyframe, curva), controle central com navegacao de keyframes
/// e sub-ferramentas a direita (posicao/rotacao/escala/skew/pivo).
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

  @override
  void dispose() {
    _bodyScroll.dispose();
    super.dispose();
  }

  LayerProp get _prop => propOfTool(widget.tool);

  Widget _options(
    EditorController controller,
    String id,
    Layer layer,
  ) => PopupMenuButton<String>(
    tooltip: 'Opções de transformação',
    icon: const Icon(CupertinoIcons.ellipsis, color: AmColors.text),
    color: AmColors.panelHigh,
    itemBuilder: (_) => [
      CheckedPopupMenuItem(
        value: 'auto',
        checked: ref.read(autoKeyframeProvider),
        child: const Text('Auto-key'),
      ),
      const PopupMenuItem(value: 'previous', child: Text('Keyframe anterior')),
      const PopupMenuItem(value: 'next', child: Text('Próximo keyframe')),
      const PopupMenuItem(value: 'reset', child: Text('Resetar propriedade')),
    ],
    onSelected: (value) {
      if (value == 'auto') {
        final setting = ref.read(autoKeyframeProvider.notifier);
        setting.state = !setting.state;
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

  @override
  Widget build(BuildContext context) {
    ref.watch(autoKeyframeProvider);
    final project = ref.watch(editorControllerProvider);
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
        final local = layer.localTime(t);
        // Rotacao: keyframe e GLOBAL entre os eixos X/Y/Z.
        final times = keyframeTimesForProp(layer, _prop);
        // Mesmo epsilon de 8 ms das trilhas (scrub arredonda ao frame).
        final hasKfHere = times.any(
          (us) => (us - local.inMicroseconds).abs() < 8000,
        );
        final animated = times.isNotEmpty;

        return _buildBody(
          context,
          controller,
          id,
          layer,
          t,
          hasKfHere,
          animated,
        );
      },
    );
  }

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
              ParamTab(
                id: TransformTool.pivot.name,
                label: 'Pivo',
                icone: CupertinoIcons.smallcircle_circle,
                animated: layer.pivot.isAnimated,
              ),
              ParamTab(
                id: TransformTool.opacity.name,
                label: 'Opacid.',
                icone: CupertinoIcons.circle_lefthalf_fill,
                animated: layer.opacity.isAnimated,
              ),
            ],
            abaAtiva: widget.tool.name,
            onAba: (nome) => widget.onToolChanged(
              TransformTool.values.firstWhere((e) => e.name == nome),
            ),
            corpo: LayoutBuilder(
              builder: (context, constraints) => Scrollbar(
                controller: _bodyScroll,
                thumbVisibility:
                    constraints.maxHeight < (layer.is3D ? 260 : 160),
                child: SingleChildScrollView(
                  controller: _bodyScroll,
                  child: SizedBox(
                    height: math.max(
                      layer.is3D ? 260 : 160,
                      constraints.maxHeight,
                    ),
                    child: Padding(
                      padding: const EdgeInsets.fromLTRB(6, 6, 6, 2),
                      child: switch (widget.tool) {
                        TransformTool.position => _PositionControl(
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
  const _PositionControl({required this.layer, required this.playback});

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
              child: Text(
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
                  title: Text(
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
    final project = ref.watch(editorControllerProvider);
    final t = playback.time.value;
    final local = layer.localTime(t);
    final pos = layer.position.valueAt(local);
    final z = layer.positionZ.valueAt(local);
    final controller = ref.read(editorControllerProvider.notifier);
    final link = project.linkFor(layer.id, LayerProp.position);

    return Column(
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Expanded(
              child: AmValueChip(
                compact: true,
                text: amNumber(pos.dx),
                label: 'X',
                width: double.infinity,
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: AmValueChip(
                compact: true,
                text: amNumber(pos.dy),
                label: 'Y',
                width: double.infinity,
              ),
            ),
            if (layer.is3D) ...[
              const SizedBox(width: 10),
              Expanded(
                child: AmValueChip(
                  compact: true,
                  text: amNumber(z),
                  label: 'Z',
                  width: double.infinity,
                ),
              ),
            ],
          ],
        ),
        const SizedBox(height: 4),
        Expanded(
          child: GestureDetector(
            key: const ValueKey('position-drag-pad'),
            behavior: HitTestBehavior.opaque,
            dragStartBehavior: DragStartBehavior.down,
            onPanStart: (_) {
              _dragStart = layer.position.valueAt(
                layer.localTime(playback.time.value),
              );
              _accum = Offset.zero;
            },
            onPanUpdate: (d) => _onPadUpdate(d.delta),
            child: Container(
              decoration: BoxDecoration(
                color: AmColors.bg.withValues(alpha: 0.45),
                borderRadius: BorderRadius.circular(14),
              ),
              child: const Center(
                child: Text(
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
        const SizedBox(height: 6),
        Row(
          children: [
            const SizedBox(width: 4),
            const Text(
              '3D',
              style: TextStyle(fontSize: 12, color: AmColors.muted),
            ),
            Transform.scale(
              scale: 0.72,
              child: CupertinoSwitch(
                value: layer.is3D,
                activeTrackColor: AmColors.accent,
                onChanged: (_) => controller.toggle3D(layer.id),
              ),
            ),
            const Spacer(),
            // Pickwhip: seguir a posicao de outra camada.
            GestureDetector(
              onTap: link != null
                  ? () =>
                        controller.unlinkProperty(layer.id, LayerProp.position)
                  : () => _pickLinkSource(context, ref, t),
              child: Row(
                children: [
                  Icon(
                    link != null
                        ? CupertinoIcons.link_circle_fill
                        : CupertinoIcons.link,
                    size: 18,
                    color: link != null ? AmColors.accent : AmColors.muted,
                  ),
                  const SizedBox(width: 4),
                  Text(
                    link != null ? 'Vinculado' : 'Vincular',
                    style: TextStyle(
                      fontSize: 11,
                      color: link != null ? AmColors.accent : AmColors.muted,
                    ),
                  ),
                  const SizedBox(width: 4),
                ],
              ),
            ),
          ],
        ),
        if (layer.is3D)
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
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Expanded(
              child: AmValueChip(
                compact: true,
                text: amNumber(pivot.dx),
                label: 'Pivo X',
                width: double.infinity,
              ),
            ),
            const SizedBox(width: 18),
            Expanded(
              child: AmValueChip(
                compact: true,
                text: amNumber(pivot.dy),
                label: 'Pivo Y',
                width: double.infinity,
              ),
            ),
          ],
        ),
        const SizedBox(height: 10),
        Expanded(
          child: GestureDetector(
            behavior: HitTestBehavior.opaque,
            onPanUpdate: (d) =>
                controller.editPivot(layer.id, t, pivot + d.delta * 2),
            onDoubleTap: () => controller.editPivot(layer.id, t, Offset.zero),
            key: const ValueKey('pivot-drag-pad'),
            child: Container(
              decoration: BoxDecoration(
                color: AmColors.bg.withValues(alpha: 0.45),
                borderRadius: BorderRadius.circular(14),
              ),
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
                    Text(
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

class _RotationControl extends ConsumerWidget {
  const _RotationControl({required this.layer, required this.playback});

  final Layer layer;
  final PlaybackController playback;

  void _setFromPoint(WidgetRef ref, Offset localPos, Size size, Duration t) {
    final center = Offset(size.width / 2, size.height / 2);
    final v = localPos - center;
    final deg = math.atan2(v.dy, v.dx) * 180 / math.pi;
    ref.read(editorControllerProvider.notifier).editRotation(layer.id, t, deg);
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
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
        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onPanUpdate: (d) => _setFromPoint(ref, d.localPosition, size, t),
          onTapDown: (d) => _setFromPoint(ref, d.localPosition, size, t),
          child: Stack(
            alignment: Alignment.center,
            children: [
              CustomPaint(
                size: size,
                painter: _DialPainter(radius: radius),
              ),
              Container(
                width: 190,
                height: 62,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: AmColors.chip,
                  borderRadius: BorderRadius.circular(12),
                ),
                child: Text(
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

    if (!layer.is3D) return dial;

    // Camada 3D: alem do dial (eixo Z), reguas para girar em X e Y.
    final rx = layer.rotationX.valueAt(local);
    final ry = layer.rotationY.valueAt(local);
    return Column(
      children: [
        Expanded(child: dial),
        const SizedBox(height: 6),
        Row(
          children: [
            const SizedBox(
              width: 44,
              child: Text(
                '3D X',
                textAlign: TextAlign.center,
                style: TextStyle(fontSize: 12, color: AmColors.muted),
              ),
            ),
            Expanded(
              child: AmTickRuler(
                value: rx,
                min: -1080,
                max: 1080,
                unitsPerPixel: 0.8,
                height: 40,
                onChanged: (v) => controller.editRotationX(layer.id, t, v),
              ),
            ),
            SizedBox(
              width: 62,
              child: Text(
                '${amNumber(rx, 0)}°',
                textAlign: TextAlign.center,
                style: const TextStyle(fontSize: 13, color: AmColors.accent),
              ),
            ),
          ],
        ),
        const SizedBox(height: 4),
        Row(
          children: [
            const SizedBox(
              width: 44,
              child: Text(
                '3D Y',
                textAlign: TextAlign.center,
                style: TextStyle(fontSize: 12, color: AmColors.muted),
              ),
            ),
            Expanded(
              child: AmTickRuler(
                value: ry,
                min: -1080,
                max: 1080,
                unitsPerPixel: 0.8,
                accentCenter: false,
                height: 40,
                onChanged: (v) => controller.editRotationY(layer.id, t, v),
              ),
            ),
            SizedBox(
              width: 62,
              child: Text(
                '${amNumber(ry, 0)}°',
                textAlign: TextAlign.center,
                style: const TextStyle(fontSize: 13, color: AmColors.accent),
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

    return Column(
      children: [
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Expanded(
              child: AmValueChip(
                compact: true,
                text: amNumber(sx * 100),
                label: 'Largura',
                width: double.infinity,
              ),
            ),
            GestureDetector(
              onTap: onToggleLink,
              child: Container(
                width: 32,
                height: 32,
                margin: const EdgeInsets.symmetric(horizontal: 5),
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
            Expanded(
              child: AmValueChip(
                compact: true,
                text: amNumber(sy * 100),
                label: 'Altura',
                width: double.infinity,
              ),
            ),
          ],
        ),
        const SizedBox(height: 10),
        ...[
          Expanded(
            child: AmTickRuler(
              value: sx * 100,
              min: 1,
              max: 2000,
              unitsPerPixel: 0.6,
              height: double.infinity,
              key: const ValueKey('scale-width-ruler'),
              onChanged: (v) => linked
                  ? controller.editScaleUniform(layer.id, t, v / 100)
                  : controller.editScaleX(layer.id, t, v / 100),
            ),
          ),
          const SizedBox(height: 6),
          Expanded(
            child: AmTickRuler(
              value: sy * 100,
              min: 1,
              max: 2000,
              unitsPerPixel: 0.6,
              accentCenter: false,
              height: double.infinity,
              key: const ValueKey('scale-height-ruler'),
              onChanged: (v) => linked
                  ? controller.editScaleUniform(layer.id, t, v / 100)
                  : controller.editScaleY(layer.id, t, v / 100),
            ),
          ),
        ],
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
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Expanded(
              child: AmValueChip(
                compact: true,
                text: '${amNumber(kx, 2)}°',
                label: 'X Skew',
                width: double.infinity,
              ),
            ),
            const SizedBox(width: 18),
            Expanded(
              child: AmValueChip(
                compact: true,
                text: '${amNumber(ky, 2)}°',
                label: 'Y Skew',
                width: double.infinity,
              ),
            ),
          ],
        ),
        const SizedBox(height: 10),
        Expanded(
          child: AmTickRuler(
            value: kx,
            min: -80,
            max: 80,
            unitsPerPixel: 0.25,
            height: double.infinity,
            onChanged: (v) => controller.editSkewX(layer.id, t, v),
          ),
        ),
        const SizedBox(height: 6),
        Expanded(
          child: AmTickRuler(
            value: ky,
            min: -80,
            max: 80,
            unitsPerPixel: 0.25,
            accentCenter: false,
            height: double.infinity,
            onChanged: (v) => controller.editSkewY(layer.id, t, v),
          ),
        ),
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
        AmValueChip(
          compact: true,
          text: '${amNumber(op * 100, 0)}%',
          label: 'Opacidade',
          width: 132,
        ),
        const SizedBox(height: 10),
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
