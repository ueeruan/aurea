import 'package:flutter/material.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/freehand_session.dart';
import '../../application/playback_controller.dart';
import '../../domain/keyframe.dart';
import '../../domain/mask.dart';
import '../../domain/shape.dart';
import '../../domain/shape_library.dart';
export '../../application/freehand_session.dart' show freehandRequestProvider;

/// DESENHO LIVRE: cobre a composicao enquanto o pedido esta ligado,
/// acompanha o dedo com uma linha, e ao soltar simplifica o rabisco em
/// vertices suaves e cria a camada centrada no desenho.
class FreehandOverlay extends ConsumerStatefulWidget {
  const FreehandOverlay({super.key, required this.playback});

  final PlaybackController playback;

  @override
  ConsumerState<FreehandOverlay> createState() => _FreehandOverlayState();
}

class _FreehandOverlayState extends ConsumerState<FreehandOverlay> {
  final List<Offset> _pontos = [];
  String? _projectId;
  Duration _startTime = Duration.zero;

  void _cancelar() {
    _pontos.clear();
    _projectId = null;
    if (mounted) setState(() {});
    ref.read(freehandRequestProvider.notifier).state = false;
  }

  void _fim() {
    // Um gesto interrompido pela troca de projeto não pode gravar no próximo.
    if (!ref.read(freehandRequestProvider) ||
        _projectId != ref.read(editorControllerProvider).id) {
      _pontos.clear();
      _projectId = null;
      return;
    }
    final pts = List<Offset>.of(_pontos);
    _pontos.clear();
    _projectId = null;
    ref.read(freehandRequestProvider.notifier).state = false;
    if (pts.length < 2) {
      setState(() {});
      return;
    }
    // Centro do desenho vira a posicao da camada; os vertices ficam
    // relativos a ele (e assim que toda forma vive na Aurea).
    var minX = double.infinity, minY = double.infinity;
    var maxX = -double.infinity, maxY = -double.infinity;
    for (final p in pts) {
      if (p.dx < minX) minX = p.dx;
      if (p.dx > maxX) maxX = p.dx;
      if (p.dy < minY) minY = p.dy;
      if (p.dy > maxY) maxY = p.dy;
    }
    final centro = Offset((minX + maxX) / 2, (minY + maxY) / 2);
    final caminho = freehandToPath([for (final p in pts) p - centro]);
    if (caminho.vertices.length < 2) {
      setState(() {});
      return;
    }
    final controller = ref.read(editorControllerProvider.notifier);
    final antes = {
      for (final l in ref.read(editorControllerProvider).layers) l.id,
    };
    controller.addShapeLayer(
      _startTime,
      contents: [
        ShapeBezier(path: AnimatedPath(caminho)),
        ShapeStroke(color: const Color(0xFFFFFFFF), width: AnimatedDouble(12)),
      ],
      name: 'Desenho livre',
    );
    for (final l in ref.read(editorControllerProvider).layers) {
      if (!antes.contains(l.id)) {
        controller.editPosition(l.id, _startTime, centro);
        break;
      }
    }
    setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    ref.listen(freehandRequestProvider, (_, active) {
      if (!active) {
        _pontos.clear();
        _projectId = null;
      }
    });
    final ligado = ref.watch(freehandRequestProvider);
    if (!ligado) return const SizedBox.shrink();
    return Listener(
      onPointerCancel: (_) => _cancelar(),
      child: GestureDetector(
        key: const ValueKey('freehand-canvas'),
        behavior: HitTestBehavior.opaque,
        dragStartBehavior: DragStartBehavior.down,
        onPanStart: (d) {
          widget.playback.pause();
          _projectId = ref.read(editorControllerProvider).id;
          _startTime = widget.playback.time.value;
          setState(
            () => _pontos
              ..clear()
              ..add(d.localPosition),
          );
        },
        onPanUpdate: (d) => setState(() => _pontos.add(d.localPosition)),
        onPanEnd: (_) => _fim(),
        onPanCancel: _cancelar,
        child: CustomPaint(
          painter: _RabiscoPainter(_pontos),
          child: const SizedBox.expand(),
        ),
      ),
    );
  }
}

class _RabiscoPainter extends CustomPainter {
  const _RabiscoPainter(this.pontos);

  final List<Offset> pontos;

  @override
  void paint(Canvas canvas, Size size) {
    if (pontos.length < 2) return;
    final path = Path()..moveTo(pontos.first.dx, pontos.first.dy);
    for (final p in pontos.skip(1)) {
      path.lineTo(p.dx, p.dy);
    }
    canvas.drawPath(
      path,
      Paint()
        ..color = Colors.white
        ..style = PaintingStyle.stroke
        ..strokeWidth = 12
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round,
    );
  }

  @override
  bool shouldRepaint(_RabiscoPainter old) => true;
}
