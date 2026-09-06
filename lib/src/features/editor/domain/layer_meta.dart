import 'dart:math' as math;
import 'dart:ui';

import 'keyframe.dart';

/// Camada de OFICIO (spec motion-graphics-pro): tudo que se usa o dia
/// inteiro e nao muda o motor. Vive ao LADO da camada, num mapa do
/// projeto, para nao inchar as 11 subclasses de Layer.

// ---------------------------------------------------------- bloco 8

/// Rotulo colorido (PR-X26): cor + nome editavel.
class LayerLabel {
  const LayerLabel({required this.color, this.name = ''});

  final Color color;
  final String name;

  LayerLabel copyWith({Color? color, String? name}) =>
      LayerLabel(color: color ?? this.color, name: name ?? this.name);

  static const palette = <LayerLabel>[
    LayerLabel(color: Color(0xFFE85B81), name: 'Rosa'),
    LayerLabel(color: Color(0xFFFFB020), name: 'Ambar'),
    LayerLabel(color: Color(0xFFB8FF3D), name: 'Lima'),
    LayerLabel(color: Color(0xFF2BE3A0), name: 'Verde'),
    LayerLabel(color: Color(0xFF35C4E7), name: 'Ciano'),
    LayerLabel(color: Color(0xFF7C62FF), name: 'Violeta'),
  ];
}

// ---------------------------------------------------------- bloco 3

/// Estilos de camada (PR-X10): aplicam DEPOIS do transform e acompanham
/// a forma da camada — diferente de efeito.
class LayerStyles {
  const LayerStyles({
    this.dropShadow,
    this.innerShadow,
    this.outerGlow,
    this.colorOverlay,
    this.gradientOverlay,
    this.stroke,
  });

  final ShadowStyle? dropShadow;
  final ShadowStyle? innerShadow;
  final GlowStyle? outerGlow;
  final OverlayStyle? colorOverlay;
  final GradientOverlayStyle? gradientOverlay;
  final StrokeStyle? stroke;

  bool get isEmpty =>
      dropShadow == null &&
      innerShadow == null &&
      outerGlow == null &&
      colorOverlay == null &&
      gradientOverlay == null &&
      stroke == null;

  LayerStyles copyWith({
    ShadowStyle? dropShadow,
    ShadowStyle? innerShadow,
    GlowStyle? outerGlow,
    OverlayStyle? colorOverlay,
    GradientOverlayStyle? gradientOverlay,
    StrokeStyle? stroke,
    bool clearDropShadow = false,
    bool clearInnerShadow = false,
    bool clearOuterGlow = false,
    bool clearColorOverlay = false,
    bool clearGradientOverlay = false,
    bool clearStroke = false,
  }) {
    return LayerStyles(
      dropShadow: clearDropShadow ? null : (dropShadow ?? this.dropShadow),
      innerShadow: clearInnerShadow ? null : (innerShadow ?? this.innerShadow),
      outerGlow: clearOuterGlow ? null : (outerGlow ?? this.outerGlow),
      colorOverlay: clearColorOverlay
          ? null
          : (colorOverlay ?? this.colorOverlay),
      gradientOverlay: clearGradientOverlay
          ? null
          : (gradientOverlay ?? this.gradientOverlay),
      stroke: clearStroke ? null : (stroke ?? this.stroke),
    );
  }
}

class ShadowStyle {
  ShadowStyle({
    this.enabled = true,
    this.color = const Color(0xFF000000),
    AnimatedDouble? opacity,
    AnimatedDouble? angleDeg,
    AnimatedDouble? distance,
    AnimatedDouble? size,
    AnimatedDouble? spread,
  }) : opacity = opacity ?? AnimatedDouble(0.5),
       angleDeg = angleDeg ?? AnimatedDouble(120),
       distance = distance ?? AnimatedDouble(12),
       size = size ?? AnimatedDouble(10),
       spread = spread ?? AnimatedDouble(0);

  final bool enabled;
  final Color color;
  final AnimatedDouble opacity;
  final AnimatedDouble angleDeg;
  final AnimatedDouble distance;
  final AnimatedDouble size;
  final AnimatedDouble spread;

  Offset offsetAt(Duration t) {
    final a = angleDeg.valueAt(t) * math.pi / 180;
    final d = distance.valueAt(t);
    // Angulo em graus no sentido do AE: 0 = direita, cresce anti-horario.
    return Offset(d * math.cos(a), -d * math.sin(a));
  }

  ShadowStyle copyWith({
    bool? enabled,
    Color? color,
    AnimatedDouble? opacity,
    AnimatedDouble? angleDeg,
    AnimatedDouble? distance,
    AnimatedDouble? size,
    AnimatedDouble? spread,
  }) => ShadowStyle(
    enabled: enabled ?? this.enabled,
    color: color ?? this.color,
    opacity: opacity ?? this.opacity,
    angleDeg: angleDeg ?? this.angleDeg,
    distance: distance ?? this.distance,
    size: size ?? this.size,
    spread: spread ?? this.spread,
  );
}

class GlowStyle {
  GlowStyle({
    this.enabled = true,
    this.color = const Color(0xFFFFFFFF),
    AnimatedDouble? opacity,
    AnimatedDouble? size,
  }) : opacity = opacity ?? AnimatedDouble(0.75),
       size = size ?? AnimatedDouble(16);

  final bool enabled;
  final Color color;
  final AnimatedDouble opacity;
  final AnimatedDouble size;

  GlowStyle copyWith({
    bool? enabled,
    Color? color,
    AnimatedDouble? opacity,
    AnimatedDouble? size,
  }) => GlowStyle(
    enabled: enabled ?? this.enabled,
    color: color ?? this.color,
    opacity: opacity ?? this.opacity,
    size: size ?? this.size,
  );
}

class OverlayStyle {
  OverlayStyle({
    this.enabled = true,
    this.color = const Color(0xFFB8FF3D),
    AnimatedDouble? opacity,
    this.blend = BlendMode.srcATop,
  }) : opacity = opacity ?? AnimatedDouble(1);

  final bool enabled;
  final Color color;
  final AnimatedDouble opacity;
  final BlendMode blend;

  OverlayStyle copyWith({
    bool? enabled,
    Color? color,
    AnimatedDouble? opacity,
    BlendMode? blend,
  }) => OverlayStyle(
    enabled: enabled ?? this.enabled,
    color: color ?? this.color,
    opacity: opacity ?? this.opacity,
    blend: blend ?? this.blend,
  );
}

class GradientOverlayStyle {
  GradientOverlayStyle({
    this.enabled = true,
    this.colorA = const Color(0xFFB8FF3D),
    this.colorB = const Color(0xFF7C62FF),
    AnimatedDouble? angleDeg,
    AnimatedDouble? opacity,
  }) : angleDeg = angleDeg ?? AnimatedDouble(90),
       opacity = opacity ?? AnimatedDouble(1);

  final bool enabled;
  final Color colorA;
  final Color colorB;
  final AnimatedDouble angleDeg;
  final AnimatedDouble opacity;

  GradientOverlayStyle copyWith({
    bool? enabled,
    Color? colorA,
    Color? colorB,
    AnimatedDouble? angleDeg,
    AnimatedDouble? opacity,
  }) => GradientOverlayStyle(
    enabled: enabled ?? this.enabled,
    colorA: colorA ?? this.colorA,
    colorB: colorB ?? this.colorB,
    angleDeg: angleDeg ?? this.angleDeg,
    opacity: opacity ?? this.opacity,
  );
}

class StrokeStyle {
  StrokeStyle({
    this.enabled = true,
    this.color = const Color(0xFFFFFFFF),
    AnimatedDouble? width,
    AnimatedDouble? opacity,
  }) : width = width ?? AnimatedDouble(4),
       opacity = opacity ?? AnimatedDouble(1);

  final bool enabled;
  final Color color;
  final AnimatedDouble width;
  final AnimatedDouble opacity;

  StrokeStyle copyWith({
    bool? enabled,
    Color? color,
    AnimatedDouble? width,
    AnimatedDouble? opacity,
  }) => StrokeStyle(
    enabled: enabled ?? this.enabled,
    color: color ?? this.color,
    width: width ?? this.width,
    opacity: opacity ?? this.opacity,
  );
}

/// PALETA DO PROJETO (PR-X11): cores NOMEADAS. Qualquer campo de cor
/// pode vincular a uma entrada — trocar a paleta muda o projeto inteiro,
/// que e o que permite entregar o mesmo pacote em tres marcas.
class Palette {
  const Palette({this.entries = const {}});

  final Map<String, Color> entries;

  Color? operator [](String name) => entries[name];

  Palette withColor(String name, Color c) =>
      Palette(entries: {...entries, name: c});

  Palette without(String name) => Palette(entries: {...entries}..remove(name));

  static const aurea = Palette(
    entries: {
      'primaria': Color(0xFFB8FF3D),
      'fundo': Color(0xFF12151A),
      'destaque': Color(0xFF7C62FF),
      'texto': Color(0xFFE7ECF3),
    },
  );
}

/// Estilo de texto nomeado (PR-X12).
class TextStyleDef {
  const TextStyleDef({
    required this.name,
    this.fontSize = 120,
    this.bold = true,
    this.color = const Color(0xFFFFFFFF),
    this.colorRef,
    this.tracking = 0,
    this.lineHeight = 1.2,
  });

  final String name;
  final double fontSize;
  final bool bold;
  final Color color;

  /// Vinculo com a paleta (vence sobre [color] quando presente).
  final String? colorRef;
  final double tracking;
  final double lineHeight;

  TextStyleDef copyWith({
    String? name,
    double? fontSize,
    bool? bold,
    Color? color,
    String? colorRef,
    double? tracking,
    double? lineHeight,
  }) => TextStyleDef(
    name: name ?? this.name,
    fontSize: fontSize ?? this.fontSize,
    bold: bold ?? this.bold,
    color: color ?? this.color,
    colorRef: colorRef ?? this.colorRef,
    tracking: tracking ?? this.tracking,
    lineHeight: lineHeight ?? this.lineHeight,
  );
}

// ---------------------------------------------------------- bloco 4

/// Modo da caixa de texto (PR-X13).
enum TextBoxMode { autoWidth, autoHeight, fixed }

/// Canto que fica PARADO quando o texto cresce. Sem isso, um nome mais
/// longo desloca o layout inteiro.
enum GrowAnchor {
  topLeft,
  topCenter,
  topRight,
  centerLeft,
  center,
  centerRight,
  bottomLeft,
  bottomCenter,
  bottomRight,
}

class TextBoxSpec {
  const TextBoxSpec({
    this.mode = TextBoxMode.autoWidth,
    this.width = 600,
    this.height = 200,
    this.anchor = GrowAnchor.center,
  });

  final TextBoxMode mode;
  final double width;
  final double height;
  final GrowAnchor anchor;

  TextBoxSpec copyWith({
    TextBoxMode? mode,
    double? width,
    double? height,
    GrowAnchor? anchor,
  }) => TextBoxSpec(
    mode: mode ?? this.mode,
    width: width ?? this.width,
    height: height ?? this.height,
    anchor: anchor ?? this.anchor,
  );
}

/// Deslocamento do CENTRO quando uma caixa muda de tamanho mantendo o
/// ponto de ancoragem parado. E o coracao do layout responsivo.
Offset anchorShift(Size before, Size after, GrowAnchor anchor) {
  final dw = after.width - before.width;
  final dh = after.height - before.height;
  final fx = switch (anchor) {
    GrowAnchor.topLeft || GrowAnchor.centerLeft || GrowAnchor.bottomLeft => 0.5,
    GrowAnchor.topCenter || GrowAnchor.center || GrowAnchor.bottomCenter => 0.0,
    GrowAnchor.topRight ||
    GrowAnchor.centerRight ||
    GrowAnchor.bottomRight => -0.5,
  };
  final fy = switch (anchor) {
    GrowAnchor.topLeft || GrowAnchor.topCenter || GrowAnchor.topRight => 0.5,
    GrowAnchor.centerLeft || GrowAnchor.center || GrowAnchor.centerRight => 0.0,
    GrowAnchor.bottomLeft ||
    GrowAnchor.bottomCenter ||
    GrowAnchor.bottomRight => -0.5,
  };
  return Offset(dw * fx, dh * fy);
}

/// Forma que ABRACA um texto (PR-X14): e o que torna um lower-third
/// reutilizavel em vez de refeito para cada nome.
class ContainerSpec {
  const ContainerSpec({
    required this.targetLayerId,
    this.padLeft = 32,
    this.padRight = 32,
    this.padTop = 18,
    this.padBottom = 18,
    this.minWidth = 0,
    this.maxWidth = 100000,
    this.anchor = GrowAnchor.center,
    this.follow = true,
  });

  final String targetLayerId;
  final double padLeft;
  final double padRight;
  final double padTop;
  final double padBottom;
  final double minWidth;
  final double maxWidth;
  final GrowAnchor anchor;

  /// A forma tambem acompanha a POSICAO do alvo.
  final bool follow;

  /// Tamanho da forma para um texto de [textSize].
  Size sizeFor(Size textSize) {
    final w = (textSize.width + padLeft + padRight).clamp(minWidth, maxWidth);
    return Size(w.toDouble(), textSize.height + padTop + padBottom);
  }

  ContainerSpec copyWith({
    String? targetLayerId,
    double? padLeft,
    double? padRight,
    double? padTop,
    double? padBottom,
    double? minWidth,
    double? maxWidth,
    GrowAnchor? anchor,
    bool? follow,
  }) => ContainerSpec(
    targetLayerId: targetLayerId ?? this.targetLayerId,
    padLeft: padLeft ?? this.padLeft,
    padRight: padRight ?? this.padRight,
    padTop: padTop ?? this.padTop,
    padBottom: padBottom ?? this.padBottom,
    minWidth: minWidth ?? this.minWidth,
    maxWidth: maxWidth ?? this.maxWidth,
    anchor: anchor ?? this.anchor,
    follow: follow ?? this.follow,
  );
}

enum StackDirection { vertical, horizontal }

enum StackAlign { start, center, end }

enum StackDistribution { packed, spaced }

/// Empilhamento automatico (PR-X15).
class StackSpec {
  const StackSpec({
    this.direction = StackDirection.vertical,
    this.gap = 24,
    this.align = StackAlign.center,
    this.padding = 0,
    this.distribution = StackDistribution.packed,
    this.extent = 0,
  });

  final StackDirection direction;
  final double gap;
  final StackAlign align;
  final double padding;
  final StackDistribution distribution;

  /// Comprimento total quando [distribution] e spaced.
  final double extent;

  StackSpec copyWith({
    StackDirection? direction,
    double? gap,
    StackAlign? align,
    double? padding,
    StackDistribution? distribution,
    double? extent,
  }) => StackSpec(
    direction: direction ?? this.direction,
    gap: gap ?? this.gap,
    align: align ?? this.align,
    padding: padding ?? this.padding,
    distribution: distribution ?? this.distribution,
    extent: extent ?? this.extent,
  );
}

/// Posiciona os filhos de um grupo conforme [spec]. Devolve o CENTRO de
/// cada um, relativo ao centro do grupo. Funcao pura e testavel.
Map<String, Offset> stackLayout(
  StackSpec spec,
  List<({String id, Size size})> children,
) {
  if (children.isEmpty) return const {};
  final vertical = spec.direction == StackDirection.vertical;
  double extentOf(Size s) => vertical ? s.height : s.width;
  double crossOf(Size s) => vertical ? s.width : s.height;

  var total = 0.0;
  for (final c in children) {
    total += extentOf(c.size);
  }
  final n = children.length;
  var gap = spec.gap;
  if (spec.distribution == StackDistribution.spaced && n > 1) {
    final free = spec.extent - spec.padding * 2 - total;
    gap = free / (n - 1);
  }
  final span = total + gap * (n - 1);

  var cursor = -span / 2;
  final maxCross = children
      .map((c) => crossOf(c.size))
      .reduce((a, b) => a > b ? a : b);

  final out = <String, Offset>{};
  for (final c in children) {
    final e = extentOf(c.size);
    final main = cursor + e / 2;
    final cross = switch (spec.align) {
      StackAlign.start => -maxCross / 2 + crossOf(c.size) / 2,
      StackAlign.center => 0.0,
      StackAlign.end => maxCross / 2 - crossOf(c.size) / 2,
    };
    out[c.id] = vertical ? Offset(cross, main) : Offset(main, cross);
    cursor += e + gap;
  }
  return out;
}

// ---------------------------------------------------------- bloco 5

enum ExposedType { number, color, text, boolean, choice, media }

/// Propriedade EXPOSTA de uma precomp (PR-X16): quem usa o template
/// mexe aqui e nao abre a precomp.
class ExposedProperty {
  const ExposedProperty({
    required this.id,
    required this.layerId,
    required this.property,
    required this.label,
    this.type = ExposedType.number,
    this.group = 'Geral',
    this.min,
    this.max,
    this.step,
    this.options = const [],
  });

  final String id;

  /// Camada DENTRO da precomp e qual propriedade dela.
  final String layerId;
  final String property;

  final String label;
  final ExposedType type;
  final String group;
  final double? min;
  final double? max;
  final double? step;
  final List<String> options;

  /// Aplica limites a um valor numerico exposto — e o que impede o
  /// cliente de quebrar o layout.
  double clampValue(double v) {
    var out = v;
    if (min != null && out < min!) out = min!;
    if (max != null && out > max!) out = max!;
    if (step != null && step! > 0) {
      out = (out / step!).roundToDouble() * step!;
    }
    return out;
  }

  ExposedProperty copyWith({
    String? label,
    ExposedType? type,
    String? group,
    double? min,
    double? max,
    double? step,
    List<String>? options,
  }) => ExposedProperty(
    id: id,
    layerId: layerId,
    property: property,
    label: label ?? this.label,
    type: type ?? this.type,
    group: group ?? this.group,
    min: min ?? this.min,
    max: max ?? this.max,
    step: step ?? this.step,
    options: options ?? this.options,
  );
}

// ---------------------------------------------------------- bloco 6

/// Osso (PR-X18).
class Bone {
  const Bone({
    required this.id,
    this.parentId,
    required this.length,
    required this.angleDeg,
  });

  final String id;
  final String? parentId;
  final double length;
  final double angleDeg;

  Bone copyWith({double? length, double? angleDeg, String? parentId}) => Bone(
    id: id,
    parentId: parentId ?? this.parentId,
    length: length ?? this.length,
    angleDeg: angleDeg ?? this.angleDeg,
  );
}

/// Resultado do solver de duas juntas.
typedef IkSolution = ({double angle1Deg, double angle2Deg, bool reached});

/// CINEMATICA INVERSA de DOIS OSSOS, solucao ANALITICA (PR-X18): lei dos
/// cossenos, sem iteracao — rapido e deterministico.
IkSolution solveTwoBoneIk({
  required Offset root,
  required Offset target,
  required double l1,
  required double l2,
  bool flip = false,
  bool stretch = false,
}) {
  final delta = target - root;
  var dist = delta.distance;
  final baseAngle = math.atan2(delta.dy, delta.dx);
  final reach = l1 + l2;
  final minReach = (l1 - l2).abs();

  var a1 = l1;
  var a2 = l2;
  var reached = true;
  if (dist > reach) {
    reached = false;
    if (stretch) {
      // Estica proporcionalmente ate alcancar.
      final k = dist / reach;
      a1 = l1 * k;
      a2 = l2 * k;
    } else {
      dist = reach; // trava esticado na direcao do alvo
    }
  } else if (dist < minReach) {
    reached = false;
    dist = minReach;
  }

  final cosInner = ((a1 * a1 + dist * dist - a2 * a2) / (2 * a1 * dist)).clamp(
    -1.0,
    1.0,
  );
  final cosElbow = ((a1 * a1 + a2 * a2 - dist * dist) / (2 * a1 * a2)).clamp(
    -1.0,
    1.0,
  );
  final inner = math.acos(cosInner);
  final elbow = math.acos(cosElbow);
  final s = flip ? -1.0 : 1.0;

  // Angulo do primeiro osso e do cotovelo (relativo ao primeiro).
  final angle1 = baseAngle - s * inner;
  final angle2 = s * (math.pi - elbow);
  return (
    angle1Deg: angle1 * 180 / math.pi,
    angle2Deg: angle2 * 180 / math.pi,
    reached: reached,
  );
}

/// Membro de tubo (PR-X20): forma vetorial que dobra sozinha entre dois
/// pontos, com espessura variavel — resolve personagem simples sem malha.
class TubeLimb {
  const TubeLimb({
    required this.rootId,
    required this.targetId,
    this.thickStart = 40,
    this.thickEnd = 24,
    this.bend = 0.35,
    this.color = const Color(0xFFB8FF3D),
  });

  final String rootId;
  final String targetId;
  final double thickStart;
  final double thickEnd;

  /// Quanto o cotovelo se afasta da reta (fracao do comprimento).
  final double bend;
  final Color color;

  TubeLimb copyWith({
    double? thickStart,
    double? thickEnd,
    double? bend,
    Color? color,
  }) => TubeLimb(
    rootId: rootId,
    targetId: targetId,
    thickStart: thickStart ?? this.thickStart,
    thickEnd: thickEnd ?? this.thickEnd,
    bend: bend ?? this.bend,
    color: color ?? this.color,
  );
}

// ---------------------------------------------------------- bloco 7

/// Formatacao numerica (PR-X22).
class NumberFormatSpec {
  const NumberFormatSpec({
    this.decimals = 0,
    this.thousands = true,
    this.prefix = '',
    this.suffix = '',
    this.percent = false,
  });

  final int decimals;
  final bool thousands;
  final String prefix;
  final String suffix;
  final bool percent;

  String format(double v) {
    final value = percent ? v * 100 : v;
    var s = value.toStringAsFixed(decimals);
    if (thousands) {
      final parts = s.split('.');
      final intPart = parts[0];
      final neg = intPart.startsWith('-');
      final digits = neg ? intPart.substring(1) : intPart;
      final buf = StringBuffer();
      for (var i = 0; i < digits.length; i++) {
        if (i > 0 && (digits.length - i) % 3 == 0) buf.write('.');
        buf.write(digits[i]);
      }
      s = '${neg ? '-' : ''}$buf${parts.length > 1 ? ',${parts[1]}' : ''}';
    } else if (decimals > 0) {
      s = s.replaceFirst('.', ',');
    }
    return '$prefix$s${percent ? '%' : ''}$suffix';
  }

  NumberFormatSpec copyWith({
    int? decimals,
    bool? thousands,
    String? prefix,
    String? suffix,
    bool? percent,
  }) => NumberFormatSpec(
    decimals: decimals ?? this.decimals,
    thousands: thousands ?? this.thousands,
    prefix: prefix ?? this.prefix,
    suffix: suffix ?? this.suffix,
    percent: percent ?? this.percent,
  );
}

/// Contador animado (PR-X22): o numero sobe com a curva da propria
/// propriedade, e e formatado na hora de virar texto.
class CounterSpec {
  CounterSpec({AnimatedDouble? value, this.format = const NumberFormatSpec()})
    : value = value ?? AnimatedDouble(0);

  final AnimatedDouble value;
  final NumberFormatSpec format;

  String textAt(Duration t) => format.format(value.valueAt(t));

  CounterSpec copyWith({AnimatedDouble? value, NumberFormatSpec? format}) =>
      CounterSpec(value: value ?? this.value, format: format ?? this.format);
}

/// Fonte de dados CSV/JSON (PR-X21).
class DataSource {
  const DataSource({
    this.name = 'dados',
    this.columns = const [],
    this.rows = const [],
  });

  final String name;
  final List<String> columns;
  final List<List<String>> rows;

  int get rowCount => rows.length;

  String? cell(int row, String column) {
    final c = columns.indexOf(column);
    if (c < 0 || row < 0 || row >= rows.length) return null;
    final r = rows[row];
    return c < r.length ? r[c] : null;
  }

  double? number(int row, String column) {
    final raw = cell(row, column);
    if (raw == null) return null;
    return double.tryParse(raw.replaceAll('.', '').replaceAll(',', '.')) ??
        double.tryParse(raw);
  }
}

/// Lê CSV simples (virgula ou ponto-e-virgula, aspas duplas).
DataSource parseCsv(String content, {String name = 'dados'}) {
  final lines = content
      .replaceAll('\r\n', '\n')
      .split('\n')
      .where((l) => l.trim().isNotEmpty)
      .toList();
  if (lines.isEmpty) return DataSource(name: name);
  final sep = lines.first.contains(';') ? ';' : ',';

  List<String> splitLine(String line) {
    final out = <String>[];
    final buf = StringBuffer();
    var quoted = false;
    for (var i = 0; i < line.length; i++) {
      final ch = line[i];
      if (ch == '"') {
        if (quoted && i + 1 < line.length && line[i + 1] == '"') {
          buf.write('"');
          i++;
        } else {
          quoted = !quoted;
        }
      } else if (ch == sep && !quoted) {
        out.add(buf.toString().trim());
        buf.clear();
      } else {
        buf.write(ch);
      }
    }
    out.add(buf.toString().trim());
    return out;
  }

  return DataSource(
    name: name,
    columns: splitLine(lines.first),
    rows: [for (final l in lines.skip(1)) splitLine(l)],
  );
}

/// Vinculo campo -> camada (PR-X21).
class DataBinding {
  const DataBinding({
    required this.layerId,
    required this.column,
    this.property = 'text',
    this.row = 0,
    this.format = const NumberFormatSpec(),
  });

  final String layerId;
  final String column;

  /// 'text' escreve no texto; qualquer outro nome vira propriedade.
  final String property;
  final int row;
  final NumberFormatSpec format;

  DataBinding copyWith({String? column, String? property, int? row}) =>
      DataBinding(
        layerId: layerId,
        column: column ?? this.column,
        property: property ?? this.property,
        row: row ?? this.row,
        format: format,
      );
}

// ------------------------------------------------------- meta da camada

/// Tudo que se pendura numa camada sem mudar o motor.
class LayerMeta {
  const LayerMeta({
    this.label,
    this.solo = false,
    this.shy = false,
    this.locked = false,
    this.folder,
    this.styles = const LayerStyles(),
    this.textBox,
    this.container,
    this.stack,
    this.counter,
    this.colorRef,
    this.textStyleRef,
    this.motionBlur = false,
    this.extrude = 0,
  });

  final LayerLabel? label;
  final bool solo;
  final bool shy;
  final bool locked;
  final String? folder;
  final LayerStyles styles;
  final TextBoxSpec? textBox;
  final ContainerSpec? container;
  final StackSpec? stack;
  final CounterSpec? counter;

  /// Vinculo de cor com a paleta do projeto (PR-X11).
  final String? colorRef;

  /// Vinculo com um estilo de texto nomeado (PR-X12).
  final String? textStyleRef;

  /// Motion blur por camada (PR-X9).
  final bool motionBlur;

  /// EXTRUDE 3D: espessura (px) da camada quando inclinada em X/Y —
  /// fatias empilhadas atras da frente. 0 = desligado.
  final double extrude;

  bool get isEmpty =>
      label == null &&
      !solo &&
      !shy &&
      !locked &&
      folder == null &&
      styles.isEmpty &&
      textBox == null &&
      container == null &&
      stack == null &&
      counter == null &&
      colorRef == null &&
      textStyleRef == null &&
      !motionBlur &&
      extrude <= 0;

  LayerMeta copyWith({
    LayerLabel? label,
    bool? solo,
    bool? shy,
    bool? locked,
    String? folder,
    LayerStyles? styles,
    TextBoxSpec? textBox,
    ContainerSpec? container,
    StackSpec? stack,
    CounterSpec? counter,
    String? colorRef,
    String? textStyleRef,
    bool? motionBlur,
    double? extrude,
    bool clearLabel = false,
    bool clearContainer = false,
    bool clearStack = false,
    bool clearCounter = false,
    bool clearColorRef = false,
  }) {
    return LayerMeta(
      label: clearLabel ? null : (label ?? this.label),
      solo: solo ?? this.solo,
      shy: shy ?? this.shy,
      locked: locked ?? this.locked,
      folder: folder ?? this.folder,
      styles: styles ?? this.styles,
      textBox: textBox ?? this.textBox,
      container: clearContainer ? null : (container ?? this.container),
      stack: clearStack ? null : (stack ?? this.stack),
      counter: clearCounter ? null : (counter ?? this.counter),
      colorRef: clearColorRef ? null : (colorRef ?? this.colorRef),
      textStyleRef: textStyleRef ?? this.textStyleRef,
      motionBlur: motionBlur ?? this.motionBlur,
      extrude: extrude ?? this.extrude,
    );
  }

  static const empty = LayerMeta();
}

/// MOTION BLUR real (PR-X9): obturador, fase e amostras.
class MotionBlurSpec {
  const MotionBlurSpec({
    this.enabled = false,
    this.shutterAngle = 180,
    this.shutterPhase = -90,
    this.samples = 16,
    this.adaptiveLimit = 32,
  });

  /// 0..720. 180 e o padrao cinematografico.
  final double shutterAngle;

  /// -360..360. -90 CENTRALIZA o borrao no frame; 0 arrasta para frente.
  final double shutterPhase;

  final int samples;
  final int adaptiveLimit;
  final bool enabled;

  /// Janela de exposicao (em frames) que o borrao cobre, ja com a fase.
  /// Com 180/-90 a janela e [-0,25, +0,25] — centrada.
  (double, double) exposureWindow() {
    final dur = shutterAngle / 360;
    final start = shutterPhase / 360;
    return (start, start + dur);
  }

  MotionBlurSpec copyWith({
    bool? enabled,
    double? shutterAngle,
    double? shutterPhase,
    int? samples,
    int? adaptiveLimit,
  }) => MotionBlurSpec(
    enabled: enabled ?? this.enabled,
    shutterAngle: shutterAngle ?? this.shutterAngle,
    shutterPhase: shutterPhase ?? this.shutterPhase,
    samples: samples ?? this.samples,
    adaptiveLimit: adaptiveLimit ?? this.adaptiveLimit,
  );
}

/// GUIAS, GRADE E AREAS SEGURAS (PR-X3).
class GuidesSpec {
  const GuidesSpec({
    this.vertical = const [],
    this.horizontal = const [],
    this.columns = 0,
    this.gutter = 24,
    this.margin = 48,
    this.showSafeAreas = false,
    this.framePreview,
  });

  /// Guias arrastaveis, em px.
  final List<double> vertical;
  final List<double> horizontal;

  /// Grade de layout: colunas, medianiz e margem.
  final int columns;
  final double gutter;
  final double margin;

  final bool showSafeAreas;

  /// Mascara de enquadramento: mostra como o quadro fica cortado noutra
  /// proporcao, SEM alterar o projeto.
  final double? framePreview;

  GuidesSpec copyWith({
    List<double>? vertical,
    List<double>? horizontal,
    int? columns,
    double? gutter,
    double? margin,
    bool? showSafeAreas,
    double? framePreview,
    bool clearFramePreview = false,
  }) => GuidesSpec(
    vertical: vertical ?? this.vertical,
    horizontal: horizontal ?? this.horizontal,
    columns: columns ?? this.columns,
    gutter: gutter ?? this.gutter,
    margin: margin ?? this.margin,
    showSafeAreas: showSafeAreas ?? this.showSafeAreas,
    framePreview: clearFramePreview
        ? null
        : (framePreview ?? this.framePreview),
  );
}

/// Ponta do primeiro osso e do segundo, dada a solucao do IK. Util para
/// desenhar o rig e para posicionar as camadas parenteadas.
({Offset elbow, Offset tip}) twoBoneJoints({
  required Offset root,
  required double l1,
  required double l2,
  required IkSolution sol,
}) {
  final a1 = sol.angle1Deg * math.pi / 180;
  final a2 = a1 + sol.angle2Deg * math.pi / 180;
  final elbow = root + Offset(math.cos(a1), math.sin(a1)) * l1;
  final tip = elbow + Offset(math.cos(a2), math.sin(a2)) * l2;
  return (elbow: elbow, tip: tip);
}
