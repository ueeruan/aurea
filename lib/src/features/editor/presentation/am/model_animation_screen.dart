import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import '../../domain/camera3d.dart';
import '../../domain/model_asset3d.dart';
import '../widgets/scene3d_painter.dart';
import 'am_widgets.dart';

/// A real scene preview and seekable clock, not a list of animation names.
/// FK edits are additive to the imported clip and stored as pose keyframes.
class ModelAnimationScreen extends ConsumerStatefulWidget {
  const ModelAnimationScreen({
    super.key,
    required this.layerId,
    required this.nodeId,
  });
  final String layerId, nodeId;
  @override
  ConsumerState<ModelAnimationScreen> createState() =>
      _ModelAnimationScreenState();
}

class _ModelAnimationScreenState extends ConsumerState<ModelAnimationScreen>
    with SingleTickerProviderStateMixin {
  late final PlaybackController playback;
  int selected = 0;
  bool showRig = true;
  @override
  void initState() {
    super.initState();
    playback = PlaybackController(
      vsync: this,
      durationOf: () {
        final l = ref.read(editorControllerProvider).layerById(widget.layerId);
        return l?.duration ?? const Duration(seconds: 5);
      },
    );
    playback.compositionFps = ref.read(editorControllerProvider).fps;
    playback.loop.value = true;
  }

  @override
  void dispose() {
    playback.dispose();
    super.dispose();
  }

  void change(ModelMotion3D Function(ModelMotion3D) update) {
    ref
        .read(editorControllerProvider.notifier)
        .updateSceneNode(
          widget.layerId,
          widget.nodeId,
          (n) => n.copyWith(modelMotion: update(n.modelMotion)),
        );
  }

  void poseChange(ModelPose3D pose) {
    playback.pause();
    final seconds = playback.time.value.inMicroseconds / 1e6;
    change((m) {
      // A first edit later in the timeline must not change the bind pose at 0.
      var next = m;
      if (next.keys.isEmpty && seconds > 1e-6) next = next.withPose(0, {});
      return next.withPose(seconds, {...next.poseAt(seconds), selected: pose});
    });
  }

  @override
  Widget build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);
    final l = project.layerById(widget.layerId);
    final n = l is Scene3DLayer ? l.scene.nodeById(widget.nodeId) : null;
    if (l is! Scene3DLayer || n?.modelAsset == null) {
      return Scaffold(
        appBar: AppBar(title: const Text('Animacao 3D')),
        body: const Center(child: Text('Modelo indisponivel.')),
      );
    }
    final node = n!, asset = node.modelAsset!, motion = node.modelMotion;
    selected = selected.clamp(0, asset.nodes.length - 1);
    return Scaffold(
      appBar: AppBar(
        title: Text('Animar · ${node.name}'),
        actions: [
          IconButton(
            tooltip: 'Mostrar rig',
            onPressed: () => setState(() => showRig = !showRig),
            icon: Icon(
              showRig ? Icons.account_tree : Icons.account_tree_outlined,
            ),
          ),
        ],
      ),
      body: SafeArea(
        child: ValueListenableBuilder<Duration>(
          valueListenable: playback.time,
          builder: (context, time, _) {
            final seconds = time.inMicroseconds / 1e6;
            final pose =
                motion.poseAt(seconds)[selected] ?? const ModelPose3D();
            final euler = _euler(pose.rotation);
            final maxTime = math.max(.001, l.duration.inMicroseconds / 1e6);
            return Column(
              children: [
                SizedBox(
                  height: MediaQuery.sizeOf(context).height * .29,
                  width: double.infinity,
                  child: CustomPaint(
                    painter: Scene3DPainter(
                      scene: l.scene,
                      camera: l.camera,
                      resolvedCamera: l.cameraAt(time),
                      view: SceneView.camera,
                      time: time,
                      selectedNodeId: node.id,
                      showHelpers: false,
                      showModelRig: showRig,
                    ),
                  ),
                ),
                Row(
                  children: [
                    ValueListenableBuilder<bool>(
                      valueListenable: playback.playing,
                      builder: (_, playing, _) => IconButton(
                        tooltip: playing ? 'Pausar' : 'Reproduzir',
                        onPressed: playback.toggle,
                        icon: Icon(playing ? Icons.pause : Icons.play_arrow),
                      ),
                    ),
                    Expanded(
                      child: AmTickRuler(
                        key: const ValueKey('model-time'),
                        height: 48,
                        unitsPerPixel: maxTime / 300,
                        min: 0,
                        max: maxTime,
                        value: seconds.clamp(0.0, maxTime),
                        onChanged: (v) {
                          playback.pause();
                          playback.seek(
                            Duration(microseconds: (v * 1e6).round()),
                          );
                        },
                      ),
                    ),
                    Padding(
                      padding: const EdgeInsets.only(right: 12),
                      child: Text('${seconds.toStringAsFixed(2)} s'),
                    ),
                  ],
                ),
                Expanded(
                  child: ListView(
                    padding: const EdgeInsets.fromLTRB(16, 0, 16, 18),
                    children: [
                      if (node.locked)
                        const Text(
                          'Objeto bloqueado. Desbloqueie na cena para editar.',
                        ),
                      DropdownButtonFormField<int>(
                        initialValue: motion.clip >= asset.clips.length
                            ? -1
                            : motion.clip,
                        decoration: const InputDecoration(
                          labelText: 'Clipe importado',
                        ),
                        items: [
                          const DropdownMenuItem(
                            value: -1,
                            child: Text('Pose de repouso'),
                          ),
                          for (var i = 0; i < asset.clips.length; i++)
                            DropdownMenuItem(
                              value: i,
                              child: Text(asset.clipNames[i]),
                            ),
                        ],
                        onChanged: node.locked
                            ? null
                            : (v) => change((m) => m.copyWith(clip: v)),
                      ),
                      SwitchListTile.adaptive(
                        contentPadding: EdgeInsets.zero,
                        title: const Text('Repetir clipe'),
                        value: motion.loop,
                        onChanged: node.locked
                            ? null
                            : (v) => change((m) => m.copyWith(loop: v)),
                      ),
                      _slider(
                        'Velocidade do clipe',
                        motion.speed,
                        .1,
                        3,
                        (v) => change((m) => m.copyWith(speed: v)),
                        disabled: node.locked,
                      ),
                      const SizedBox(height: 8),
                      DropdownButtonFormField<int>(
                        initialValue: selected,
                        isExpanded: true,
                        decoration: const InputDecoration(
                          labelText: 'Osso / parte do modelo',
                        ),
                        items: [
                          for (var i = 0; i < asset.nodes.length; i++)
                            DropdownMenuItem(
                              value: i,
                              child: Text(
                                '${asset.joints.contains(i) ? 'Osso · ' : ''}${asset.nodes[i]['name']}',
                                overflow: TextOverflow.ellipsis,
                              ),
                            ),
                        ],
                        onChanged: (v) => setState(() => selected = v ?? 0),
                      ),
                      const Padding(
                        padding: EdgeInsets.symmetric(vertical: 8),
                        child: Text(
                          'Escolha uma parte, avance o tempo e ajuste a pose. Cada ajuste grava um keyframe. O rig importado deforma a malha pelos pesos dos ossos.',
                          style: TextStyle(fontSize: 12),
                        ),
                      ),
                      for (var axis = 0; axis < 3; axis++)
                        _slider(
                          'Rotacao ${'XYZ'[axis]}',
                          euler[axis],
                          -180,
                          180,
                          (v) {
                            final angles = [...euler]..[axis] = v;
                            poseChange(
                              ModelPose3D(
                                translation: pose.translation,
                                rotation: _quaternion(angles),
                                scale: pose.scale,
                              ),
                            );
                          },
                          disabled: node.locked,
                        ),
                      for (var axis = 0; axis < 3; axis++)
                        _slider(
                          'Deslocamento ${'XYZ'[axis]}',
                          pose.translation[axis],
                          -asset.poseTranslationRange,
                          asset.poseTranslationRange,
                          (v) {
                            final translation = [...pose.translation]
                              ..[axis] = v;
                            poseChange(
                              ModelPose3D(
                                translation: translation,
                                rotation: pose.rotation,
                                scale: pose.scale,
                              ),
                            );
                          },
                          disabled: node.locked,
                        ),
                      Wrap(
                        spacing: 8,
                        children: [
                          OutlinedButton.icon(
                            onPressed: node.locked
                                ? null
                                : () => change(
                                    (m) =>
                                        m.withPose(seconds, m.poseAt(seconds)),
                                  ),
                            icon: const Icon(Icons.diamond_outlined),
                            label: const Text('Gravar pose'),
                          ),
                          TextButton(
                            onPressed: node.locked
                                ? null
                                : () => change(
                                    (m) => m.copyWith(
                                      keys: m.keys
                                          .where(
                                            (k) =>
                                                (k.seconds - seconds).abs() >
                                                .000001,
                                          )
                                          .toList(),
                                    ),
                                  ),
                            child: const Text('Apagar keyframe'),
                          ),
                          TextButton(
                            onPressed: node.locked
                                ? null
                                : () => poseChange(const ModelPose3D()),
                            child: const Text('Zerar parte'),
                          ),
                        ],
                      ),
                      Wrap(
                        spacing: 6,
                        children: [
                          for (final key in motion.keys)
                            ActionChip(
                              label: Text(
                                '◆ ${key.seconds.toStringAsFixed(2)}s',
                              ),
                              onPressed: () {
                                playback.pause();
                                playback.seek(
                                  Duration(
                                    microseconds: (key.seconds * 1e6).round(),
                                  ),
                                );
                              },
                            ),
                        ],
                      ),
                      if (asset.warnings.isNotEmpty)
                        Text(
                          asset.warnings.join('\n'),
                          style: const TextStyle(fontSize: 11),
                        ),
                    ],
                  ),
                ),
              ],
            );
          },
        ),
      ),
    );
  }

  Widget _slider(
    String label,
    double value,
    double min,
    double max,
    ValueChanged<double> update, {
    bool disabled = false,
  }) => Row(
    children: [
      SizedBox(
        width: 128,
        child: Text(
          '$label\n${value.toStringAsFixed(2)}',
          style: const TextStyle(fontSize: 12),
        ),
      ),
      Expanded(
        child: IgnorePointer(
          ignoring: disabled,
          child: AmTickRuler(
            key: ValueKey(label),
            height: 48,
            unitsPerPixel: (max - min) / 500,
            value: value.clamp(min, max),
            min: min,
            max: max,
            onChanged: (v) {
              if (!disabled) update(v);
            },
          ),
        ),
      ),
    ],
  );
}

List<double> _quaternion(List<double> angles) {
  final x = angles[0] * math.pi / 360,
      y = angles[1] * math.pi / 360,
      z = angles[2] * math.pi / 360;
  final cx = math.cos(x),
      sx = math.sin(x),
      cy = math.cos(y),
      sy = math.sin(y),
      cz = math.cos(z),
      sz = math.sin(z);
  return [
    sx * cy * cz - cx * sy * sz,
    cx * sy * cz + sx * cy * sz,
    cx * cy * sz - sx * sy * cz,
    cx * cy * cz + sx * sy * sz,
  ];
}

List<double> _euler(List<double> q) {
  final x = q[0], y = q[1], z = q[2], w = q[3];
  return [
    math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * 180 / math.pi,
    math.asin((2 * (w * y - z * x)).clamp(-1.0, 1.0)) * 180 / math.pi,
    math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) * 180 / math.pi,
  ];
}
