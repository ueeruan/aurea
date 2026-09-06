import 'dart:math' as math;

import 'package:uuid/uuid.dart';

import 'keyframe.dart';
import 'text_animator.dart';

/// ANIMADOR DE TEXTO — modelo do Alight Motion, motor do After Effects.
///
/// O animador antigo pedia que a pessoa montasse seletor, faixa, offset e
/// propriedade na mao para conseguir um "aparecer letra por letra". Isso e
/// o modelo do AE, e ele continua existindo aqui embaixo (TextAnimator +
/// RangeSelector) para quem quiser.
///
/// Por cima dele vai o modelo do AM: voce escolhe UMA animacao de um
/// catalogo, diz se ela e de ENTRADA, ENFASE ou SAIDA, e mexe em seis
/// controles — unidade, inicio, duracao, atraso, ordem e curva. O resto
/// e compilado para animadores de verdade.

// ------------------------------------------------------------ enums

/// Onde a animacao age: entrando, chamando atencao, ou saindo.
enum TextAnimSlot { entrada, enfase, saida }

String textAnimSlotLabel(TextAnimSlot s) => switch (s) {
      TextAnimSlot.entrada => 'Entrada',
      TextAnimSlot.enfase => 'Enfase',
      TextAnimSlot.saida => 'Saida',
    };

/// O que conta como "uma unidade" da animacao.
enum TextAnimUnit { character, charactersNoSpaces, word, line, all }

String textAnimUnitLabel(TextAnimUnit u) => switch (u) {
      TextAnimUnit.character => 'Letras',
      TextAnimUnit.charactersNoSpaces => 'Letras (sem espaco)',
      TextAnimUnit.word => 'Palavras',
      TextAnimUnit.line => 'Linhas',
      TextAnimUnit.all => 'Tudo junto',
    };

SelectorBasedOn basedOnFor(TextAnimUnit u) => switch (u) {
      TextAnimUnit.character || TextAnimUnit.all => SelectorBasedOn.characters,
      TextAnimUnit.charactersNoSpaces => SelectorBasedOn.charactersNoSpaces,
      TextAnimUnit.word => SelectorBasedOn.words,
      TextAnimUnit.line => SelectorBasedOn.lines,
    };

// -------------------------------------------------------- o catalogo

/// Um controle exposto por uma animacao do catalogo.
class TextAnimParam {
  const TextAnimParam({
    required this.key,
    required this.label,
    required this.initial,
    required this.min,
    required this.max,
    this.suffix = '',
  });

  final String key;
  final String label;
  final double initial;
  final double min;
  final double max;
  final String suffix;
}

/// Uma animacao do catalogo: o que ela mexe no extremo da cobertura.
class TextAnimSpec {
  const TextAnimSpec({
    required this.id,
    required this.label,
    required this.slots,
    required this.build,
    this.params = const [],
    this.unit = TextAnimUnit.character,
    this.ease = TextAnimEase.desacelerar,
    this.duration = const Duration(milliseconds: 600),
    this.stagger = const Duration(milliseconds: 55),
    this.loop = false,
    this.loopShape = LoopShape.sine,
    this.overshoot = false,
  });

  final String id;
  final String label;

  /// Em quais posicoes ela faz sentido.
  final List<TextAnimSlot> slots;

  /// Valor de cada propriedade quando a cobertura e 1 — ou seja, o
  /// estado "animado" (de onde a entrada vem, para onde a saida vai).
  final Map<TextAnimProp, double> Function(Map<String, double> p) build;

  final List<TextAnimParam> params;
  final TextAnimUnit unit;
  final TextAnimEase ease;
  final Duration duration;
  final Duration stagger;
  final bool loop;
  final LoopShape loopShape;

  /// Deixa a cobertura passar de 1 (necessario para mola).
  final bool overshoot;

  Map<String, double> get defaults => {
        for (final p in params) p.key: p.initial,
      };
}

const _dist = TextAnimParam(
    key: 'distancia', label: 'Distancia', initial: 90, min: 0, max: 600,
    suffix: 'px');
const _blur = TextAnimParam(
    key: 'desfoque', label: 'Desfoque', initial: 22, min: 0, max: 80,
    suffix: 'px');
const _giro = TextAnimParam(
    key: 'giro', label: 'Giro', initial: 90, min: -720, max: 720,
    suffix: '°');
const _escala = TextAnimParam(
    key: 'escala', label: 'Escala', initial: 0, min: 0, max: 400,
    suffix: '%');
const _forca = TextAnimParam(
    key: 'forca', label: 'Forca', initial: 100, min: 0, max: 300,
    suffix: '%');

/// O CATALOGO. Cobre o que o Alight Motion traz de fabrica e os presets
/// de texto que voce ja usa no After Effects.
final List<TextAnimSpec> textAnimCatalog = [
  // ---------------------------------------------- entrada / saida
  TextAnimSpec(
    id: 'fade',
    label: 'Aparecer',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    build: (p) => {TextAnimProp.opacity: 0},
  ),
  TextAnimSpec(
    id: 'slideUp',
    label: 'Subir',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [_dist],
    build: (p) => {
      TextAnimProp.positionY: p['distancia'] ?? 90,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'slideDown',
    label: 'Descer',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [_dist],
    build: (p) => {
      TextAnimProp.positionY: -(p['distancia'] ?? 90),
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'slideLeft',
    label: 'Vir da direita',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [_dist],
    build: (p) => {
      TextAnimProp.positionX: p['distancia'] ?? 90,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'slideRight',
    label: 'Vir da esquerda',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [_dist],
    build: (p) => {
      TextAnimProp.positionX: -(p['distancia'] ?? 90),
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'wordSlide',
    label: 'Palavra deslizando',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    unit: TextAnimUnit.word,
    stagger: const Duration(milliseconds: 110),
    params: const [_dist],
    build: (p) => {
      TextAnimProp.positionX: -(p['distancia'] ?? 90),
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'scaleUp',
    label: 'Crescer',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [_escala],
    build: (p) => {
      TextAnimProp.scale: p['escala'] ?? 0,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'scaleDown',
    label: 'Encolher',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [
      TextAnimParam(
          key: 'escala', label: 'Escala', initial: 220, min: 100, max: 500,
          suffix: '%')
    ],
    build: (p) => {
      TextAnimProp.scale: p['escala'] ?? 220,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'pop',
    label: 'Estourar',
    slots: const [TextAnimSlot.entrada],
    ease: TextAnimEase.mola,
    overshoot: true,
    params: const [_escala],
    build: (p) => {
      TextAnimProp.scale: p['escala'] ?? 0,
      TextAnimProp.opacity: 0,
    },
  ),
  // A animacao da sua extensao: entrada por letra com mola.
  TextAnimSpec(
    id: 'bounceLetter',
    label: 'Quicar por letra',
    slots: const [TextAnimSlot.entrada],
    ease: TextAnimEase.mola,
    overshoot: true,
    duration: const Duration(milliseconds: 900),
    stagger: const Duration(milliseconds: 45),
    params: const [_dist],
    build: (p) => {
      TextAnimProp.positionY: p['distancia'] ?? 90,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'bounceWord',
    label: 'Quicar por palavra',
    slots: const [TextAnimSlot.entrada],
    unit: TextAnimUnit.word,
    ease: TextAnimEase.mola,
    overshoot: true,
    duration: const Duration(milliseconds: 900),
    stagger: const Duration(milliseconds: 120),
    params: const [_dist],
    build: (p) => {
      TextAnimProp.positionY: p['distancia'] ?? 90,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'dropIn',
    label: 'Cair',
    slots: const [TextAnimSlot.entrada],
    ease: TextAnimEase.quicar,
    duration: const Duration(milliseconds: 850),
    params: const [_dist],
    build: (p) => {
      TextAnimProp.positionY: -(p['distancia'] ?? 200),
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'blurIn',
    label: 'Aparecer em desfoque',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [_blur],
    build: (p) => {
      TextAnimProp.blur: p['desfoque'] ?? 22,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'blurWord',
    label: 'Desfoque por palavra',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    unit: TextAnimUnit.word,
    stagger: const Duration(milliseconds: 110),
    params: const [_blur],
    build: (p) => {
      TextAnimProp.blur: p['desfoque'] ?? 22,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'blurUp',
    label: 'Desfoque subindo',
    slots: const [TextAnimSlot.entrada],
    params: const [_blur, _dist],
    build: (p) => {
      TextAnimProp.blur: p['desfoque'] ?? 22,
      TextAnimProp.positionY: p['distancia'] ?? 90,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'blurDown',
    label: 'Desfoque descendo',
    slots: const [TextAnimSlot.saida],
    params: const [_blur, _dist],
    build: (p) => {
      TextAnimProp.blur: p['desfoque'] ?? 22,
      TextAnimProp.positionY: -(p['distancia'] ?? 90),
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'typewriter',
    label: 'Maquina de escrever',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    ease: TextAnimEase.linear,
    duration: Duration.zero,
    stagger: const Duration(milliseconds: 60),
    build: (p) => {TextAnimProp.opacity: 0},
  ),
  TextAnimSpec(
    id: 'spin',
    label: 'Girar',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [
      TextAnimParam(
          key: 'giro', label: 'Giro', initial: 180, min: -720, max: 720,
          suffix: '°')
    ],
    build: (p) => {
      TextAnimProp.rotation: p['giro'] ?? 180,
      TextAnimProp.scale: 0,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'tumble',
    label: 'Tombar',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [_giro],
    build: (p) => {
      TextAnimProp.rotation: -(p['giro'] ?? 90),
      TextAnimProp.positionY: 40,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'flip',
    label: 'Virar',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    build: (p) => {
      TextAnimProp.scaleX: 0,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'skewIn',
    label: 'Inclinar',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [
      TextAnimParam(
          key: 'giro', label: 'Inclinacao', initial: 45, min: -80, max: 80,
          suffix: '°'),
      _dist,
    ],
    build: (p) => {
      TextAnimProp.skew: p['giro'] ?? 45,
      TextAnimProp.positionX: -(p['distancia'] ?? 90),
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'trackingIn',
    label: 'Abrir espacamento',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    unit: TextAnimUnit.all,
    duration: const Duration(milliseconds: 900),
    params: const [
      TextAnimParam(
          key: 'distancia', label: 'Espaco', initial: 40, min: -60, max: 200,
          suffix: 'px')
    ],
    build: (p) => {
      TextAnimProp.tracking: p['distancia'] ?? 40,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'slowFade',
    label: 'Aparecer devagar',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    unit: TextAnimUnit.all,
    ease: TextAnimEase.suave,
    duration: const Duration(milliseconds: 1400),
    build: (p) => {TextAnimProp.opacity: 0},
  ),
  TextAnimSpec(
    id: 'glitchIn',
    label: 'Glitch',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    ease: TextAnimEase.linear,
    duration: const Duration(milliseconds: 420),
    stagger: const Duration(milliseconds: 30),
    params: const [_dist],
    build: (p) => {
      TextAnimProp.positionX: p['distancia'] ?? 40,
      TextAnimProp.hue: 120,
      TextAnimProp.opacity: 0,
    },
  ),
  TextAnimSpec(
    id: 'colorIn',
    label: 'Entrar em cor',
    slots: const [TextAnimSlot.entrada, TextAnimSlot.saida],
    params: const [
      TextAnimParam(
          key: 'matiz', label: 'Matiz', initial: 180, min: -180, max: 180,
          suffix: '°')
    ],
    build: (p) => {
      TextAnimProp.hue: p['matiz'] ?? 180,
      TextAnimProp.saturation: 0,
    },
  ),

  // ------------------------------------------------------- enfase
  TextAnimSpec(
    id: 'wave',
    label: 'Onda',
    slots: const [TextAnimSlot.enfase],
    loop: true,
    ease: TextAnimEase.linear,
    duration: const Duration(milliseconds: 1200),
    stagger: const Duration(milliseconds: 90),
    params: const [
      TextAnimParam(
          key: 'distancia', label: 'Altura', initial: 22, min: 0, max: 200,
          suffix: 'px')
    ],
    build: (p) => {TextAnimProp.positionY: -(p['distancia'] ?? 22)},
  ),
  TextAnimSpec(
    id: 'float',
    label: 'Flutuar',
    slots: const [TextAnimSlot.enfase],
    loop: true,
    unit: TextAnimUnit.all,
    duration: const Duration(milliseconds: 2600),
    stagger: Duration.zero,
    params: const [
      TextAnimParam(
          key: 'distancia', label: 'Altura', initial: 14, min: 0, max: 120,
          suffix: 'px')
    ],
    build: (p) => {TextAnimProp.positionY: -(p['distancia'] ?? 14)},
  ),
  TextAnimSpec(
    id: 'pulse',
    label: 'Pulsar',
    slots: const [TextAnimSlot.enfase],
    loop: true,
    unit: TextAnimUnit.all,
    duration: const Duration(milliseconds: 900),
    stagger: Duration.zero,
    params: const [
      TextAnimParam(
          key: 'escala', label: 'Escala', initial: 118, min: 50, max: 250,
          suffix: '%')
    ],
    build: (p) => {TextAnimProp.scale: p['escala'] ?? 118},
  ),
  TextAnimSpec(
    id: 'breathe',
    label: 'Respirar',
    slots: const [TextAnimSlot.enfase],
    loop: true,
    unit: TextAnimUnit.all,
    duration: const Duration(milliseconds: 2200),
    stagger: Duration.zero,
    build: (p) => {
      TextAnimProp.scale: 108,
      TextAnimProp.brightness: 130,
    },
  ),
  TextAnimSpec(
    id: 'shake',
    label: 'Tremer',
    slots: const [TextAnimSlot.enfase],
    loop: true,
    loopShape: LoopShape.noise,
    duration: const Duration(milliseconds: 240),
    stagger: const Duration(milliseconds: 17),
    params: const [_forca],
    build: (p) => {
      TextAnimProp.positionX: 10 * (p['forca'] ?? 100) / 100,
      TextAnimProp.positionY: -8 * (p['forca'] ?? 100) / 100,
    },
  ),
  TextAnimSpec(
    id: 'jitter',
    label: 'Tremilique',
    slots: const [TextAnimSlot.enfase],
    loop: true,
    loopShape: LoopShape.noise,
    duration: const Duration(milliseconds: 150),
    stagger: const Duration(milliseconds: 11),
    params: const [_forca],
    build: (p) => {
      TextAnimProp.rotation: 7 * (p['forca'] ?? 100) / 100,
      TextAnimProp.positionY: -5 * (p['forca'] ?? 100) / 100,
      TextAnimProp.scale: 100 + 6 * (p['forca'] ?? 100) / 100,
    },
  ),
  TextAnimSpec(
    id: 'flicker',
    label: 'Piscar',
    slots: const [TextAnimSlot.enfase],
    loop: true,
    loopShape: LoopShape.noise,
    duration: const Duration(milliseconds: 320),
    stagger: const Duration(milliseconds: 23),
    build: (p) => {TextAnimProp.opacity: 0},
  ),
  TextAnimSpec(
    id: 'blink',
    label: 'Blink',
    slots: const [TextAnimSlot.enfase],
    loop: true,
    loopShape: LoopShape.pulse,
    unit: TextAnimUnit.all,
    duration: const Duration(milliseconds: 700),
    stagger: Duration.zero,
    build: (p) => {TextAnimProp.opacity: 0},
  ),
  TextAnimSpec(
    id: 'rainbow',
    label: 'Arco-iris',
    slots: const [TextAnimSlot.enfase],
    loop: true,
    loopShape: LoopShape.triangle,
    duration: const Duration(milliseconds: 2000),
    stagger: const Duration(milliseconds: 70),
    build: (p) => {TextAnimProp.hue: 180},
  ),
  TextAnimSpec(
    id: 'viral',
    label: 'Ambientacao viral',
    slots: const [TextAnimSlot.enfase],
    loop: true,
    duration: const Duration(milliseconds: 420),
    stagger: const Duration(milliseconds: 26),
    params: const [_forca],
    build: (p) => {
      TextAnimProp.scale: 100 + 14 * (p['forca'] ?? 100) / 100,
      TextAnimProp.positionY: -9 * (p['forca'] ?? 100) / 100,
      TextAnimProp.brightness: 100 + 25 * (p['forca'] ?? 100) / 100,
    },
  ),
];

TextAnimSpec? textAnimSpecById(String id) {
  for (final s in textAnimCatalog) {
    if (s.id == id) return s;
  }
  return null;
}

List<TextAnimSpec> textAnimsForSlot(TextAnimSlot slot) =>
    [for (final s in textAnimCatalog) if (s.slots.contains(slot)) s];

// ---------------------------------------------------- a instancia

/// Uma animacao aplicada a uma camada de texto. Seis controles, como no
/// Alight Motion: unidade, inicio, duracao, atraso, ordem e curva.
class TextAnim {
  TextAnim({
    String? id,
    required this.specId,
    required this.slot,
    TextAnimUnit? unit,
    this.start = Duration.zero,
    Duration? duration,
    Duration? stagger,
    this.order = TextAnimOrder.forward,
    TextAnimEase? ease,
    this.seed = 1,
    this.enabled = true,
    this.amplitude = 1,
    this.frequency = 1.8,
    this.decay = 5,
    Map<String, double>? params,
  })  : id = id ?? const Uuid().v4(),
        unit = unit ?? textAnimSpecById(specId)?.unit ?? TextAnimUnit.character,
        duration = duration ??
            textAnimSpecById(specId)?.duration ??
            const Duration(milliseconds: 600),
        stagger = stagger ??
            textAnimSpecById(specId)?.stagger ??
            const Duration(milliseconds: 55),
        ease = ease ??
            textAnimSpecById(specId)?.ease ??
            TextAnimEase.desacelerar,
        params = Map.unmodifiable(
            params ?? textAnimSpecById(specId)?.defaults ?? const {});

  final String id;
  final String specId;
  final TextAnimSlot slot;
  final TextAnimUnit unit;

  /// Deslocamento em relacao ao inicio da camada (saida conta do fim).
  final Duration start;
  final Duration duration;
  final Duration stagger;
  final TextAnimOrder order;
  final TextAnimEase ease;
  final int seed;
  final bool enabled;

  final double amplitude;
  final double frequency;
  final double decay;

  final Map<String, double> params;

  TextAnimSpec? get spec => textAnimSpecById(specId);

  String get label => spec?.label ?? specId;

  /// Quanto tempo a animacao inteira leva com [n] unidades.
  Duration totalFor(int n) =>
      duration + stagger * math.max(0, n - 1);

  TextAnim copyWith({
    String? specId,
    TextAnimSlot? slot,
    TextAnimUnit? unit,
    Duration? start,
    Duration? duration,
    Duration? stagger,
    TextAnimOrder? order,
    TextAnimEase? ease,
    int? seed,
    bool? enabled,
    double? amplitude,
    double? frequency,
    double? decay,
    Map<String, double>? params,
  }) =>
      TextAnim(
        id: id,
        specId: specId ?? this.specId,
        slot: slot ?? this.slot,
        unit: unit ?? this.unit,
        start: start ?? this.start,
        duration: duration ?? this.duration,
        stagger: stagger ?? this.stagger,
        order: order ?? this.order,
        ease: ease ?? this.ease,
        seed: seed ?? this.seed,
        enabled: enabled ?? this.enabled,
        amplitude: amplitude ?? this.amplitude,
        frequency: frequency ?? this.frequency,
        decay: decay ?? this.decay,
        params: params ?? this.params,
      );
}

// ---------------------------------------------------- compilacao

/// Compila uma animacao do catalogo para um ANIMADOR de verdade — o
/// mesmo tipo que o modo avancado (AE) edita na mao. Nada de caixa-preta:
/// o resultado e inspecionavel e continua passando pelo motor de sempre.
TextAnimator compileTextAnim(
  TextAnim anim, {
  required Duration layerDuration,
  int unitCount = 1,
}) {
  final spec = anim.spec;
  final values = spec?.build(anim.params) ?? const <TextAnimProp, double>{};

  // SAIDA conta do fim da camada para tras: a ultima unidade termina de
  // sair exatamente quando a camada acaba.
  var start = anim.start;
  if (anim.slot == TextAnimSlot.saida) {
    final total = anim.totalFor(unitCount);
    final from = layerDuration - total - anim.start;
    start = from.isNegative ? Duration.zero : from;
  }

  final selector = StaggerSelector(
    basedOn: basedOnFor(anim.unit),
    start: start,
    duration: anim.duration,
    stagger: anim.unit == TextAnimUnit.all ? Duration.zero : anim.stagger,
    order: anim.order,
    seed: anim.seed,
    ease: anim.ease,
    amplitude: anim.amplitude,
    frequency: anim.frequency,
    decay: anim.decay,
    // Entrada nasce coberta e caminha para o neutro; saida faz o inverso.
    startCovered: anim.slot == TextAnimSlot.entrada,
    loop: spec?.loop ?? false,
    loopShape: spec?.loopShape ?? LoopShape.sine,
  );

  return TextAnimator(
    name: anim.label,
    enabled: anim.enabled,
    allowOvershoot:
        (spec?.overshoot ?? false) || anim.ease == TextAnimEase.mola,
    selectors: [selector],
    properties: [
      for (final e in values.entries)
        AnimatorProperty(type: e.key, value: AnimatedDouble(e.value)),
    ],
  );
}

/// Compila a lista inteira, na ordem entrada -> enfase -> saida.
List<TextAnimator> compileTextAnims(
  List<TextAnim> anims, {
  required Duration layerDuration,
  int unitCount = 1,
}) {
  const rank = {
    TextAnimSlot.entrada: 0,
    TextAnimSlot.enfase: 1,
    TextAnimSlot.saida: 2,
  };
  final sorted = [...anims]
    ..sort((a, b) => rank[a.slot]!.compareTo(rank[b.slot]!));
  return [
    for (final a in sorted)
      compileTextAnim(a,
          layerDuration: layerDuration, unitCount: unitCount),
  ];
}
