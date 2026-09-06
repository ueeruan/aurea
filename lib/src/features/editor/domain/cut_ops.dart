import 'dart:math' as math;

import 'cut.dart';
import 'effect.dart';
import 'keyframe.dart';
import 'layer.dart';

const _junctionTolerance = Duration(milliseconds: 8);
const _minimumSourceSpan = Duration(milliseconds: 34);

EffectInstance? timeRemapEffectOf(VideoLayer layer) {
  for (final effect in layer.effects) {
    if (effect.enabled && effect.type == EffectType.timeRemap) return effect;
  }
  return null;
}

AnimatedDouble? timeRemapTrackOf(VideoLayer layer) =>
    timeRemapEffectOf(layer)?.track('tempo');

bool hasTimeRemap(VideoLayer layer) => timeRemapTrackOf(layer) != null;

/// Recorta a funcao fonte-tempo entre dois instantes locais. O resultado
/// fica normalizado para um novo sourceOffset e pode representar inclusive
/// um trecho reverso sem depender do sinal de [VideoLayer.speed].
({Duration sourceOffset, AnimatedDouble track}) sliceVideoTrack(
  VideoLayer layer,
  Duration from,
  Duration to,
) {
  final originalTrack = timeRemapTrackOf(layer);
  final times = <Duration>{from, to};
  if (originalTrack != null) {
    for (final keyframe in originalTrack.keyframes) {
      if (keyframe.time > from && keyframe.time < to) {
        times.add(keyframe.time);
      }
    }
  }
  final sorted = times.toList()..sort();
  final values = <double>[
    for (final time in sorted)
      videoSourceTimeAt(layer, time).inMicroseconds / 1000000.0,
  ];
  final minimum = values.reduce(math.min);
  var track = AnimatedDouble(values.first - minimum);
  for (var i = 0; i < sorted.length; i++) {
    track = track.withKeyframe(
      sorted[i] - from,
      values[i] - minimum,
      originalTrack?.easeAt(sorted[i]) ?? Easing.linear,
    );
  }
  return (
    sourceOffset:
        layer.sourceOffset +
        Duration(microseconds: (minimum * 1000000).round()),
    track: track,
  );
}

/// Faixa relativa de fonte tocada pelo clipe. Para remap, vem dos valores
/// reais do trilho; para velocidade constante, da conta duracao * speed.
Duration videoSourceSpan(VideoLayer layer) {
  final track = timeRemapTrackOf(layer);
  if (track == null) return layer.sourceSpan;
  var lo = track.base;
  var hi = track.base;
  for (final k in track.keyframes) {
    lo = math.min(lo, k.value);
    hi = math.max(hi, k.value);
  }
  final seconds = math.max(0.0, hi - math.min(0.0, lo));
  final span = Duration(microseconds: (seconds * 1000000).round());
  return span < _minimumSourceSpan ? _minimumSourceSpan : span;
}

/// Instante RELATIVO a [VideoLayer.sourceOffset] que deve ser mostrado.
/// Esta funcao e pura, portanto scrub direto e reproducao desde o inicio
/// chegam sempre ao mesmo quadro.
Duration videoSourceTimeAt(VideoLayer layer, Duration local) {
  final track = timeRemapTrackOf(layer);
  final forwardUs = track == null
      ? (local.inMicroseconds * layer.speed).round()
      : (_extendedTrackValue(track, local) * 1000000).round();
  if (!layer.reverse) return Duration(microseconds: forwardUs);
  return Duration(
    microseconds: videoSourceSpan(layer).inMicroseconds - forwardUs,
  );
}

double _extendedTrackValue(AnimatedDouble track, Duration time) {
  final keyframes = track.keyframes;
  if (keyframes.isEmpty) return track.base;
  final first = keyframes.first;
  final last = keyframes.last;
  if (time >= first.time && time <= last.time) return track.valueAt(time);
  if (keyframes.length < 2) return time < first.time ? first.value : last.value;
  final a = time < first.time ? keyframes[0] : keyframes[keyframes.length - 2];
  final b = time < first.time ? keyframes[1] : keyframes[keyframes.length - 1];
  final dt = (b.time - a.time).inMicroseconds;
  if (dt == 0) return time < first.time ? first.value : last.value;
  final slope = (b.value - a.value) / dt;
  final anchor = time < first.time ? a : b;
  return anchor.value + (time - anchor.time).inMicroseconds * slope;
}

Duration videoAbsoluteSourceTimeAt(VideoLayer layer, Duration local) {
  final absolute = layer.sourceOffset + videoSourceTimeAt(layer, local);
  return absolute < Duration.zero ? Duration.zero : absolute;
}

/// Melhor instante da timeline para um quadro relativo da fonte. A via
/// constante e exata; Time Remap pode voltar ou repetir quadros, entao a
/// procura amostrada escolhe a primeira ocorrencia mais proxima de forma
/// deterministica.
Duration videoLocalTimeForSource(VideoLayer layer, Duration sourceTime) {
  if (!hasTimeRemap(layer)) {
    final wanted = layer.reverse
        ? videoSourceSpan(layer) - sourceTime
        : sourceTime;
    final us = (wanted.inMicroseconds / math.max(0.05, layer.speed)).round();
    return Duration(
      microseconds: us.clamp(0, layer.duration.inMicroseconds).toInt(),
    );
  }

  const samples = 480;
  var best = Duration.zero;
  var bestDistance = (videoSourceTimeAt(layer, best) - sourceTime).abs();
  for (var i = 1; i <= samples; i++) {
    final candidate = Duration(
      microseconds: (layer.duration.inMicroseconds * i / samples).round(),
    );
    final distance = (videoSourceTimeAt(layer, candidate) - sourceTime).abs();
    if (distance < bestDistance) {
      best = candidate;
      bestDistance = distance;
    }
  }
  return best;
}

/// Derivada local do Time Remap, em segundos de fonte por segundo de
/// timeline. O sinal negativo identifica reverso.
double videoPlaybackRateAt(VideoLayer layer, Duration local) {
  if (!hasTimeRemap(layer)) {
    return layer.reverse ? -layer.speed : layer.speed;
  }
  const half = Duration(milliseconds: 2);
  final a = videoSourceTimeAt(layer, local - half);
  final b = videoSourceTimeAt(layer, local + half);
  return (b - a).inMicroseconds / (half.inMicroseconds * 2);
}

EffectInstance withTimeRemapTrack(VideoLayer layer, AnimatedDouble track) {
  final current = timeRemapEffectOf(layer);
  if (current != null) {
    return current.copyWith(params: {...current.params, 'tempo': track});
  }
  return EffectInstance(type: EffectType.timeRemap, params: {'tempo': track});
}

List<EffectInstance> replaceTimeRemap(VideoLayer layer, AnimatedDouble? track) {
  final out = <EffectInstance>[];
  var inserted = false;
  for (final effect in layer.effects) {
    if (effect.type != EffectType.timeRemap) {
      out.add(effect);
    } else if (track != null && !inserted) {
      out.add(withTimeRemapTrack(layer, track));
      inserted = true;
    }
  }
  if (track != null && !inserted) out.add(withTimeRemapTrack(layer, track));
  return out;
}

bool _touches(Duration a, Duration b) => (a - b).abs() <= _junctionTolerance;

/// B imediatamente depois de A no tempo. Continuidade da mesma fonte tem
/// prioridade; em seguida vale a proximidade na pilha visual.
VideoLayer? videoAfter(List<Layer> layers, String outgoingId) {
  VideoLayer? outgoing;
  for (final l in layers.whereType<VideoLayer>()) {
    if (l.id == outgoingId) outgoing = l;
  }
  if (outgoing == null) return null;

  for (final l in layers.whereType<VideoLayer>()) {
    if (l.transitionIn?.outgoingLayerId == outgoingId &&
        _touches(l.startTime, outgoing.endTime)) {
      return l;
    }
  }
  final candidates = [
    for (final l in layers.whereType<VideoLayer>())
      if (l.id != outgoingId && _touches(l.startTime, outgoing.endTime)) l,
  ];
  if (candidates.isEmpty) return null;
  for (final l in candidates) {
    if (l.sourcePath == outgoing.sourcePath &&
        _touches(
          l.sourceOffset,
          outgoing.sourceOffset + videoSourceSpan(outgoing),
        )) {
      return l;
    }
  }
  final index = layers.indexOf(outgoing);
  candidates.sort(
    (a, b) => (layers.indexOf(a) - index).abs().compareTo(
      (layers.indexOf(b) - index).abs(),
    ),
  );
  return candidates.first;
}

class ClipTransitionContext {
  const ClipTransitionContext({
    required this.transition,
    required this.outgoing,
    required this.incoming,
    required this.window,
    required this.progress,
  });

  final ClipTransition transition;
  final VideoLayer outgoing;
  final VideoLayer incoming;
  final TransitionWindow window;
  final double progress;

  bool isParticipant(String id) => id == outgoing.id || id == incoming.id;

  double opacityFor(String id) {
    final incomingLayer = id == incoming.id;
    if (transition.type == ClipTransitionType.black) {
      return incomingLayer
          ? (progress <= 0.5 ? 0 : (progress - 0.5) * 2)
          : (progress >= 0.5 ? 0 : 1 - progress * 2);
    }
    // A e pintado embaixo e B por cima. Atenuar os dois em srcOver
    // escurece o meio da transicao (A ainda e multiplicado pela
    // transparencia de B). A permanece opaco; B revela-se com p. Isso
    // tambem deixa wipe/whip/zoom/glitch/effect preservarem a imagem de A
    // nas regioes em que B ainda nao foi revelado.
    return incomingLayer ? progress : 1;
  }
}

/// Todas as transicoes ativas neste quadro, em ordem temporal de juncao.
///
/// Uma lista (em vez de um unico contexto global) e necessaria quando dois
/// cortes curtos possuem janelas que se sobrepoem: a camada do meio pode ser
/// simultaneamente a entrada de uma transicao e a saida da seguinte.
List<ClipTransitionContext> transitionContextsAt(
  List<Layer> layers,
  Duration globalTime,
) {
  final contexts = <ClipTransitionContext>[];
  for (final incoming in layers.whereType<VideoLayer>()) {
    final transition = incoming.transitionIn;
    if (transition == null || !transition.enabled) continue;
    VideoLayer? outgoing;
    for (final l in layers.whereType<VideoLayer>()) {
      if (l.id == transition.outgoingLayerId) {
        outgoing = l;
        break;
      }
    }
    if (outgoing == null) continue;
    // A referencia mora em B, mas nao transforma um vinculo deslocado
    // numa transicao eterna. Ripple/split que deixaram A e B separados
    // tornam o dado orfao ate a juncao voltar a encostar.
    if (!_touches(outgoing.endTime, incoming.startTime)) continue;
    final window = transition.windowAt(incoming.startTime);
    if (!window.contains(globalTime)) continue;
    contexts.add(
      ClipTransitionContext(
        transition: transition,
        outgoing: outgoing,
        incoming: incoming,
        window: window,
        progress: transition.progressAt(globalTime, incoming.startTime),
      ),
    );
  }
  contexts.sort((a, b) {
    final byJunction = a.window.junction.compareTo(b.window.junction);
    if (byJunction != 0) return byJunction;
    return a.incoming.id.compareTo(b.incoming.id);
  });
  return contexts;
}

/// Compatibilidade para consumidores que so conseguem processar um corte.
/// Preview e playback devem usar [transitionContextsAt].
ClipTransitionContext? transitionContextAt(
  List<Layer> layers,
  Duration globalTime,
) {
  final contexts = transitionContextsAt(layers, globalTime);
  return contexts.isEmpty ? null : contexts.first;
}

List<ClipTransitionContext> transitionContextsForLayer(
  List<Layer> layers,
  String layerId,
  Duration globalTime, {
  Iterable<ClipTransitionContext>? contexts,
}) => [
  for (final context in contexts ?? transitionContextsAt(layers, globalTime))
    if (context.isParticipant(layerId)) context,
];

bool visibleForCut(
  List<Layer> layers,
  Layer layer,
  Duration globalTime, {
  Iterable<ClipTransitionContext>? contexts,
}) {
  if (layer.activeAt(globalTime)) return true;
  return (contexts ?? transitionContextsAt(layers, globalTime)).any(
    (context) => context.isParticipant(layer.id),
  );
}

Duration localTimeForCut(
  Layer layer,
  Duration globalTime,
  ClipTransitionContext? transition,
) => localTimeForCutContexts(
  layer,
  globalTime,
  transition == null ? const <ClipTransitionContext>[] : [transition],
);

Duration localTimeForCutContexts(
  Layer layer,
  Duration globalTime,
  Iterable<ClipTransitionContext> transitions,
) {
  var local = layer.localTime(globalTime);
  final freezeEdges = transitions.any(
    (transition) =>
        transition.transition.freezeEdges && transition.isParticipant(layer.id),
  );
  if (freezeEdges) {
    local = Duration(
      microseconds: local.inMicroseconds
          .clamp(0, layer.duration.inMicroseconds)
          .toInt(),
    );
  }
  return local;
}

class TransitionHandleReport {
  const TransitionHandleReport({
    required this.outgoing,
    required this.incoming,
    required this.requested,
    required this.availableBeforeB,
    required this.availableAfterA,
    required this.knownAfterA,
  });

  final VideoLayer outgoing;
  final VideoLayer incoming;
  final ClipTransition requested;
  final Duration availableBeforeB;
  final Duration availableAfterA;
  final bool knownAfterA;

  Duration get requiredBeforeB =>
      requested.windowAt(incoming.startTime).junction -
      requested.windowAt(incoming.startTime).start;

  Duration get requiredAfterA =>
      requested.windowAt(incoming.startTime).end -
      requested.windowAt(incoming.startTime).junction;

  bool get lacksBeforeB => availableBeforeB < requiredBeforeB;
  bool get lacksAfterA => knownAfterA && availableAfterA < requiredAfterA;
  bool get hasEnough => !lacksBeforeB && !lacksAfterA;

  Duration get maximumDuration {
    final beforeFactor = switch (requested.alignment) {
      TransitionAlignment.center => 2,
      TransitionAlignment.endA => 1,
      TransitionAlignment.startB => 0,
    };
    final afterFactor = switch (requested.alignment) {
      TransitionAlignment.center => 2,
      TransitionAlignment.endA => 0,
      TransitionAlignment.startB => 1,
    };
    var us = requested.duration.inMicroseconds;
    if (beforeFactor > 0) {
      us = math.min(us, availableBeforeB.inMicroseconds * beforeFactor);
    }
    if (knownAfterA && afterFactor > 0) {
      us = math.min(us, availableAfterA.inMicroseconds * afterFactor);
    }
    return Duration(microseconds: math.max(0, us).toInt());
  }
}

TransitionHandleReport? transitionHandles(
  List<Layer> layers,
  String outgoingId,
  ClipTransition requested,
) {
  final incoming = videoAfter(layers, outgoingId);
  if (incoming == null) return null;
  VideoLayer? outgoing;
  for (final l in layers.whereType<VideoLayer>()) {
    if (l.id == outgoingId) outgoing = l;
  }
  if (outgoing == null) return null;

  Duration timelineFromSource(Duration source, double rate) => Duration(
    microseconds: (source.inMicroseconds / math.max(0.05, rate.abs())).round(),
  );

  final before = timelineFromSource(
    incoming.sourceOffset,
    videoPlaybackRateAt(incoming, Duration.zero),
  );
  var after = Duration.zero;
  var knownAfter = false;
  if (outgoing.sourcePath == incoming.sourcePath) {
    final outgoingEnd = outgoing.sourceOffset + videoSourceSpan(outgoing);
    if (incoming.sourceOffset >= outgoingEnd - _junctionTolerance) {
      after = timelineFromSource(
        videoSourceSpan(incoming),
        videoPlaybackRateAt(outgoing, outgoing.duration),
      );
      knownAfter = true;
    }
  }
  return TransitionHandleReport(
    outgoing: outgoing,
    incoming: incoming,
    requested: requested,
    availableBeforeB: before,
    availableAfterA: after,
    knownAfterA: knownAfter,
  );
}
