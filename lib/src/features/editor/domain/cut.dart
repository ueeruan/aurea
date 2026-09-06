import 'dart:math' as math;

import 'effect.dart';
import 'keyframe.dart';

/// Transicoes disponiveis diretamente na juncao entre dois clipes.
enum ClipTransitionType {
  dissolve,
  black,
  wipe,
  zoomWarp,
  whip,
  glitch,
  effect,
}

extension ClipTransitionTypeLabel on ClipTransitionType {
  String get label => switch (this) {
    ClipTransitionType.dissolve => 'Dissolve',
    ClipTransitionType.black => 'Fundo preto',
    ClipTransitionType.wipe => 'Wipe',
    ClipTransitionType.zoomWarp => 'Zoom warp',
    ClipTransitionType.whip => 'Chicote',
    ClipTransitionType.glitch => 'Glitch',
    ClipTransitionType.effect => 'Com efeito',
  };

  String get shortLabel => switch (this) {
    ClipTransitionType.dissolve => 'Diss',
    ClipTransitionType.black => 'Preto',
    ClipTransitionType.wipe => 'Wipe',
    ClipTransitionType.zoomWarp => 'Zoom',
    ClipTransitionType.whip => 'Chicote',
    ClipTransitionType.glitch => 'Glitch',
    ClipTransitionType.effect => 'FX',
  };
}

enum TransitionAlignment { center, endA, startB }

extension TransitionAlignmentLabel on TransitionAlignment {
  String get label => switch (this) {
    TransitionAlignment.center => 'Centro',
    TransitionAlignment.endA => 'Fim de A',
    TransitionAlignment.startB => 'Inicio de B',
  };
}

/// O que fazer quando a fonte nao tem quadros suficientes alem do corte.
enum TransitionEdgeFallback { none, shorten, freeze }

/// Intervalo global em que uma transicao esta ativa.
class TransitionWindow {
  const TransitionWindow({
    required this.start,
    required this.end,
    required this.junction,
  });

  final Duration start;
  final Duration end;
  final Duration junction;

  Duration get duration => end - start;

  bool contains(Duration t) =>
      duration > Duration.zero && t >= start && t < end;

  double rawProgressAt(Duration t) {
    final us = duration.inMicroseconds;
    if (us <= 0) return t < start ? 0 : 1;
    return ((t - start).inMicroseconds / us).clamp(0.0, 1.0);
  }
}

/// Dados que vivem na ENTRADA do clipe B, mas identificam tambem A.
/// Assim uma juncao possui exatamente uma transicao e continua estavel
/// quando a ordem visual das camadas muda.
class ClipTransition {
  ClipTransition({
    required this.outgoingLayerId,
    this.type = ClipTransitionType.dissolve,
    this.duration = const Duration(milliseconds: 300),
    this.alignment = TransitionAlignment.center,
    this.curve = Easing.easeInOut,
    this.crossfadeAudio = true,
    this.freezeEdges = false,
    this.rippleLayerIds = const [],
    this.effect,
    AnimatedDouble? effectAmount,
  }) : effectAmount = effectAmount ?? _defaultBell(duration);

  final String outgoingLayerId;
  final ClipTransitionType type;
  final Duration duration;
  final TransitionAlignment alignment;
  final Easing curve;

  /// Dissolve de video cruza o audio por padrao. Pode ser desligado sem
  /// desligar a transicao visual.
  final bool crossfadeAudio;

  /// Fallback explicitamente escolhido quando faltam handles de midia.
  final bool freezeEdges;

  /// IDs deslocados pelo trim magnetico de "Fim de A". Guardar o
  /// conjunto torna mudar/remover a transicao exatamente reversivel mesmo
  /// quando ha uma camada sobreposta perto da juncao.
  final List<String> rippleLayerIds;

  /// Qualquer efeito do catalogo pode ser usado como transicao.
  final EffectInstance? effect;

  /// Intensidade real, editavel: zero nas pontas e um no meio.
  final AnimatedDouble effectAmount;

  bool get enabled => duration > Duration.zero;

  TransitionWindow windowAt(Duration junction) {
    if (duration <= Duration.zero) {
      return TransitionWindow(
        start: junction,
        end: junction,
        junction: junction,
      );
    }
    return switch (alignment) {
      TransitionAlignment.center => TransitionWindow(
        start: junction - Duration(microseconds: duration.inMicroseconds ~/ 2),
        end:
            junction +
            Duration(
              microseconds:
                  duration.inMicroseconds - duration.inMicroseconds ~/ 2,
            ),
        junction: junction,
      ),
      TransitionAlignment.endA => TransitionWindow(
        start: junction - duration,
        end: junction,
        junction: junction,
      ),
      TransitionAlignment.startB => TransitionWindow(
        start: junction,
        end: junction + duration,
        junction: junction,
      ),
    };
  }

  double progressAt(Duration globalTime, Duration junction) {
    final raw = windowAt(junction).rawProgressAt(globalTime);
    return curve.transform(raw).clamp(0.0, 1.0);
  }

  double amountAt(Duration globalTime, Duration junction) {
    final w = windowAt(junction);
    final local = Duration(
      microseconds: (w.rawProgressAt(globalTime) * duration.inMicroseconds)
          .round(),
    );
    return effectAmount.valueAt(local).clamp(0.0, 1.0);
  }

  ClipTransition copyWith({
    String? outgoingLayerId,
    ClipTransitionType? type,
    Duration? duration,
    TransitionAlignment? alignment,
    Easing? curve,
    bool? crossfadeAudio,
    bool? freezeEdges,
    List<String>? rippleLayerIds,
    EffectInstance? effect,
    bool clearEffect = false,
    AnimatedDouble? effectAmount,
  }) {
    final nextDuration = duration ?? this.duration;
    return ClipTransition(
      outgoingLayerId: outgoingLayerId ?? this.outgoingLayerId,
      type: type ?? this.type,
      duration: nextDuration,
      alignment: alignment ?? this.alignment,
      curve: curve ?? this.curve,
      crossfadeAudio: crossfadeAudio ?? this.crossfadeAudio,
      freezeEdges: freezeEdges ?? this.freezeEdges,
      rippleLayerIds: rippleLayerIds ?? this.rippleLayerIds,
      effect: clearEffect ? null : (effect ?? this.effect),
      effectAmount:
          effectAmount ??
          (duration == null ? this.effectAmount : _defaultBell(nextDuration)),
    );
  }

  static AnimatedDouble _defaultBell(Duration duration) {
    final half = Duration(microseconds: duration.inMicroseconds ~/ 2);
    return AnimatedDouble(0)
        .withKeyframe(Duration.zero, 0, Easing.easeOut)
        .withKeyframe(half, 1, Easing.easeInOut)
        .withKeyframe(duration, 0, Easing.easeIn);
  }
}

enum SpeedRampPreset { impacto, heroi, bala, montagem }

enum FreezePlacement { insideClip, separateClip }

extension SpeedRampPresetLabel on SpeedRampPreset {
  String get label => switch (this) {
    SpeedRampPreset.impacto => 'Impacto',
    SpeedRampPreset.heroi => 'Heroi',
    SpeedRampPreset.bala => 'Bala',
    SpeedRampPreset.montagem => 'Montagem',
  };
}

/// Cria a curva fonte-tempo dos presets. O primeiro e o ultimo valor
/// permanecem iguais, portanto a rampa nao muda a duracao nem a sobra.
AnimatedDouble speedRampTrack(
  SpeedRampPreset preset,
  Duration timelineDuration,
  Duration sourceSpan,
) {
  final d = math.max(1, timelineDuration.inMicroseconds);
  final sourceSeconds = sourceSpan.inMicroseconds / 1000000.0;
  final points = switch (preset) {
    SpeedRampPreset.impacto => const <(double, double)>[
      (0, 0),
      (0.28, 0.16),
      (0.48, 0.58),
      (0.72, 0.82),
      (1, 1),
    ],
    SpeedRampPreset.heroi => const <(double, double)>[
      (0, 0),
      (0.25, 0.35),
      (0.5, 0.5),
      (0.75, 0.65),
      (1, 1),
    ],
    SpeedRampPreset.bala => const <(double, double)>[
      (0, 0),
      (0.25, 0.44),
      (0.5, 0.5),
      (0.75, 0.56),
      (1, 1),
    ],
    SpeedRampPreset.montagem => const <(double, double)>[
      (0, 0),
      (0.2, 0.08),
      (0.4, 0.38),
      (0.6, 0.52),
      (0.8, 0.9),
      (1, 1),
    ],
  };
  var track = AnimatedDouble(0);
  for (var i = 0; i < points.length; i++) {
    final p = points[i];
    track = track.withKeyframe(
      Duration(microseconds: (d * p.$1).round()),
      sourceSeconds * p.$2,
      i == points.length - 1 ? Easing.linear : Easing.easeInOut,
    );
  }
  return track;
}
