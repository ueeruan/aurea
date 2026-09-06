import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import '../../domain/shape.dart';
import 'am_colors.dart';
import 'am_widgets.dart';
import 'color_picker_sheet.dart';

Future<void> showGradientFillSheet(
  BuildContext context,
  String layerId, {
  PlaybackController? playback,
}) => showParamSheet(
  context,
  title: 'Gradiente vetorial',
  heightFactor: .60,
  builder: (_) => playback == null
      ? _GradientPanel(layerId: layerId)
      : ValueListenableBuilder<Duration>(
          valueListenable: playback.time,
          builder: (_, t, _) => _GradientPanel(layerId: layerId, time: t),
        ),
);

class _GradientPanel extends ConsumerWidget {
  const _GradientPanel({required this.layerId, this.time = Duration.zero});
  final String layerId;
  final Duration time;
  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final layer = ref.watch(editorControllerProvider).layerById(layerId);
    if (layer is! ShapeLayer) return const SizedBox.shrink();
    final controller = ref.read(editorControllerProvider.notifier);
    final local = layer.localTime(time);
    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        const Text(
          'Cores e distribuicao',
          style: TextStyle(color: AmColors.text, fontSize: 18),
        ),
        for (final g in layer.contents.whereType<ShapeGradientFill>()) ...[
          const SizedBox(height: 12),
          Container(
            height: 28,
            decoration: BoxDecoration(
              gradient: LinearGradient(
                colors: g.colorsAt(local),
                stops: g.resolvedStops,
              ),
            ),
          ),
          Row(
            children: [
              Expanded(
                child: SwitchListTile.adaptive(
                  title: const Text('Animar cores'),
                  value: g.colorFrames.isNotEmpty,
                  onChanged: (enabled) => controller.updateShapeGradient(
                    layerId,
                    g.id,
                    (old) {
                      if (enabled) return old.withColorsAt(local, old.paradas);
                      final colors = old.colorsAt(local);
                      return old.copyWith(
                        colorA: colors.first,
                        colorB: colors.last,
                        extras: colors.sublist(1, colors.length - 1),
                        colorFrames: [],
                      );
                    },
                  ),
                ),
              ),
              if (g.colorFrames.isNotEmpty)
                CupertinoButton(
                  onPressed: () => controller.updateShapeGradient(
                    layerId,
                    g.id,
                    (old) => old.colorFrames.any((k) => k.time == local)
                        ? old.copyWith(
                            colorFrames: old.colorFrames
                                .where((k) => k.time != local)
                                .toList(),
                          )
                        : old.withColorsAt(local, old.colorsAt(local)),
                  ),
                  child: Icon(
                    g.colorFrames.any((k) => k.time == local)
                        ? CupertinoIcons.rhombus_fill
                        : CupertinoIcons.rhombus,
                  ),
                ),
            ],
          ),
          SwitchListTile.adaptive(
            title: const Text('Radial'),
            value: g.radial,
            onChanged: (v) => controller.updateShapeGradient(
              layerId,
              g.id,
              (old) => old.copyWith(radial: v),
            ),
          ),
          for (var i = 0; i < g.paradas.length; i++)
            Row(
              children: [
                CupertinoButton(
                  padding: const EdgeInsets.all(6),
                  onPressed: () async {
                    final index = i;
                    final color = await showColorPicker(
                      context,
                      initial: g.colorsAt(local)[index],
                    );
                    if (color == null || !context.mounted) return;
                    controller.updateShapeGradient(layerId, g.id, (old) {
                      final colors = [...old.colorsAt(local)];
                      if (index >= colors.length) return old;
                      colors[index] = color;
                      if (old.colorFrames.isNotEmpty) {
                        return old.withColorsAt(local, colors);
                      }
                      return old.copyWith(
                        colorA: colors.first,
                        colorB: colors.last,
                        extras: colors.sublist(1, colors.length - 1),
                      );
                    });
                  },
                  child: Container(
                    width: 28,
                    height: 28,
                    color: g.colorsAt(local)[i],
                  ),
                ),
                Text(
                  '${(g.resolvedStops[i] * 100).round()}%',
                  style: const TextStyle(color: AmColors.text),
                ),
                Expanded(
                  child: AmTickRuler(
                    value: g.resolvedStops[i],
                    min: 0,
                    max: 1,
                    height: 48,
                    unitsPerPixel: .003,
                    onChanged: (v) =>
                        controller.updateShapeGradient(layerId, g.id, (old) {
                          final stops = [...old.resolvedStops];
                          stops[i] = v.clamp(
                            i == 0 ? 0 : stops[i - 1],
                            i == stops.length - 1 ? 1 : stops[i + 1],
                          );
                          return old.copyWith(stops: stops);
                        }),
                  ),
                ),
              ],
            ),
          _slider(
            'Angulo',
            g.angleDeg,
            -180,
            180,
            (v) => controller.updateShapeGradient(
              layerId,
              g.id,
              (old) => old.copyWith(angleDeg: v),
            ),
          ),
          _slider(
            'Centro X',
            g.center.dx,
            -1,
            1,
            (v) => controller.updateShapeGradient(
              layerId,
              g.id,
              (old) => old.copyWith(center: Offset(v, old.center.dy)),
            ),
          ),
          _slider(
            'Centro Y',
            g.center.dy,
            -1,
            1,
            (v) => controller.updateShapeGradient(
              layerId,
              g.id,
              (old) => old.copyWith(center: Offset(old.center.dx, v)),
            ),
          ),
          _slider(
            'Alcance',
            g.radiusScale,
            .05,
            3,
            (v) => controller.updateShapeGradient(
              layerId,
              g.id,
              (old) => old.copyWith(radiusScale: v),
            ),
          ),
        ],
      ],
    );
  }

  Widget _slider(
    String name,
    double v,
    double min,
    double max,
    ValueChanged<double> update,
  ) => Row(
    children: [
      SizedBox(
        width: 82,
        child: Text(name, style: const TextStyle(color: AmColors.text)),
      ),
      Expanded(
        child: AmTickRuler(
          min: min,
          max: max,
          height: 56,
          unitsPerPixel: (max - min) / 240,
          value: v.isFinite ? v.clamp(min, max) : min,
          onChanged: update,
        ),
      ),
      SizedBox(
        width: 44,
        child: Text(
          v.toStringAsFixed(2),
          style: const TextStyle(color: AmColors.muted),
        ),
      ),
    ],
  );
}
