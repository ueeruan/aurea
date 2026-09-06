import 'dart:ui';

import 'package:uuid/uuid.dart';

import 'effect.dart';
import 'keyframe.dart';

/// SISTEMA DE PRESETS (spec catalogo-de-efeitos §2). Multiplica o valor
/// de todo o resto, por isso vem antes do catalogo.
///
/// Dois pontos que quase todo mundo erra e que aqui sao lei:
///  1. os keyframes sao gravados RELATIVOS ao inicio do preset — um
///     preset aplicado aos 12 s tem que funcionar;
///  2. ponto, distancia e raio sao NORMALIZADOS pelo tamanho da camada
///     ao salvar e desnormalizados ao aplicar — senao um preset feito em
///     1080x1920 sai errado numa camada 1920x1080.
/// Cor, angulo e switch nao normalizam.
class EffectPreset {
  EffectPreset({
    String? id,
    required this.name,
    required this.effects,
    this.tags = const [],
    this.category = 'Meus',
    this.suggestedDuration = const Duration(seconds: 2),
    this.builtIn = false,
    DateTime? createdAt,
    this.author = '',
  })  : id = id ?? const Uuid().v4(),
        createdAt = createdAt ?? DateTime(2026);

  final String id;
  final String name;
  final List<EffectInstance> effects;
  final List<String> tags;
  final String category;

  /// Duracao com que o preset foi criado — a base para esticar.
  final Duration suggestedDuration;

  /// Preset de fabrica: somente leitura.
  final bool builtIn;
  final DateTime createdAt;
  final String author;

  EffectPreset copyWith({String? name, List<String>? tags}) =>
      EffectPreset(
        id: id,
        name: name ?? this.name,
        effects: effects,
        tags: tags ?? this.tags,
        category: category,
        suggestedDuration: suggestedDuration,
        builtIn: builtIn,
        createdAt: createdAt,
        author: author,
      );
}

/// SALVAR (§2.4): a pilha vira preset com os keyframes deslocados para o
/// inicio e os parametros relativos normalizados pelo tamanho da camada.
EffectPreset saveEffectPreset({
  required String name,
  required List<EffectInstance> effects,
  required Duration layerStart,
  required Duration layerDuration,
  required Size layerSize,
  List<String> tags = const [],
  String category = 'Meus',
}) {
  final ref = _refSize(layerSize);
  return EffectPreset(
    name: name,
    tags: tags,
    category: category,
    suggestedDuration: layerDuration,
    effects: [
      for (final e in effects)
        e.copyWith(params: {
          for (final entry in e.params.entries)
            entry.key: _normalizeTrack(
              entry.value,
              relative: _isRelative(e.type, entry.key),
              ref: ref,
              shift: -layerStart,
            ),
        }),
    ],
  );
}

/// APLICAR (§2.4): ancora no cabecote, opcionalmente ESTICA para a
/// duracao pedida (reescalando os intervalos SEM alterar valores) e
/// desnormaliza os parametros relativos pelo tamanho do destino.
List<EffectInstance> applyEffectPreset(
  EffectPreset preset, {
  required Duration at,
  required Size targetSize,
  Duration? stretchTo,
}) {
  final ref = _refSize(targetSize);
  final factor = (stretchTo == null ||
          preset.suggestedDuration.inMicroseconds == 0)
      ? 1.0
      : stretchTo.inMicroseconds / preset.suggestedDuration.inMicroseconds;
  return [
    for (final e in preset.effects)
      EffectInstance(
        type: e.type,
        color: e.color,
        enabled: e.enabled,
        params: {
          for (final entry in e.params.entries)
            entry.key: _denormalizeTrack(
              entry.value,
              relative: _isRelative(e.type, entry.key),
              ref: ref,
              shift: at,
              timeScale: factor,
            ),
        },
      ),
  ];
}

/// TOLERANCIA DE VERSAO (§2.5): parametros que nao existem mais no
/// efeito sao descartados com aviso, nunca falham em silencio.
typedef PresetCompat = ({
  List<EffectInstance> effects,
  List<String> warnings,
});

PresetCompat reconcilePreset(EffectPreset preset) {
  final warnings = <String>[];
  final out = <EffectInstance>[];
  for (final e in preset.effects) {
    final spec = effectSpecs[e.type];
    if (spec == null) {
      warnings.add('O efeito ${e.type.name} nao existe mais.');
      continue;
    }
    final kept = <String, AnimatedDouble>{};
    for (final entry in e.params.entries) {
      // PARAMETRO RENOMEADO nao e parametro perdido: a chave antiga vira
      // a nova (e, se mudou de unidade, o numero e convertido junto).
      final chave = resolveParamKey(e.type, entry.key);
      if (spec.params.containsKey(chave)) {
        kept[chave] = chave == entry.key
            ? entry.value
            : AnimatedDouble(
                migrateParamValue(e.type, chave, entry.value.base, 0),
                [
                  for (final k in entry.value.keyframes)
                    Keyframe<double>(
                      time: k.time,
                      value: migrateParamValue(e.type, chave, k.value, 0),
                      ease: k.ease,
                    ),
                ],
                entry.value.loop);
      } else {
        warnings.add(
            '"${entry.key}" nao existe mais em ${spec.name} e foi ignorado.');
      }
    }
    for (final p in spec.params.entries) {
      kept.putIfAbsent(p.key, () {
        warnings.add(
            '"${p.value.label}" e novo em ${spec.name}: entrou no padrao.');
        return AnimatedDouble(p.value.initial);
      });
    }
    out.add(e.copyWith(params: kept));
  }
  return (effects: out, warnings: warnings);
}

bool _isRelative(EffectType type, String key) =>
    effectSpecs[type]?.params[key]?.relative ?? false;

/// Referencia de normalizacao: o menor lado, que e o que preserva
/// proporcao entre retrato e paisagem.
double _refSize(Size s) {
  final v = s.shortestSide;
  return v <= 1 ? 1 : v;
}

AnimatedDouble _normalizeTrack(
  AnimatedDouble track, {
  required bool relative,
  required double ref,
  required Duration shift,
}) {
  final f = relative ? 1 / ref : 1.0;
  return AnimatedDouble(
    track.base * f,
    [
      for (final k in track.keyframes)
        Keyframe<double>(
            time: k.time + shift, value: k.value * f, ease: k.ease),
    ],
    track.loop,
  );
}

AnimatedDouble _denormalizeTrack(
  AnimatedDouble track, {
  required bool relative,
  required double ref,
  required Duration shift,
  required double timeScale,
}) {
  final f = relative ? ref : 1.0;
  return AnimatedDouble(
    track.base * f,
    [
      for (final k in track.keyframes)
        Keyframe<double>(
          time: shift +
              Duration(
                  microseconds:
                      (k.time.inMicroseconds * timeScale).round()),
          value: k.value * f,
          ease: k.ease,
        ),
    ],
    track.loop,
  );
}

/// ASSAR EM KEYFRAMES (PR-C3): converte o movimento PROCEDURAL do
/// efeito em keyframes reais na camada, um por frame. E o que impede o
/// usuario de ficar preso na logica do efeito: aplica o comportamento,
/// assa, e depois ajusta quadro a quadro na mao.
///
/// Devolve as trilhas de posicao/rotacao/escala prontas para substituir
/// as da camada; o efeito procedural e removido depois.
typedef BakedTracks = ({
  AnimatedOffset position,
  AnimatedDouble rotation,
  AnimatedDouble scale,
});

BakedTracks bakeProceduralMotion({
  required EffectInstance effect,
  required Duration duration,
  required int fps,
  required Offset basePosition,
  required double baseRotation,
  required double baseScale,
  required Offset Function(Duration t) sampleOffset,
  double Function(Duration t)? sampleRotation,
  double Function(Duration t)? sampleScale,
}) {
  final rate = fps < 1 ? 30 : fps;
  final frameUs = 1000000 ~/ rate;
  // Conta os frames pela TAXA, nao pelo frameUs truncado — senao um
  // segundo a 30 fps rende 32 amostras em vez de 31.
  final frames = (duration.inMicroseconds * rate / 1000000).round();
  final pos = <Keyframe<Offset>>[];
  final rot = <Keyframe<double>>[];
  final scl = <Keyframe<double>>[];

  for (var i = 0; i <= frames; i++) {
    final t = Duration(microseconds: i * frameUs);
    pos.add(Keyframe<Offset>(
      time: t,
      value: basePosition + sampleOffset(t),
      ease: Easing.linear,
    ));
    if (sampleRotation != null) {
      rot.add(Keyframe<double>(
          time: t,
          value: baseRotation + sampleRotation(t),
          ease: Easing.linear));
    }
    if (sampleScale != null) {
      scl.add(Keyframe<double>(
          time: t,
          value: baseScale * sampleScale(t),
          ease: Easing.linear));
    }
  }

  return (
    position: AnimatedOffset(basePosition, pos),
    rotation: rot.isEmpty
        ? AnimatedDouble(baseRotation)
        : AnimatedDouble(baseRotation, rot),
    scale: scl.isEmpty
        ? AnimatedDouble(baseScale)
        : AnimatedDouble(baseScale, scl),
  );
}

/// BIBLIOTECA DE FABRICA (§2.4): presets prontos por categoria, somente
/// leitura.
List<EffectPreset> factoryPresets() => [
      EffectPreset(
        name: 'Cor de filme',
        category: 'Cor',
        builtIn: true,
        tags: ['cor', 'cinema'],
        effects: [
          EffectInstance(type: EffectType.curves, params: {
            'contraste': AnimatedDouble(0.18),
            'brilho': AnimatedDouble(-0.04),
            'sombras': AnimatedDouble(0.12),
            'altas': AnimatedDouble(0.05),
          }),
          EffectInstance(type: EffectType.vibrance, params: {
            'vibracao': AnimatedDouble(0.25),
            'saturacao': AnimatedDouble(-0.08),
            'protecaoPele': AnimatedDouble(0.8),
          }),
          EffectInstance(type: EffectType.filmGrain, params: {
            'intensidade': AnimatedDouble(0.18),
            'tamanho': AnimatedDouble(1.4),
            'semente': AnimatedDouble(7),
          }),
        ],
      ),
      EffectPreset(
        name: 'Sonho suave',
        category: 'Luz',
        builtIn: true,
        tags: ['glow', 'sonho'],
        effects: [
          EffectInstance(type: EffectType.glowVol, params: {
            'radius': AnimatedDouble(0.06),
            'exposure': AnimatedDouble(0.9),
            'red_radius_multiplier': AnimatedDouble(1.15),
            'threshold': AnimatedDouble(0.7),
          }),
          EffectInstance(type: EffectType.vignette, params: {
            'quantidade': AnimatedDouble(0.35),
            'raio': AnimatedDouble(0.9),
          }),
        ],
      ),
      EffectPreset(
        name: 'Glitch de impacto',
        category: 'Glitch',
        builtIn: true,
        tags: ['glitch', 'edit'],
        suggestedDuration: const Duration(milliseconds: 600),
        effects: [
          EffectInstance(type: EffectType.glitch, params: {
            'quantidade': AnimatedDouble(1)
                .withKeyframe(Duration.zero, 1.6)
                .withKeyframe(const Duration(milliseconds: 600), 0),
            'velocidade': AnimatedDouble(6),
            'rgb': AnimatedDouble(0.8),
            'semente': AnimatedDouble(11),
          }),
          EffectInstance(type: EffectType.digitalDamage, params: {
            'blocos': AnimatedDouble(10),
            'intervalo': AnimatedDouble(0.08),
            'semente': AnimatedDouble(3),
          }),
        ],
      ),
      EffectPreset(
        name: 'Camera na mao',
        category: 'Distorcao',
        builtIn: true,
        tags: ['shake', 'tremor'],
        effects: [
          EffectInstance(type: EffectType.tremor, params: {
            'amplitude': AnimatedDouble(18),
            'frequency': AnimatedDouble(3.5),
            'style': AnimatedDouble(0),
            'tilt_random_amplitude': AnimatedDouble(0.05),
            'seed': AnimatedDouble(4),
          }),
        ],
      ),
      EffectPreset(
        name: 'Overlay de fogo',
        category: 'Luz',
        builtIn: true,
        tags: ['unmult', 'overlay', 'fogo'],
        effects: [
          EffectInstance(type: EffectType.unmult, params: {
            'limiar': AnimatedDouble(0.05),
            'suavidade': AnimatedDouble(0.6),
          }),
          EffectInstance(type: EffectType.lightGlow, params: {
            'diffusion': AnimatedDouble(0.3),
            'intensity': AnimatedDouble(0.6),
          }, color: const Color(0xFFFF8A2B)),
        ],
      ),
      EffectPreset(
        name: 'Zoom de soco',
        category: 'Distorcao',
        builtIn: true,
        tags: ['zoom', 'impacto'],
        suggestedDuration: const Duration(milliseconds: 400),
        effects: [
          EffectInstance(type: EffectType.zoomWarp, params: {
            'quantidade': AnimatedDouble(0)
                .withKeyframe(Duration.zero, 0.35)
                .withKeyframe(const Duration(milliseconds: 400), 0),
            'rastro': AnimatedDouble(0.5),
            'amostras': AnimatedDouble(6),
          }),
        ],
      ),
    ];
