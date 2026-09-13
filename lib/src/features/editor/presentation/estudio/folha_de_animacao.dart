import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/keyframe.dart';
import '../../domain/layer.dart';
import 'estado_do_estudio.dart';
import 'ficha_do_selecionado.dart';
import 'folhas_do_estudio.dart';
import 'scene3d_theme.dart';

Future<void> abrirFolhaDeAnimacao(
  BuildContext context,
  WidgetRef ref, {
  required String layerId,
  required PlaybackController playback,
}) => mostrarFolhaScene3D<void>(
  context,
  title: 'Animação',
  body: FolhaDeAnimacao(layerId: layerId, playback: playback),
);

class FolhaDeAnimacao extends ConsumerStatefulWidget {
  const FolhaDeAnimacao({
    super.key,
    required this.layerId,
    required this.playback,
  });
  final String layerId;
  final PlaybackController playback;
  @override
  ConsumerState<FolhaDeAnimacao> createState() => _FolhaDeAnimacaoState();
}

class _FolhaDeAnimacaoState extends ConsumerState<FolhaDeAnimacao> {
  PropDoNo _nodeProp = PropDoNo.x;
  PropDaCamera _cameraProp = PropDaCamera.posX;
  @override
  Widget build(BuildContext context) {
    final layer = ref.watch(projetoVisivelProvider).layerById(widget.layerId);
    if (layer is! Scene3DLayer) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);
    final node = layer.scene.nodeById(ref.watch(noSelecionadoProvider) ?? '');
    final selectedCamera = ref.watch(cameraSelecionadaProvider);
    return ValueListenableBuilder<Duration>(
      valueListenable: widget.playback.time,
      builder: (context, time, _) {
        final local = layer.localTime(time);
        final camera =
            layer.allCameras
                .where((cam) => cam.id == selectedCamera)
                .firstOrNull ??
            cameraNoAr(layer, local);
        final track = node != null
            ? c.sceneNodeTrack(node, _nodeProp)
            : c.sceneCameraTrack(camera, _cameraProp);
        final marked = track.hasKeyframeAt(local);
        void toggle() {
          if (node != null) {
            c.toggleSceneNodeKeyframe(layer.id, node.id, _nodeProp, time);
          } else {
            c.toggleSceneCameraKeyframe(layer.id, camera.id, _cameraProp, time);
          }
        }

        return Padding(
          padding: const EdgeInsets.fromLTRB(16, 0, 16, 20),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              AppText(
                node?.name ?? camera.name,
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                  color: Scene3DTheme.text,
                  fontSize: 16,
                  fontWeight: FontWeight.w600,
                ),
              ),
              const SizedBox(height: 12),
              if (node != null)
                DropdownButton<PropDoNo>(
                  value: _nodeProp,
                  isExpanded: true,
                  items: [
                    for (final p in PropDoNo.values)
                      DropdownMenuItem(value: p, child: AppText(propDoNoLabel(p))),
                  ],
                  onChanged: (p) {
                    if (p != null) setState(() => _nodeProp = p);
                  },
                )
              else
                DropdownButton<PropDaCamera>(
                  value: _cameraProp,
                  isExpanded: true,
                  items: [
                    for (final p in PropDaCamera.values)
                      DropdownMenuItem(
                        value: p,
                        child: AppText(propDaCameraLabel(p)),
                      ),
                  ],
                  onChanged: (p) {
                    if (p != null) setState(() => _cameraProp = p);
                  },
                ),
              const SizedBox(height: 12),
              LayoutBuilder(
                builder: (context, box) => GestureDetector(
                  key: const ValueKey('scene-animation-graph'),
                  onTapDown: (d) => widget.playback.seek(
                    layer.startTime +
                        Duration(
                          microseconds:
                              (d.localPosition.dx /
                                      box.maxWidth *
                                      layer.duration.inMicroseconds)
                                  .round()
                                  .clamp(0, layer.duration.inMicroseconds),
                        ),
                  ),
                  child: CustomPaint(
                    size: Size(box.maxWidth, 160),
                    painter: _TrackPainter(track, layer.duration, local),
                  ),
                ),
              ),
              const SizedBox(height: 12),
              AppText(
                '${track.keyframes.length} keyframes · ${track.valueAt(local).toStringAsFixed(2)}',
                style: const TextStyle(color: Scene3DTheme.textMuted),
              ),
              const SizedBox(height: 12),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: [
                  OutlinedButton.icon(
                    key: const ValueKey('scene-animation-add'),
                    onPressed: marked ? null : toggle,
                    icon: const Icon(Icons.add),
                    label: const AppText('Adicionar'),
                  ),
                  OutlinedButton.icon(
                    key: const ValueKey('scene-animation-curve'),
                    onPressed: track.keyframes.length < 2
                        ? null
                        : () {
                            if (node != null) {
                              abrirCurvaDoNo(
                                ref,
                                layer.id,
                                node,
                                _nodeProp,
                                local,
                              );
                            } else {
                              abrirCurvaDaCamera(
                                ref,
                                layer.id,
                                camera,
                                _cameraProp,
                                local,
                              );
                            }
                          },
                    icon: const Icon(Icons.auto_graph),
                    label: const AppText('Ajustar curva'),
                  ),
                  OutlinedButton.icon(
                    key: const ValueKey('scene-animation-delete'),
                    onPressed: marked ? toggle : null,
                    icon: const Icon(Icons.delete_outline),
                    label: const AppText('Excluir'),
                  ),
                ],
              ),
            ],
          ),
        );
      },
    );
  }
}

class _TrackPainter extends CustomPainter {
  _TrackPainter(this.track, this.duration, this.local);
  final AnimatedDouble track;
  final Duration duration, local;
  @override
  void paint(Canvas canvas, Size size) {
    if (duration.inMicroseconds <= 0) return;
    final values = [
      for (var i = 0; i <= 120; i++)
        track.valueAt(
          Duration(microseconds: (duration.inMicroseconds * i / 120).round()),
        ),
    ];
    final lo = values.reduce(math.min), hi = values.reduce(math.max);
    final span = math.max(1.0, hi - lo);
    Offset pos(double x, double value) => Offset(
      x,
      size.height -
          12 -
          (value - lo + span * .1) / (span * 1.2) * (size.height - 24),
    );
    canvas.drawRect(
      Offset.zero & size,
      Paint()..color = const Color(0xFF11161D),
    );
    final path = Path();
    for (var i = 0; i < values.length; i++) {
      final p = pos(i / 120 * size.width, values[i]);
      if (i == 0) {
        path.moveTo(p.dx, p.dy);
      } else {
        path.lineTo(p.dx, p.dy);
      }
    }
    canvas.drawPath(
      path,
      Paint()
        ..color = Scene3DTheme.accent
        ..strokeWidth = 2
        ..style = PaintingStyle.stroke,
    );
    for (final k in track.keyframes) {
      canvas.drawCircle(
        pos(
          k.time.inMicroseconds / duration.inMicroseconds * size.width,
          k.value,
        ),
        4,
        Paint()..color = Colors.white,
      );
    }
    final x =
        (local.inMicroseconds / duration.inMicroseconds).clamp(0, 1) *
        size.width;
    canvas.drawLine(
      Offset(x, 0),
      Offset(x, size.height),
      Paint()
        ..color = Colors.white54
        ..strokeWidth = 1,
    );
  }

  @override
  bool shouldRepaint(covariant _TrackPainter old) =>
      old.track != track || old.local != local || old.duration != duration;
}
