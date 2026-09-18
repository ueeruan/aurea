import 'dart:math' as math;

import 'keyframe.dart';

enum SpeedRampPreset {
  impacto,
  heroi,
  bala,
  montagem,
  lentoRapido,
  rapidoLento,
  soco,

  /// Rapido -> LENTO -> rapido: entra em disparada, freia em camera
  /// lenta no corpo do clipe e sai acelerando de novo. E o perfil dos
  /// microclipes de edit (lido quadro a quadro do video do dono, 15/09)
  /// — nenhuma das rampas acima tinha as DUAS pontas rapidas.
  flow,
}

enum FreezePlacement { insideClip, separateClip }

extension SpeedRampPresetLabel on SpeedRampPreset {
  String get label => switch (this) {
    SpeedRampPreset.impacto => 'Impacto',
    SpeedRampPreset.heroi => 'Heroi',
    SpeedRampPreset.bala => 'Bala',
    SpeedRampPreset.montagem => 'Montagem',
    SpeedRampPreset.lentoRapido => 'Lento → Rápido',
    SpeedRampPreset.rapidoLento => 'Rápido → Lento',
    SpeedRampPreset.soco => 'Soco',
    SpeedRampPreset.flow => 'Flow',
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
    // Rampa de subida: comeca em camera lenta e chega acelerado.
    SpeedRampPreset.lentoRapido => const <(double, double)>[
      (0, 0),
      (0.6, 0.25),
      (1, 1),
    ],
    // Rampa de descida: estoura no comeco e assenta em camera lenta.
    SpeedRampPreset.rapidoLento => const <(double, double)>[
      (0, 0),
      (0.4, 0.75),
      (1, 1),
    ],
    // Estouro no meio: lento, dispara, lento — o "punch" de transicao.
    SpeedRampPreset.soco => const <(double, double)>[
      (0, 0),
      (0.4, 0.18),
      (0.6, 0.82),
      (1, 1),
    ],
    // As duas pontas em disparada, o corpo em camera lenta (com optical
    // flow por cima, e o slow-mo liso dos edits).
    SpeedRampPreset.flow => const <(double, double)>[
      (0, 0),
      (0.14, 0.40),
      (0.30, 0.50),
      (0.70, 0.58),
      (0.86, 0.64),
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
