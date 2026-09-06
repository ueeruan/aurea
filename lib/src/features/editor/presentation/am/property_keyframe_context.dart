import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import '../../domain/layer.dart';
import '../../domain/video_project.dart';
import 'am_colors.dart';

String labelOfProp(LayerProp prop) => switch (prop) {
  LayerProp.position => 'Posição',
  LayerProp.rotation => 'Rotação',
  LayerProp.scale => 'Escala',
  LayerProp.skew => 'Inclinação',
  LayerProp.pivot => 'Pivô',
  LayerProp.opacity => 'Opacidade',
  LayerProp.parent => 'Vínculo',
};

/// União dos eixos: uma escala animada só em Y também é animada.
Set<int> keyframeTimesForProp(Layer layer, LayerProp prop) => switch (prop) {
  LayerProp.position => layer.positionTimesUs,
  LayerProp.rotation => layer.rotationTimesUs,
  LayerProp.scale => layer.scaleTimesUs,
  LayerProp.skew => layer.skewTimesUs,
  LayerProp.pivot => layer.pivotTimesUs,
  LayerProp.opacity => layer.opacityTimesUs,
  LayerProp.parent => const {},
};

/// Estado e navegação visíveis, sem depender de acertar um diamante pequeno.
/// Os tempos são locais à camada; o callback recebe tempo da composição.
class PropertyKeyframeContext extends StatelessWidget {
  const PropertyKeyframeContext({
    super.key,
    required this.layer,
    required this.prop,
    required this.time,
    required this.onSeek,
    this.compact = false,
  });

  final Layer layer;
  final LayerProp prop;
  final Duration time;
  final ValueChanged<Duration> onSeek;
  final bool compact;

  @override
  Widget build(BuildContext context) {
    final times = keyframeTimesForProp(layer, prop).toList()..sort();
    final local = layer.localTime(time).inMicroseconds;
    final index = times.indexWhere((us) => (us - local).abs() < 8000);
    int? previous, next;
    for (final value in times) {
      if ((value - local).abs() < 8000) continue;
      if (value < local) previous = value;
      if (value > local) {
        next = value;
        break;
      }
    }
    final status = times.isEmpty
        ? 'Sem keyframes · toque no diamante +'
        : index >= 0
        ? 'Keyframe ${index + 1} de ${times.length}'
        : local < times.first
        ? '${times.length} keyframes · antes do primeiro'
        : local > times.last
        ? '${times.length} keyframes · após o último'
        : '${times.length} keyframes · entre marcas';
    Widget jump(String key, String label, IconData icon, int? target) =>
        IconButton(
          key: ValueKey(key),
          tooltip: label,
          constraints: BoxConstraints.tightFor(
            width: 48,
            height: compact ? 32 : 48,
          ),
          padding: EdgeInsets.zero,
          onPressed: target == null
              ? null
              : () => onSeek(layer.startTime + Duration(microseconds: target)),
          icon: Icon(icon, size: 20),
          color: AmColors.accent,
          disabledColor: AmColors.muted.withValues(alpha: .35),
        );
    return ColoredBox(
      color: AmColors.panel,
      child: Row(
        children: [
          jump(
            'previous-property-keyframe',
            'Keyframe anterior de ${labelOfProp(prop)}',
            CupertinoIcons.backward_end,
            previous,
          ),
          Expanded(
            child: Semantics(
              label: '${labelOfProp(prop)}. $status',
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    labelOfProp(prop),
                    maxLines: 1,
                    style: TextStyle(
                      color: AmColors.text,
                      fontSize: 12,
                      height: compact ? 1.1 : null,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  Text(
                    status,
                    key: const ValueKey('property-keyframe-status'),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      color: AmColors.muted,
                      fontSize: 10,
                      height: compact ? 1.1 : null,
                    ),
                  ),
                ],
              ),
            ),
          ),
          jump(
            'next-property-keyframe',
            'Próximo keyframe de ${labelOfProp(prop)}',
            CupertinoIcons.forward_end,
            next,
          ),
        ],
      ),
    );
  }
}
