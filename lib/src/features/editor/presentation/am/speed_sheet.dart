import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/utils/time_format.dart';
import '../../application/editor_controller.dart';
import '../../application/proxy_service.dart';
import '../../domain/cut.dart';
import '../../domain/cut_ops.dart';
import '../../domain/layer.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// Velocidade constante, rampas compiladas em Time Remap e controles
/// avancados continuam na mesma folha aberta pelo icone de relogio.
Future<void> showSpeedSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) async {
  await showParamSheet(
    context,
    title: 'Velocidade',
    heightFactor: 0.76,
    builder: (sheetContext) => StatefulBuilder(
      builder: (sheetContext, setSheetState) {
        final project = ref.read(editorControllerProvider);
        final controller = ref.read(editorControllerProvider.notifier);
        final layer = project.layerById(layerId);
        if (layer == null) return const SizedBox.shrink();

        final speed = controller.clipSpeedOf(layerId);
        final video = layer is VideoLayer ? layer : null;
        final remap = controller.clipTimeRemapTrack(layerId);
        final audio = switch (layer) {
          VideoLayer v => v.audio,
          AudioLayer a => a.audio,
          _ => const AudioSpec(),
        };
        final hasSound =
            layer is AudioLayer ||
            (layer is VideoLayer && layer.volume > 0.001);

        void setSpeed(double value) {
          controller.setClipSpeed(layerId, value);
          setSheetState(() {});
        }

        Future<void> setReverse(bool value) async {
          if (!value) {
            controller.setClipReverse(layerId, false);
            setSheetState(() {});
            return;
          }

          if (video == null) return;
          if (ProxyService.instance.proxyOf(video.sourcePath) == null) {
            final generate = await showCupertinoDialog<bool>(
              context: sheetContext,
              builder: (dialogContext) => CupertinoAlertDialog(
                title: const Text('Reverso precisa de proxy'),
                content: const Text(
                  'O reverso busca os quadros de tras para frente. Um '
                  'proxy com GOP curto torna essa leitura deterministica.',
                ),
                actions: [
                  CupertinoDialogAction(
                    onPressed: () => Navigator.pop(dialogContext, false),
                    child: const Text('Cancelar'),
                  ),
                  CupertinoDialogAction(
                    isDefaultAction: true,
                    onPressed: () => Navigator.pop(dialogContext, true),
                    child: const Text('Gerar proxy'),
                  ),
                ],
              ),
            );
            if (generate != true || !sheetContext.mounted) return;
          }

          final proxy = await ProxyService.instance.ensureReverseProxy(
            video.sourcePath,
          );
          if (!sheetContext.mounted) return;
          if (proxy == null) {
            await showCupertinoDialog<void>(
              context: sheetContext,
              builder: (dialogContext) => CupertinoAlertDialog(
                title: const Text('Proxy nao foi criado'),
                content: const Text(
                  'O reverso continua desligado. Verifique o arquivo e '
                  'tente novamente.',
                ),
                actions: [
                  CupertinoDialogAction(
                    isDefaultAction: true,
                    onPressed: () => Navigator.pop(dialogContext),
                    child: const Text('OK'),
                  ),
                ],
              ),
            );
            return;
          }

          // `allowWithoutProxy` apenas pula a heuristica de duracao do
          // controller. A verificacao forte acima confirmou o arquivo.
          controller.setClipReverse(layerId, true, allowWithoutProxy: true);
          setSheetState(() {});
        }

        return SafeArea(
          child: SingleChildScrollView(
            padding: const EdgeInsets.fromLTRB(18, 14, 18, 24),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  '${speed.toStringAsFixed(2)}x · ${formatTime(layer.duration)}',
                  style: const TextStyle(fontSize: 12, color: AmColors.accent),
                ),
                const SizedBox(height: 12),
                Wrap(
                  spacing: 6,
                  runSpacing: 6,
                  children: [
                    for (final preset in const [0.5, 1.0, 2.0])
                      _SpeedChip(
                        label: preset == 1 ? '1x' : '${preset}x',
                        selected:
                            remap == null && (speed - preset).abs() < 0.01,
                        onTap: () => setSpeed(preset),
                      ),
                    if (video != null)
                      for (final preset in SpeedRampPreset.values)
                        _SpeedChip(
                          label: preset.label,
                          selected: false,
                          onTap: () {
                            controller.applySpeedRamp(layerId, preset);
                            setSheetState(() {});
                          },
                        ),
                  ],
                ),
                const SizedBox(height: 14),
                Row(
                  children: [
                    const SizedBox(
                      width: 62,
                      child: Text(
                        'Ajuste',
                        style: TextStyle(fontSize: 12, color: AmColors.muted),
                      ),
                    ),
                    Expanded(
                      child: AmTickRuler(
                        value: speed.clamp(0.1, 10.0),
                        min: 0.1,
                        max: 10,
                        unitsPerPixel: 0.025,
                        height: 42,
                        onChanged: setSpeed,
                      ),
                    ),
                    SizedBox(
                      width: 58,
                      child: Text(
                        '${speed.toStringAsFixed(2)}x',
                        textAlign: TextAlign.right,
                        style: const TextStyle(
                          fontSize: 12,
                          color: AmColors.text,
                        ),
                      ),
                    ),
                  ],
                ),
                if (hasSound) ...[
                  const SizedBox(height: 8),
                  _ToggleRow(
                    label: 'Manter tom do audio',
                    value: audio.preservePitch,
                    onChanged: (value) {
                      controller.setClipPreservePitch(layerId, value);
                      setSheetState(() {});
                    },
                  ),
                ],
                if (video != null) ...[
                  _ToggleRow(
                    label: 'Rampa / Time Remap',
                    value: remap != null,
                    onChanged: (value) {
                      controller.setClipTimeRemapEnabled(layerId, value);
                      setSheetState(() {});
                    },
                  ),
                  _ToggleRow(
                    label: 'Reverso',
                    value: video.reverse,
                    onChanged: setReverse,
                  ),
                  _ToggleRow(
                    label: 'Blur proporcional a velocidade',
                    value: video.speedBlur,
                    onChanged: (value) {
                      controller.setClipSpeedBlur(layerId, value);
                      setSheetState(() {});
                    },
                  ),
                ],
                if (video != null && remap != null) ...[
                  const SizedBox(height: 14),
                  const Text(
                    'Avancado · keyframes de Time Remap',
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w700,
                      color: AmColors.accent,
                    ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    'Mover um valor muda qual instante da fonte aparece. '
                    'Os diamantes tambem ficam visiveis na timeline.',
                    style: TextStyle(
                      fontSize: 10,
                      height: 1.35,
                      color: AmColors.muted.withValues(alpha: 0.9),
                    ),
                  ),
                  const SizedBox(height: 8),
                  for (final keyframe in remap.keyframes)
                    _RemapRow(
                      time: keyframe.time,
                      value: keyframe.value,
                      max: math
                          .max(
                            1,
                            videoSourceSpan(video).inMicroseconds /
                                1000000.0 *
                                1.25,
                          )
                          .toDouble(),
                      onChanged: (value) {
                        controller.setClipTimeRemapKeyframe(
                          layerId,
                          keyframe.time,
                          value,
                        );
                        setSheetState(() {});
                      },
                    ),
                ],
              ],
            ),
          ),
        );
      },
    ),
  );
}

class _SpeedChip extends StatelessWidget {
  const _SpeedChip({
    required this.label,
    required this.selected,
    required this.onTap,
  });

  final String label;
  final bool selected;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    onTap: onTap,
    child: Container(
      padding: const EdgeInsets.symmetric(horizontal: 13, vertical: 8),
      decoration: BoxDecoration(
        color: selected ? AmColors.accentDim : AmColors.chip,
        borderRadius: BorderRadius.circular(9),
      ),
      child: Text(
        label,
        style: TextStyle(
          fontSize: 12,
          color: selected ? AmColors.accent : AmColors.text,
        ),
      ),
    ),
  );
}

class _ToggleRow extends StatelessWidget {
  const _ToggleRow({
    required this.label,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      Expanded(
        child: Text(
          label,
          style: const TextStyle(fontSize: 12, color: AmColors.text),
        ),
      ),
      CupertinoSwitch(
        value: value,
        activeTrackColor: AmColors.accent,
        onChanged: onChanged,
      ),
    ],
  );
}

class _RemapRow extends StatelessWidget {
  const _RemapRow({
    required this.time,
    required this.value,
    required this.max,
    required this.onChanged,
  });

  final Duration time;
  final double value;
  final double max;
  final ValueChanged<double> onChanged;

  @override
  Widget build(BuildContext context) => Row(
    children: [
      SizedBox(
        width: 54,
        child: Text(
          formatTime(time),
          style: const TextStyle(fontSize: 10, color: AmColors.muted),
        ),
      ),
      Expanded(
        child: AmTickRuler(
          value: value.clamp(0.0, max),
          min: 0,
          max: max,
          unitsPerPixel: max / 420,
          height: 40,
          onChanged: onChanged,
        ),
      ),
      SizedBox(
        width: 54,
        child: Text(
          '${value.toStringAsFixed(2)} s',
          textAlign: TextAlign.right,
          style: const TextStyle(fontSize: 10, color: AmColors.text),
        ),
      ),
    ],
  );
}
