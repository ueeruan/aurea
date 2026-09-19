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
  }) : id = id ?? const Uuid().v4(),
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

  EffectPreset copyWith({String? name, List<String>? tags}) => EffectPreset(
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
        e.copyWith(
          params: {
            for (final entry in e.params.entries)
              entry.key: _normalizeTrack(
                entry.value,
                relative: _isRelative(e.type, entry.key),
                ref: ref,
                shift: -layerStart,
              ),
          },
        ),
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
  final factor =
      (stretchTo == null || preset.suggestedDuration.inMicroseconds == 0)
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
typedef PresetCompat = ({List<EffectInstance> effects, List<String> warnings});

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
                entry.value.loop,
              );
      } else {
        warnings.add(
          '"${entry.key}" nao existe mais em ${spec.name} e foi ignorado.',
        );
      }
    }
    for (final p in spec.params.entries) {
      kept.putIfAbsent(p.key, () {
        warnings.add(
          '"${p.value.label}" e novo em ${spec.name}: entrou no padrao.',
        );
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
  return AnimatedDouble(track.base * f, [
    for (final k in track.keyframes)
      Keyframe<double>(time: k.time + shift, value: k.value * f, ease: k.ease),
  ], track.loop);
}

AnimatedDouble _denormalizeTrack(
  AnimatedDouble track, {
  required bool relative,
  required double ref,
  required Duration shift,
  required double timeScale,
}) {
  final f = relative ? ref : 1.0;
  return AnimatedDouble(track.base * f, [
    for (final k in track.keyframes)
      Keyframe<double>(
        time:
            shift +
            Duration(microseconds: (k.time.inMicroseconds * timeScale).round()),
        value: k.value * f,
        ease: k.ease,
      ),
  ], track.loop);
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
    pos.add(
      Keyframe<Offset>(
        time: t,
        value: basePosition + sampleOffset(t),
        ease: Easing.linear,
      ),
    );
    if (sampleRotation != null) {
      rot.add(
        Keyframe<double>(
          time: t,
          value: baseRotation + sampleRotation(t),
          ease: Easing.linear,
        ),
      );
    }
    if (sampleScale != null) {
      scl.add(
        Keyframe<double>(
          time: t,
          value: baseScale * sampleScale(t),
          ease: Easing.linear,
        ),
      );
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
      EffectInstance(
        type: EffectType.hueSaturation,
        params: {
          'master_saturation': AnimatedDouble(14),
          'master_lightness': AnimatedDouble(-2),
        },
      ),
    ],
  ),
  EffectPreset(
    name: 'Sonho suave',
    category: 'Luz',
    builtIn: true,
    tags: ['glow', 'sonho'],
    effects: [
      EffectInstance(
        type: EffectType.deepGlow,
        params: {
          'raio': AnimatedDouble(110),
          'exposicao': AnimatedDouble(120),
          'limiar': AnimatedDouble(60),
          'suavidade': AnimatedDouble(30),
        },
      ),
      EffectInstance(
        type: EffectType.vignette,
        params: {
          'amount': AnimatedDouble(35),
          'angle_of_view': AnimatedDouble(45),
        },
      ),
    ],
  ),
  EffectPreset(
    name: 'Glitch de impacto',
    category: 'Glitch',
    builtIn: true,
    tags: ['glitch', 'edit'],
    suggestedDuration: const Duration(milliseconds: 600),
    effects: [
      EffectInstance(
        type: EffectType.glitchify,
        params: {
          'amount': AnimatedDouble(80)
              .withKeyframe(Duration.zero, 80)
              .withKeyframe(const Duration(milliseconds: 600), 0),
          'speed': AnimatedDouble(45),
          // A SEPARACAO DE CANAIS e o "rgb" do preset antigo: o numero
          // que faz a imagem estourar em vermelho e azul.
          'split_channel': AnimatedDouble(1),
          'channel_scale': AnimatedDouble(90),
          'seed': AnimatedDouble(11),
        },
      ),
    ],
  ),
  EffectPreset(
    name: 'Camera na mao',
    category: 'Distorcao',
    builtIn: true,
    tags: ['shake', 'tremor'],
    effects: [
      EffectInstance(
        type: EffectType.tremor,
        params: {
          // O NUMERO DE CADA EIXO E QUE MANDA, e nao um estilo: quem
          // quiser mais tremor mexe na Amplitude do eixo, nao numa
          // enumeracao de comportamento.
          'amplitude': AnimatedDouble(1),
          'frequency': AnimatedDouble(3.5),
          'x_rand_amp': AnimatedDouble(150),
          'y_rand_amp': AnimatedDouble(90),
          'tilt_rand_amp': AnimatedDouble(0.6),
          'motion_blur': AnimatedDouble(1),
          'mo_blur_length': AnimatedDouble(0.4),
          'seed': AnimatedDouble(4),
        },
      ),
    ],
  ),
  EffectPreset(
    name: 'Overlay de fogo',
    category: 'Luz',
    builtIn: true,
    tags: ['unmult', 'overlay', 'fogo'],
    effects: [
      EffectInstance(
        type: EffectType.brilho,
        params: {
          'raio': AnimatedDouble(70),
          'intensidade': AnimatedDouble(70),
          'limiar': AnimatedDouble(45),
          'tingimento': AnimatedDouble(70),
        },
        color: const Color(0xFFFF8A2B),
      ),
    ],
  ),
  // ---------------------------------------------------------------
  // COLORINGS PRONTOS (pesquisa 14/09/2026). Pilhas na ordem do pipeline
  // de cor: exposicao e temperatura primeiro, depois curvas e
  // equilibrio, saturacao, mapas em Soft Light, nitidez, brilho,
  // vinheta e grao por ultimo. Os numeros vem de tutoriais de
  // coloring (PSD, After Effects, Alight Motion) e dos presets do
  // FFmpeg; onde nao ha fonte, sao ponto de partida — cada efeito da
  // pilha continua editavel.
  // ---------------------------------------------------------------
  EffectPreset(
    name: 'CC HDR',
    category: 'Cor',
    builtIn: true,
    tags: ['coloring', 'cc', 'hdr'],
    effects: [
      EffectInstance(
        type: EffectType.hueSaturation,
        params: {'master_saturation': AnimatedDouble(45)},
      ),
      EffectInstance(
        type: EffectType.unsharpMask,
        params: {
          'amount': AnimatedDouble(0.6),
          'radius': AnimatedDouble(1.5),
        },
      ),
      EffectInstance(
        type: EffectType.brilho,
        params: {
          'limiar': AnimatedDouble(90),
          'raio': AnimatedDouble(60),
          'intensidade': AnimatedDouble(40),
        },
        color: const Color(0xFFFFFFFF),
      ),
      EffectInstance(
        type: EffectType.vignette,
        params: {
          'amount': AnimatedDouble(20),
          'angle_of_view': AnimatedDouble(45),
        },
        color: const Color(0xFF000000),
      ),
    ],
  ),
  EffectPreset(
    name: 'CC Azul teal',
    category: 'Cor',
    builtIn: true,
    tags: ['coloring', 'cc', 'teal', 'azul'],
    effects: [
      EffectInstance(
        type: EffectType.hueSaturation,
        params: {'master_saturation': AnimatedDouble(-12)},
      ),
    ],
  ),
  EffectPreset(
    name: 'CC Vintage quente',
    category: 'Cor',
    builtIn: true,
    tags: ['coloring', 'cc', 'vintage', 'quente'],
    effects: [
      EffectInstance(
        type: EffectType.hueSaturation,
        params: {'master_saturation': AnimatedDouble(-10)},
      ),
      EffectInstance(
        type: EffectType.levels,
        // O PRETO NAO DESCE MAIS ABAIXO QUE ISTO: e o que poe a nevoa
        // quente do vintage, no lugar do preto puro.
        params: {'output_black': AnimatedDouble(13)},
      ),
      EffectInstance(
        type: EffectType.vignette,
        params: {'amount': AnimatedDouble(25)},
        color: const Color(0xFF000000),
      ),
    ],
  ),
  EffectPreset(
    name: 'CC Escuro contrastado',
    category: 'Cor',
    builtIn: true,
    tags: ['coloring', 'cc', 'dark', 'contraste'],
    effects: [
      EffectInstance(
        type: EffectType.hueSaturation,
        params: {'master_saturation': AnimatedDouble(-20)},
      ),
      EffectInstance(
        type: EffectType.vignette,
        params: {'amount': AnimatedDouble(45)},
        color: const Color(0xFF000000),
      ),
      EffectInstance(
        type: EffectType.unsharpMask,
        params: {'amount': AnimatedDouble(0.7)},
      ),
    ],
  ),
  EffectPreset(
    name: 'CC Anime',
    category: 'Cor',
    builtIn: true,
    tags: ['coloring', 'cc', 'anime'],
    effects: [
      EffectInstance(
        type: EffectType.hueSaturation,
        params: {'master_saturation': AnimatedDouble(50)},
      ),
      EffectInstance(
        type: EffectType.brilho,
        params: {
          'limiar': AnimatedDouble(75),
          'raio': AnimatedDouble(30),
          'intensidade': AnimatedDouble(60),
        },
        color: const Color(0xFFFFFFFF),
      ),
    ],
  ),
  EffectPreset(
    name: 'CC PSD suave',
    category: 'Cor',
    builtIn: true,
    tags: ['coloring', 'cc', 'psd', 'suave'],
    effects: [
      EffectInstance(
        type: EffectType.hueSaturation,
        params: {
          'master_saturation': AnimatedDouble(52),
          'master_lightness': AnimatedDouble(6),
        },
      ),
      EffectInstance(
        type: EffectType.brightnessContrast,
        params: {'contrast': AnimatedDouble(33)},
      ),
    ],
  ),
];
