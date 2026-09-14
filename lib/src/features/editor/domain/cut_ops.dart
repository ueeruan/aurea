import 'dart:math' as math;

import 'cut.dart';
import 'effect.dart';
import 'keyframe.dart';
import 'layer.dart';
import 'time_core.dart';

const _junctionTolerance = Duration(milliseconds: 8);

EffectInstance? timeRemapEffectOf(VideoLayer layer) {
  for (final effect in layer.effects) {
    if (effect.enabled && effect.type == EffectType.timeRemap) return effect;
  }
  return null;
}

AnimatedDouble? timeRemapTrackOf(VideoLayer layer) =>
    timeRemapEffectOf(layer)?.track('tempo');

bool hasTimeRemap(VideoLayer layer) => timeRemapTrackOf(layer) != null;

CoreLayer _core(VideoLayer layer) => (
  sourceOffsetUs: layer.sourceOffset.inMicroseconds,
  durationUs: layer.duration.inMicroseconds,
  speed: layer.speed,
  reverse: layer.reverse,
  track: timeRemapTrackOf(layer),
);

/// Recorta a funcao fonte-tempo entre dois instantes locais. O resultado
/// fica normalizado para um novo sourceOffset e pode representar inclusive
/// um trecho reverso sem depender do sinal de [VideoLayer.speed].
///
/// O corte e EXATO no nucleo C++: um trecho bezier cortado no meio vira
/// uma sub-bezier (de Casteljau), em vez de herdar a curva inteira do
/// trecho original — o que antes deslocava o tempo no meio do corte.
({Duration sourceOffset, AnimatedDouble track}) sliceVideoTrack(
  VideoLayer layer,
  Duration from,
  Duration to,
) {
  final r = coreSlice(_core(layer), from.inMicroseconds, to.inMicroseconds);
  return (
    sourceOffset:
        layer.sourceOffset +
        Duration(microseconds: (r.minimum * 1000000).round()),
    track: r.track,
  );
}

/// Faixa relativa de fonte tocada pelo clipe. Para remap, vem dos extremos
/// reais da curva (com overshoot); para velocidade constante, da conta
/// duracao * speed.
Duration videoSourceSpan(VideoLayer layer) =>
    Duration(microseconds: coreLayerSpanUs(_core(layer)));

/// Instante RELATIVO a [VideoLayer.sourceOffset] que deve ser mostrado.
/// Esta funcao e pura, portanto scrub direto e reproducao desde o inicio
/// chegam sempre ao mesmo quadro.
Duration videoSourceTimeAt(VideoLayer layer, Duration local) =>
    Duration(microseconds: coreLayerSourceUs(_core(layer), local.inMicroseconds));

Duration videoAbsoluteSourceTimeAt(VideoLayer layer, Duration local) => Duration(
  microseconds: coreLayerAbsoluteUs(_core(layer), local.inMicroseconds),
);

/// Derivada local do Time Remap, em segundos de fonte por segundo de
/// timeline (analitica no nucleo). O sinal negativo identifica reverso.
double videoPlaybackRateAt(VideoLayer layer, Duration local) =>
    coreLayerRate(_core(layer), local.inMicroseconds);

/// Instante de CONTEUDO de uma camada qualquer com o efeito Time Remap
/// (texto, forma, precomp...): segura as pontas como valueAt e nunca e
/// negativo. Video usa [videoSourceTimeAt].
Duration remappedContentTime(AnimatedDouble track, Duration local, {Duration? bakeUntil}) {
  final us = (coreValue(track, local, bakeUntil: bakeUntil) * 1000000).round();
  return Duration(microseconds: us < 0 ? 0 : us);
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

bool isTransitionLayer(Layer layer) =>
    layer is! AudioLayer &&
    layer is! NullLayer &&
    layer is! CameraLayer &&
    layer is! GroupLayer &&
    layer is! AdjustmentLayer;

Layer? layerAfter(List<Layer> layers, String outgoingId) {
  Layer? outgoing;
  for (final l in layers.where(isTransitionLayer)) {
    if (l.id == outgoingId) outgoing = l;
  }
  if (outgoing == null) return null;

  for (final l in layers.where(isTransitionLayer)) {
    if (l.transitionIn?.outgoingLayerId == outgoingId &&
        _touches(l.startTime, outgoing.endTime)) {
      return l;
    }
  }
  final candidates = [
    for (final l in layers.where(isTransitionLayer))
      if (l.id != outgoingId && _touches(l.startTime, outgoing.endTime)) l,
  ];
  if (candidates.isEmpty) return null;
  for (final l in candidates) {
    if (l is VideoLayer &&
        outgoing is VideoLayer &&
        l.sourcePath == outgoing.sourcePath &&
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
  final Layer outgoing;
  final Layer incoming;
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
  // Resolve participants once, instead of scanning the entire timeline
  // for every cut on every preview frame.
  final videos = {for (final l in layers.where(isTransitionLayer)) l.id: l};
  for (final incoming in videos.values) {
    final transition = incoming.transitionIn;
    if (transition == null || !transition.enabled) continue;
    final outgoing = videos[transition.outgoingLayerId];
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

  final Layer outgoing;
  final Layer incoming;
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
  final incoming = layerAfter(layers, outgoingId);
  if (incoming == null) return null;
  Layer? outgoing;
  for (final l in layers.where(isTransitionLayer)) {
    if (l.id == outgoingId) outgoing = l;
  }
  if (outgoing == null) return null;

  Duration timelineFromSource(Duration source, double rate) => Duration(
    microseconds: (source.inMicroseconds / math.max(0.05, rate.abs())).round(),
  );

  final before = incoming is! VideoLayer
      ? requested.duration
      : timelineFromSource(
          incoming.sourceOffset,
          videoPlaybackRateAt(incoming, Duration.zero),
        );
  var after = Duration.zero;
  var knownAfter = false;
  if (outgoing is VideoLayer &&
      incoming is VideoLayer &&
      outgoing.sourcePath == incoming.sourcePath) {
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

/// Keep incoming clips above outgoing clips during their shared window, even
/// after layer reordering. Unrelated overlays retain their exact stack slots.
List<Layer> transitionPaintOrder(
  List<Layer> order,
  List<ClipTransitionContext> contexts,
) {
  if (contexts.isEmpty) return order;
  final parents = <String, String>{};
  String root(String id) {
    var at = id;
    while (parents[at] != null && parents[at] != at) {
      at = parents[at]!;
    }
    return at;
  }

  for (final c in contexts) {
    parents[root(c.incoming.id)] = root(c.outgoing.id);
  }
  final members = {
    for (final c in contexts) ...[c.outgoing.id, c.incoming.id],
  };
  final slots = <String, List<int>>{};
  for (var i = 0; i < order.length; i++) {
    if (members.contains(order[i].id)) (slots[root(order[i].id)] ??= []).add(i);
  }
  final result = [...order];
  for (final indices in slots.values) {
    final layers = [for (final i in indices) order[i]]
      ..sort((a, b) => a.startTime.compareTo(b.startTime));
    for (var i = 0; i < indices.length; i++) {
      result[indices[i]] = layers[i];
    }
  }
  return result;
}
