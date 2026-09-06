import 'keyframe.dart';
import 'layer.dart';

/// Ordem em que os elementos selecionados recebem o atraso da cascata.
enum CascadeOrder { start, center, end, random }

/// Escalona keyframes reais das camadas selecionadas. O resultado continua
/// editável trilha por trilha; não há efeito procedural escondendo o timing.
/// Intervalo zero é estritamente neutro e devolve as mesmas instâncias.
List<Layer> cascadeLayerKeyframes(
  List<Layer> layers,
  Set<String> selectedIds, {
  Duration interval = const Duration(milliseconds: 40),
  CascadeOrder order = CascadeOrder.start,
  Easing? ease,
}) {
  if (selectedIds.length < 2 || interval <= Duration.zero) return layers;
  final ordered = orderedCascadeLayers(layers, selectedIds, order: order);
  final rank = <String, int>{
    for (var i = 0; i < ordered.length; i++) ordered[i].id: i,
  };
  return [
    for (final layer in layers)
      if (rank[layer.id] case final i?)
        _shiftLayer(layer, interval * i, ease)
      else
        layer,
  ];
}

/// Resolve a ordem visual uma vez para que a cascata e os vínculos avançados
/// usem exatamente a mesma fonte e os mesmos ranks.
List<Layer> orderedCascadeLayers(
  List<Layer> layers,
  Set<String> selectedIds, {
  required CascadeOrder order,
}) {
  final selected = [
    for (final l in layers)
      if (selectedIds.contains(l.id)) l,
  ];
  switch (order) {
    case CascadeOrder.start:
      return selected;
    case CascadeOrder.end:
      return selected.reversed.toList();
    case CascadeOrder.center:
      final center = (selected.length - 1) / 2;
      return [...selected]..sort((a, b) {
        final ia = selected.indexOf(a), ib = selected.indexOf(b);
        final distance = (ia - center).abs().compareTo((ib - center).abs());
        return distance != 0 ? distance : ia.compareTo(ib);
      });
    case CascadeOrder.random:
      final out = [...selected];
      var seed = 0x6A09E667;
      for (final layer in selected) {
        for (final unit in layer.id.codeUnits) {
          seed = ((seed * 31) ^ unit) & 0x7fffffff;
        }
      }
      for (var i = out.length - 1; i > 0; i--) {
        seed = (1103515245 * seed + 12345) & 0x7fffffff;
        final j = seed % (i + 1);
        final tmp = out[i];
        out[i] = out[j];
        out[j] = tmp;
      }
      return out;
  }
}

Layer _shiftLayer(Layer layer, Duration delay, Easing? ease) => layer.copyLayer(
  position: _shiftOffset(layer.position, delay, ease),
  positionZ: _shiftDouble(layer.positionZ, delay, ease),
  scaleX: _shiftDouble(layer.scaleX, delay, ease),
  scaleY: _shiftDouble(layer.scaleY, delay, ease),
  rotation: _shiftDouble(layer.rotation, delay, ease),
  rotationX: _shiftDouble(layer.rotationX, delay, ease),
  rotationY: _shiftDouble(layer.rotationY, delay, ease),
  opacity: _shiftDouble(layer.opacity, delay, ease),
  skewX: _shiftDouble(layer.skewX, delay, ease),
  skewY: _shiftDouble(layer.skewY, delay, ease),
  pivot: _shiftOffset(layer.pivot, delay, ease),
  effects: [
    for (final effect in layer.effects)
      effect.copyWith(
        params: {
          for (final entry in effect.params.entries)
            entry.key: _shiftDouble(entry.value, delay, ease),
        },
      ),
  ],
);

AnimatedDouble _shiftDouble(
  AnimatedDouble track,
  Duration delay,
  Easing? ease,
) => AnimatedDouble(track.base, [
  for (final keyframe in track.keyframes)
    keyframe.copyWith(time: keyframe.time + delay, ease: ease ?? keyframe.ease),
], track.loop);

AnimatedOffset _shiftOffset(
  AnimatedOffset track,
  Duration delay,
  Easing? ease,
) => AnimatedOffset(track.base, [
  for (final keyframe in track.keyframes)
    keyframe.copyWith(time: keyframe.time + delay, ease: ease ?? keyframe.ease),
], track.loop);
