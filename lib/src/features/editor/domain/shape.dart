import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui';

import 'package:uuid/uuid.dart';

import 'keyframe.dart';
import 'shape_ops.dart';
import 'mask.dart';
import 'svg_path.dart';

/// Formas vetoriais (spec AM2-formas-3d §1): a camada de forma vira uma
/// ARVORE de itens, avaliada de baixo para cima dentro de cada grupo.
/// Nada de bitmap: os Paths sao vetoriais no paint e ficam nitidos em
/// qualquer escala.

sealed class ShapeItem {
  ShapeItem({String? id}) : id = id ?? const Uuid().v4();
  final String id;
}

/// ------------------------------------------------------------- caminhos

enum ShapePrimitive {
  rectangle,
  roundedRectangle,
  ellipse,
  polygon,
  star,
  ring,
  arc,
  wave,
  // Novos no FIM: indices antigos continuam validos na serializacao.
  heart,
  gear,
  arrow,
  check,
  plus,
  drop,
  flower,
  sparkle,
}

class ShapePath extends ShapeItem {
  ShapePath({
    super.id,
    required this.primitive,
    this.width = 420,
    this.height = 420,
    this.cornerRadius = 48,
    this.points = 5,
    this.innerRadiusRatio = 0.5,
    this.startAngle = 0,
    this.sweepAngle = 270,
    this.thickness = 60,
    this.amplitude = 60,
    this.frequency = 3,
  });

  final ShapePrimitive primitive;
  final double width;
  final double height;

  /// roundedRectangle.
  final double cornerRadius;

  /// polygon/star: numero de pontas.
  final int points;

  /// star/ring: razao do raio interno (0..1).
  final double innerRadiusRatio;

  /// arc: graus.
  final double startAngle;
  final double sweepAngle;

  /// ring/arc: espessura do anel.
  final double thickness;

  /// wave.
  final double amplitude;
  final double frequency;

  Path? _cache;

  /// Constroi o Path em espaco de objeto, centrado em (0,0) — cacheado:
  /// a instancia e imutavel (zero alocacao por frame, motor-de-preview).
  Path build() => _cache ??= _buildNow();

  Path _buildNow() {
    final w = width;
    final h = height;
    final rect = Rect.fromCenter(center: Offset.zero, width: w, height: h);
    switch (primitive) {
      case ShapePrimitive.rectangle:
        return Path()..addRect(rect);
      case ShapePrimitive.roundedRectangle:
        return Path()..addRRect(
          RRect.fromRectAndRadius(rect, Radius.circular(cornerRadius)),
        );
      case ShapePrimitive.ellipse:
        return Path()..addOval(rect);
      case ShapePrimitive.polygon:
        return _radial(points, w / 2, w / 2);
      case ShapePrimitive.star:
        return _radial(points * 2, w / 2, w / 2 * innerRadiusRatio);
      case ShapePrimitive.ring:
        final outer = Path()..addOval(rect);
        final innerR = (w / 2 - thickness).clamp(1.0, w / 2);
        final inner = Path()
          ..addOval(Rect.fromCircle(center: Offset.zero, radius: innerR));
        return Path.combine(PathOperation.difference, outer, inner);
      case ShapePrimitive.arc:
        final r = w / 2;
        final innerR = (r - thickness).clamp(1.0, r);
        final a0 = startAngle * math.pi / 180;
        final sweep = sweepAngle * math.pi / 180;
        final path = Path()
          ..arcTo(
            Rect.fromCircle(center: Offset.zero, radius: r),
            a0,
            sweep,
            true,
          )
          ..arcTo(
            Rect.fromCircle(center: Offset.zero, radius: innerR),
            a0 + sweep,
            -sweep,
            false,
          )
          ..close();
        return path;
      case ShapePrimitive.wave:
        final path = Path()..moveTo(-w / 2, 0);
        const steps = 64;
        for (var i = 1; i <= steps; i++) {
          final x = -w / 2 + w * i / steps;
          final y = math.sin(i / steps * frequency * 2 * math.pi) * amplitude;
          path.lineTo(x, y);
        }
        return path;
      case ShapePrimitive.heart:
        final w2 = w / 2;
        final h2 = h / 2;
        return Path()
          ..moveTo(0, -h2 * 0.25)
          ..cubicTo(
            -w2 * 0.55,
            -h2 * 1.05,
            -w2 * 1.05,
            -h2 * 0.15,
            0,
            h2 * 0.95,
          )
          ..cubicTo(w2 * 1.05, -h2 * 0.15, w2 * 0.55, -h2 * 1.05, 0, -h2 * 0.25)
          ..close();
      case ShapePrimitive.gear:
        final n = math.max(4, points);
        final path = Path();
        final rOuter = w / 2;
        final rInner = rOuter * 0.74;
        final step = math.pi * 2 / n;
        for (var i = 0; i < n; i++) {
          final a0 = i * step;
          void pt(double a, double r, [bool move = false]) {
            final p = Offset(math.cos(a) * r, math.sin(a) * r);
            move ? path.moveTo(p.dx, p.dy) : path.lineTo(p.dx, p.dy);
          }

          pt(a0, rInner, i == 0);
          pt(a0 + step * 0.22, rInner);
          pt(a0 + step * 0.28, rOuter);
          pt(a0 + step * 0.72, rOuter);
          pt(a0 + step * 0.78, rInner);
        }
        path.close();
        return path;
      case ShapePrimitive.arrow:
        final head = w * 0.34;
        final shaft = h * 0.18;
        return Path()
          ..moveTo(-w / 2, -shaft)
          ..lineTo(w / 2 - head, -shaft)
          ..lineTo(w / 2 - head, -h / 2)
          ..lineTo(w / 2, 0)
          ..lineTo(w / 2 - head, h / 2)
          ..lineTo(w / 2 - head, shaft)
          ..lineTo(-w / 2, shaft)
          ..close();
      case ShapePrimitive.check:
        return Path()
          ..moveTo(-w * 0.44, h * 0.04)
          ..lineTo(-w * 0.10, h * 0.36)
          ..lineTo(w * 0.46, -h * 0.28)
          ..lineTo(w * 0.34, -h * 0.44)
          ..lineTo(-w * 0.10, h * 0.08)
          ..lineTo(-w * 0.32, -h * 0.12)
          ..close();
      case ShapePrimitive.plus:
        final arm = w * 0.18;
        return Path()
          ..addRect(
            Rect.fromCenter(center: Offset.zero, width: w, height: arm * 2),
          )
          ..addRect(
            Rect.fromCenter(center: Offset.zero, width: arm * 2, height: h),
          );
      case ShapePrimitive.drop:
        final w2 = w / 2;
        final h2 = h / 2;
        return Path()
          ..moveTo(0, -h2)
          ..cubicTo(w2 * 0.9, h2 * 0.05, w2 * 0.85, h2, 0, h2)
          ..cubicTo(-w2 * 0.85, h2, -w2 * 0.9, h2 * 0.05, 0, -h2)
          ..close();
      case ShapePrimitive.flower:
        final n = math.max(3, points);
        final path = Path();
        final r = w / 2;
        final step = math.pi * 2 / n;
        for (var i = 0; i < n; i++) {
          final a = -math.pi / 2 + i * step;
          final tip = Offset(math.cos(a) * r, math.sin(a) * r);
          final c1 = Offset(
            math.cos(a - step * 0.45) * r * 0.85,
            math.sin(a - step * 0.45) * r * 0.85,
          );
          final c2 = Offset(
            math.cos(a + step * 0.45) * r * 0.85,
            math.sin(a + step * 0.45) * r * 0.85,
          );
          path.moveTo(0, 0);
          path.cubicTo(c1.dx, c1.dy, tip.dx, tip.dy, tip.dx, tip.dy);
          path.cubicTo(tip.dx, tip.dy, c2.dx, c2.dy, 0, 0);
          path.close();
        }
        return path;
      case ShapePrimitive.sparkle:
        final r = w / 2;
        final path = Path()..moveTo(0, -r);
        for (var k = 0; k < 4; k++) {
          final aTip = -math.pi / 2 + (k + 1) * math.pi / 2;
          final aCtrl = -math.pi / 4 + k * math.pi / 2;
          path.quadraticBezierTo(
            math.cos(aCtrl) * r * 0.16,
            math.sin(aCtrl) * r * 0.16,
            math.cos(aTip) * r,
            math.sin(aTip) * r,
          );
        }
        path.close();
        return path;
    }
  }

  Path _radial(int n, double outer, double inner) {
    final path = Path();
    for (var i = 0; i < n; i++) {
      final r = i.isEven ? outer : inner;
      final a = -math.pi / 2 + i * math.pi * 2 / n;
      final p = Offset(math.cos(a) * r, math.sin(a) * r);
      if (i == 0) {
        path.moveTo(p.dx, p.dy);
      } else {
        path.lineTo(p.dx, p.dy);
      }
    }
    path.close();
    return path;
  }

  ShapePath copyWith({
    double? width,
    double? height,
    double? cornerRadius,
    int? points,
    double? innerRadiusRatio,
    double? startAngle,
    double? sweepAngle,
    double? thickness,
    double? amplitude,
    double? frequency,
  }) {
    return ShapePath(
      id: id,
      primitive: primitive,
      width: width ?? this.width,
      height: height ?? this.height,
      cornerRadius: cornerRadius ?? this.cornerRadius,
      points: points ?? this.points,
      innerRadiusRatio: innerRadiusRatio ?? this.innerRadiusRatio,
      startAngle: startAngle ?? this.startAngle,
      sweepAngle: sweepAngle ?? this.sweepAngle,
      thickness: thickness ?? this.thickness,
      amplitude: amplitude ?? this.amplitude,
      frequency: frequency ?? this.frequency,
    );
  }
}

/// ------------------------------------------------- geometria parametrica

/// Primitivas PARAMETRICAS (spec AUREA-parametros-de-forma): TAMANHO e um
/// parametro do CAMINHO, nao da transform — animar Tamanho muda a
/// geometria e o traco fica com a mesma espessura; animar Escala (na
/// camada) engorda tudo junto, como no AE.
enum ParamShapeKind { rect, ellipse, polygon, star, sector }

/// Caminho parametrico: TODO numero que descreve a forma e animavel, com
/// trilha propria de keyframes e curva. Avaliado por tempo (geometria ->
/// pintura -> transform da camada, nessa ordem).
class ShapeParametric extends ShapeItem {
  ShapeParametric({
    super.id,
    this.kind = ParamShapeKind.rect,
    AnimatedDouble? sizeX,
    AnimatedDouble? sizeY,
    AnimatedDouble? roundness,
    this.cornerTopLeft,
    this.cornerTopRight,
    this.cornerBottomRight,
    this.cornerBottomLeft,
    this.roundnessPercent = true,
    AnimatedDouble? points,
    AnimatedDouble? outerRadius,
    AnimatedDouble? innerRadius,
    AnimatedDouble? outerRoundness,
    AnimatedDouble? innerRoundness,
    AnimatedDouble? shapeRotation,
    AnimatedDouble? startAngle,
    AnimatedDouble? sweep,
    AnimatedDouble? sectorInner,
  }) : sizeX = sizeX ?? AnimatedDouble(200),
       sizeY = sizeY ?? AnimatedDouble(200),
       roundness = roundness ?? AnimatedDouble(0),
       points = points ?? AnimatedDouble(5),
       outerRadius = outerRadius ?? AnimatedDouble(100),
       innerRadius = innerRadius ?? AnimatedDouble(50),
       outerRoundness = outerRoundness ?? AnimatedDouble(0),
       innerRoundness = innerRoundness ?? AnimatedDouble(0),
       shapeRotation = shapeRotation ?? AnimatedDouble(0),
       startAngle = startAngle ?? AnimatedDouble(0),
       sweep = sweep ?? AnimatedDouble(90),
       sectorInner = sectorInner ?? AnimatedDouble(0);

  final ParamShapeKind kind;

  /// rect/ellipse: tamanho da caixa em px.
  final AnimatedDouble sizeX;
  final AnimatedDouble sizeY;

  /// rect: arredondamento. Em % (padrao) e proporcional ao menor lado —
  /// 100% = capsula; em px e fixo. Satura em metade do menor lado.
  final AnimatedDouble roundness;

  /// Overrides por canto. Nulos preservam o comportamento legado: todos
  /// seguem [roundness]. Ao editar um canto, só aquele ganha trilha própria.
  final AnimatedDouble? cornerTopLeft;
  final AnimatedDouble? cornerTopRight;
  final AnimatedDouble? cornerBottomRight;
  final AnimatedDouble? cornerBottomLeft;
  final bool roundnessPercent;

  /// polygon/star: pontas FRACIONARIAS (3,5 pontas e uma forma
  /// intermediaria estavel — e o que anima triangulo -> quadrado suave).
  final AnimatedDouble points;

  /// polygon/star/sector: raio externo em px (parece escala, NAO e).
  final AnimatedDouble outerRadius;

  /// star: raio interno em px.
  final AnimatedDouble innerRadius;

  /// polygon/star: arredondamento em % de -100 a 200 — negativo e acima
  /// de 100 geram flor e estrela invertida.
  final AnimatedDouble outerRoundness;
  final AnimatedDouble innerRoundness;

  /// polygon/star: rotacao da geometria (independente da camada).
  final AnimatedDouble shapeRotation;

  /// sector: angulo inicial e varredura em graus; raio interno > 0 vira
  /// anel (anel de progresso, rosca, loader).
  final AnimatedDouble startAngle;
  final AnimatedDouble sweep;
  final AnimatedDouble sectorInner;

  String? _memoKey;
  Path? _memoPath;

  /// Caminho no espaco do objeto, centrado em (0,0), avaliado em [t] —
  /// memoizado pelo valor dos parametros (zero alocacao com tudo parado).
  Path buildAt(Duration t) {
    final sx = math.max(0.0, sizeX.valueAt(t));
    final sy = math.max(0.0, sizeY.valueAt(t));
    final round = roundness.valueAt(t);
    final roundTopLeft = (cornerTopLeft ?? roundness).valueAt(t);
    final roundTopRight = (cornerTopRight ?? roundness).valueAt(t);
    final roundBottomRight = (cornerBottomRight ?? roundness).valueAt(t);
    final roundBottomLeft = (cornerBottomLeft ?? roundness).valueAt(t);
    final p = points.valueAt(t).clamp(2.0, 100.0);
    final rOut = math.max(0.0, outerRadius.valueAt(t));
    final rIn = math.max(0.0, innerRadius.valueAt(t));
    final roundOut = outerRoundness.valueAt(t).clamp(-100.0, 200.0);
    final roundIn = innerRoundness.valueAt(t).clamp(-100.0, 200.0);
    final rotDeg = shapeRotation.valueAt(t);
    final a0 = startAngle.valueAt(t);
    final sw = sweep.valueAt(t).clamp(0.0, 360.0);
    final secIn = math.max(0.0, sectorInner.valueAt(t));

    final key =
        '$kind|$sx|$sy|$round|$roundTopLeft|$roundTopRight|'
        '$roundBottomRight|$roundBottomLeft|$roundnessPercent|$p|$rOut|$rIn|'
        '$roundOut|$roundIn|$rotDeg|$a0|$sw|$secIn';
    if (key == _memoKey && _memoPath != null) return _memoPath!;
    final path = _buildFrom(
      sx,
      sy,
      roundTopLeft,
      roundTopRight,
      roundBottomRight,
      roundBottomLeft,
      p,
      rOut,
      rIn,
      roundOut,
      roundIn,
      rotDeg,
      a0,
      sw,
      secIn,
    );
    _memoKey = key;
    _memoPath = path;
    return path;
  }

  Path _buildFrom(
    double sx,
    double sy,
    double roundTopLeft,
    double roundTopRight,
    double roundBottomRight,
    double roundBottomLeft,
    double p,
    double rOut,
    double rIn,
    double roundOut,
    double roundIn,
    double rotDeg,
    double a0,
    double sw,
    double secIn,
  ) {
    switch (kind) {
      case ParamShapeKind.rect:
        final rect = Rect.fromCenter(
          center: Offset.zero,
          width: sx,
          height: sy,
        );
        final halfMin = math.min(sx, sy) / 2;
        // % e proporcional ao menor lado (100% = capsula); px e fixo.
        // Nos dois casos SATURA em metade do menor lado.
        double radius(double value) =>
            (roundnessPercent ? value / 100 * halfMin : value).clamp(
              0.0,
              halfMin,
            );
        final topLeft = radius(roundTopLeft);
        final topRight = radius(roundTopRight);
        final bottomRight = radius(roundBottomRight);
        final bottomLeft = radius(roundBottomLeft);
        if (topLeft <= 0 &&
            topRight <= 0 &&
            bottomRight <= 0 &&
            bottomLeft <= 0) {
          return Path()..addRect(rect);
        }
        return Path()..addRRect(
          RRect.fromRectAndCorners(
            rect,
            topLeft: Radius.circular(topLeft),
            topRight: Radius.circular(topRight),
            bottomRight: Radius.circular(bottomRight),
            bottomLeft: Radius.circular(bottomLeft),
          ),
        );
      case ParamShapeKind.ellipse:
        return Path()..addOval(
          Rect.fromCenter(center: Offset.zero, width: sx, height: sy),
        );
      case ParamShapeKind.polygon:
        return _polystarPath(
          p: p,
          rOut: rOut,
          rIn: null,
          roundOut: roundOut,
          roundIn: 0,
          rotDeg: rotDeg,
        );
      case ParamShapeKind.star:
        return _polystarPath(
          p: p,
          rOut: rOut,
          rIn: rIn,
          roundOut: roundOut,
          roundIn: roundIn,
          rotDeg: rotDeg,
        );
      case ParamShapeKind.sector:
        final inner = math.min(secIn, rOut - 0.01);
        if (sw <= 0.01) return Path();
        if (sw >= 359.99) {
          final outerO = Path()
            ..addOval(Rect.fromCircle(center: Offset.zero, radius: rOut));
          if (inner <= 0) return outerO;
          final innerO = Path()
            ..addOval(Rect.fromCircle(center: Offset.zero, radius: inner));
          return Path.combine(PathOperation.difference, outerO, innerO);
        }
        final start = (a0 - 90) * math.pi / 180;
        final sweepRad = sw * math.pi / 180;
        final path = Path();
        if (inner <= 0) {
          path
            ..moveTo(0, 0)
            ..arcTo(
              Rect.fromCircle(center: Offset.zero, radius: rOut),
              start,
              sweepRad,
              false,
            )
            ..close();
        } else {
          path
            ..arcTo(
              Rect.fromCircle(center: Offset.zero, radius: rOut),
              start,
              sweepRad,
              true,
            )
            ..arcTo(
              Rect.fromCircle(center: Offset.zero, radius: inner),
              start + sweepRad,
              -sweepRad,
              false,
            )
            ..close();
        }
        return path;
    }
  }

  /// Polystar continuo: pontas fracionarias viram um "dente" parcial que
  /// cresce com a fracao (nada pisca); o arredondamento vira alcas
  /// TANGENCIAIS por vertice — negativo dobra para dentro (flor
  /// invertida), acima de 100 exagera as petalas.
  static Path _polystarPath({
    required double p,
    required double rOut,
    required double? rIn,
    required double roundOut,
    required double roundIn,
    required double rotDeg,
  }) {
    final star = rIn != null;
    final n = p.floor();
    final f = p - n;
    final step = 2 * math.pi / p;
    final rot = rotDeg * math.pi / 180 - math.pi / 2;

    // (angulo, raio, arredondamento em %)
    final verts = <(double, double, double)>[];
    for (var i = 0; i < n; i++) {
      verts.add((rot + i * step, rOut, roundOut));
      if (star) verts.add((rot + (i + 0.5) * step, rIn, roundIn));
    }
    if (f > 1e-6) {
      final base = star ? rIn : rOut * 0.999;
      verts.add((rot + n * step, base + (rOut - base) * f, roundOut));
      if (star) {
        final ai = math.min(rot + (n + 0.5) * step, rot + 2 * math.pi);
        verts.add((ai, rIn, roundIn));
      }
    }
    if (verts.length < 3) {
      return Path()
        ..addOval(Rect.fromCircle(center: Offset.zero, radius: rOut));
    }

    Offset pt(double a, double r) => Offset(math.cos(a) * r, math.sin(a) * r);
    Offset tan(double a) => Offset(-math.sin(a), math.cos(a));

    final m = verts.length;
    final pts = [for (final v in verts) pt(v.$1, v.$2)];
    final path = Path()..moveTo(pts[0].dx, pts[0].dy);
    for (var i = 0; i < m; i++) {
      final j = (i + 1) % m;
      final cur = pts[i];
      final nxt = pts[j];
      final chord = (nxt - cur).distance;
      final h1 = chord * 0.5 * (verts[i].$3 / 100);
      final h2 = chord * 0.5 * (verts[j].$3 / 100);
      final c1 = cur + tan(verts[i].$1) * h1;
      final c2 = nxt - tan(verts[j].$1) * h2;
      path.cubicTo(c1.dx, c1.dy, c2.dx, c2.dy, nxt.dx, nxt.dy);
    }
    path.close();
    return path;
  }

  ShapeParametric copyWith({
    ParamShapeKind? kind,
    AnimatedDouble? sizeX,
    AnimatedDouble? sizeY,
    AnimatedDouble? roundness,
    AnimatedDouble? cornerTopLeft,
    AnimatedDouble? cornerTopRight,
    AnimatedDouble? cornerBottomRight,
    AnimatedDouble? cornerBottomLeft,
    bool? roundnessPercent,
    AnimatedDouble? points,
    AnimatedDouble? outerRadius,
    AnimatedDouble? innerRadius,
    AnimatedDouble? outerRoundness,
    AnimatedDouble? innerRoundness,
    AnimatedDouble? shapeRotation,
    AnimatedDouble? startAngle,
    AnimatedDouble? sweep,
    AnimatedDouble? sectorInner,
  }) {
    return ShapeParametric(
      id: id,
      kind: kind ?? this.kind,
      sizeX: sizeX ?? this.sizeX,
      sizeY: sizeY ?? this.sizeY,
      roundness: roundness ?? this.roundness,
      cornerTopLeft: cornerTopLeft ?? this.cornerTopLeft,
      cornerTopRight: cornerTopRight ?? this.cornerTopRight,
      cornerBottomRight: cornerBottomRight ?? this.cornerBottomRight,
      cornerBottomLeft: cornerBottomLeft ?? this.cornerBottomLeft,
      roundnessPercent: roundnessPercent ?? this.roundnessPercent,
      points: points ?? this.points,
      outerRadius: outerRadius ?? this.outerRadius,
      innerRadius: innerRadius ?? this.innerRadius,
      outerRoundness: outerRoundness ?? this.outerRoundness,
      innerRoundness: innerRoundness ?? this.innerRoundness,
      shapeRotation: shapeRotation ?? this.shapeRotation,
      startAngle: startAngle ?? this.startAngle,
      sweep: sweep ?? this.sweep,
      sectorInner: sectorInner ?? this.sectorInner,
    );
  }
}

/// Trilha animavel da primitiva parametrica por nome (controller e curve
/// editor usam o mesmo mapa).
AnimatedDouble? shapeParamTrackOf(ShapeParametric s, String key) =>
    switch (key) {
      'sizeX' => s.sizeX,
      'sizeY' => s.sizeY,
      'roundness' => s.roundness,
      'cornerTopLeft' => s.cornerTopLeft ?? s.roundness,
      'cornerTopRight' => s.cornerTopRight ?? s.roundness,
      'cornerBottomRight' => s.cornerBottomRight ?? s.roundness,
      'cornerBottomLeft' => s.cornerBottomLeft ?? s.roundness,
      'points' => s.points,
      'outerRadius' => s.outerRadius,
      'innerRadius' => s.innerRadius,
      'outerRoundness' => s.outerRoundness,
      'innerRoundness' => s.innerRoundness,
      'shapeRotation' => s.shapeRotation,
      'startAngle' => s.startAngle,
      'sweep' => s.sweep,
      'sectorInner' => s.sectorInner,
      _ => null,
    };

ShapeParametric shapeParamWithTrack(
  ShapeParametric s,
  String key,
  AnimatedDouble v,
) => switch (key) {
  'sizeX' => s.copyWith(sizeX: v),
  'sizeY' => s.copyWith(sizeY: v),
  'roundness' => s.copyWith(roundness: v),
  'cornerTopLeft' => s.copyWith(cornerTopLeft: v),
  'cornerTopRight' => s.copyWith(cornerTopRight: v),
  'cornerBottomRight' => s.copyWith(cornerBottomRight: v),
  'cornerBottomLeft' => s.copyWith(cornerBottomLeft: v),
  'points' => s.copyWith(points: v),
  'outerRadius' => s.copyWith(outerRadius: v),
  'innerRadius' => s.copyWith(innerRadius: v),
  'outerRoundness' => s.copyWith(outerRoundness: v),
  'innerRoundness' => s.copyWith(innerRoundness: v),
  'shapeRotation' => s.copyWith(shapeRotation: v),
  'startAngle' => s.copyWith(startAngle: v),
  'sweep' => s.copyWith(sweep: v),
  'sectorInner' => s.copyWith(sectorInner: v),
  _ => s,
};

/// ---------------------------------------------------------------- pintura

class ShapeFill extends ShapeItem {
  ShapeFill({
    super.id,
    this.color = const Color(0xFFB97A5E),
    this.opacity = 1,
    this.evenOdd = false,
  });

  final Color color;
  final double opacity;

  /// Regra de preenchimento: false = nao-zero (padrao), true = par-impar
  /// — decide o que e "dentro" quando o caminho se cruza ou tem furo.
  final bool evenOdd;

  ShapeFill copyWith({Color? color, double? opacity, bool? evenOdd}) =>
      ShapeFill(
        id: id,
        color: color ?? this.color,
        opacity: opacity ?? this.opacity,
        evenOdd: evenOdd ?? this.evenOdd,
      );
}

class ShapeStroke extends ShapeItem {
  ShapeStroke({
    super.id,
    this.color = const Color(0xFFFFFFFF),
    AnimatedDouble? width,
    this.cap = StrokeCap.round,
    this.join = StrokeJoin.round,
    this.miterLimit = 4,
    AnimatedDouble? opacity,
    AnimatedDouble? dashLength,
    AnimatedDouble? gapLength,
    AnimatedDouble? dashOffset,
  }) : width = width ?? AnimatedDouble(12),
       opacity = opacity ?? AnimatedDouble(1),
       dashLength = dashLength ?? AnimatedDouble(0),
       gapLength = gapLength ?? AnimatedDouble(0),
       dashOffset = dashOffset ?? AnimatedDouble(0);

  final Color color;

  /// TODO NUMERO ANIMA (constituicao, regra 6): espessura, opacidade e
  /// o tracejado tem diamante e curva como qualquer outra propriedade.
  final AnimatedDouble width;
  final StrokeCap cap;
  final StrokeJoin join;
  final double miterLimit;

  /// Opacidade PROPRIA do contorno (cadeia: camada x preenchimento x
  /// contorno — "vidro com borda" = fill 20% e stroke 100%).
  final AnimatedDouble opacity;

  /// dashLength > 0 liga o tracejado.
  final AnimatedDouble dashLength;
  final AnimatedDouble gapLength;

  /// Deslocamento do tracejado ao longo do caminho — ANIMAVEL: keyframe
  /// linear da a "formiguinha" (receita 12 da spec de mascaras).
  final AnimatedDouble dashOffset;

  ShapeStroke copyWith({
    Color? color,
    AnimatedDouble? width,
    StrokeCap? cap,
    StrokeJoin? join,
    double? miterLimit,
    AnimatedDouble? opacity,
    AnimatedDouble? dashLength,
    AnimatedDouble? gapLength,
    AnimatedDouble? dashOffset,
  }) {
    return ShapeStroke(
      id: id,
      color: color ?? this.color,
      width: width ?? this.width,
      cap: cap ?? this.cap,
      join: join ?? this.join,
      miterLimit: miterLimit ?? this.miterLimit,
      opacity: opacity ?? this.opacity,
      dashLength: dashLength ?? this.dashLength,
      gapLength: gapLength ?? this.gapLength,
      dashOffset: dashOffset ?? this.dashOffset,
    );
  }
}

/// Fill em gradiente (linear ou radial) com bounds do proprio caminho.
class ShapeGradientFill extends ShapeItem {
  ShapeGradientFill({
    super.id,
    this.colorA = const Color(0xFFB8FF3D),
    this.colorB = const Color(0xFF7C62FF),
    this.angleDeg = 0,
    this.radial = false,
    this.opacity = 1,
    List<Color>? extras,
    List<double>? stops,
    this.center = Offset.zero,
    this.radiusScale = 1,
    List<Keyframe<List<Color>>>? colorFrames,
  }) : extras = List.unmodifiable(extras ?? const <Color>[]),
       stops = List.unmodifiable(stops ?? const <double>[]),
       colorFrames = List.unmodifiable(
         (colorFrames ?? const <Keyframe<List<Color>>>[])
             .map(
               (k) => Keyframe(
                 time: k.time,
                 value: List<Color>.unmodifiable(k.value),
                 ease: k.ease,
               ),
             )
             .toList()
           ..sort((a, b) => a.time.compareTo(b.time)),
       );

  /// Cores animadas por parada: preserva o gradiente vetorial na exportacao.
  final List<Keyframe<List<Color>>> colorFrames;

  List<Color> colorsAt(Duration t) {
    final valid = colorFrames
        .where((k) => k.value.length == paradas.length)
        .toList();
    if (valid.isEmpty) return paradas;
    if (t <= valid.first.time) return valid.first.value;
    for (var i = 1; i < valid.length; i++) {
      final a = valid[i - 1], b = valid[i];
      if (t < b.time) {
        final span = (b.time - a.time).inMicroseconds;
        if (span <= 0) return b.value;
        final u = a.ease.transform((t - a.time).inMicroseconds / span);
        return [
          for (var j = 0; j < a.value.length; j++)
            Color.lerp(a.value[j], b.value[j], u)!,
        ];
      }
    }
    return valid.last.value;
  }

  ShapeGradientFill withColorsAt(Duration t, List<Color> colors) => copyWith(
    colorFrames: [
      for (final k in colorFrames)
        if (k.time != t) k,
      Keyframe(time: t, value: colors),
    ],
  );

  final Color colorA;
  final Color colorB;

  /// PARADAS DO MEIO, entre [colorA] e [colorB].
  ///
  /// Duas cores nao desenham uma faixa de horizonte: azul-marinho ate
  /// ciano ate verde ate branco sao quatro paradas, e com duas o meio
  /// vira uma mistura suja que nao existe na referencia. Vazio mantem o
  /// gradiente de duas cores de sempre.
  final List<Color> extras;

  /// Posicoes explicitas das cores, incluindo as extremidades (0..1).
  /// Vazio ou invalido preserva o espacamento uniforme de projetos antigos.
  final List<double> stops;

  /// Deslocamento do centro em fracoes dos bounds, nao pixels de exportacao.
  final Offset center;
  final double radiusScale;

  List<double> get resolvedStops {
    final n = extras.length + 2;
    if (stops.length == n &&
        stops.every((v) => v.isFinite && v >= 0 && v <= 1) &&
        List.generate(n - 1, (i) => stops[i] <= stops[i + 1]).every((v) => v)) {
      return stops;
    }
    return List.generate(n, (i) => i / (n - 1));
  }

  final double angleDeg;
  final bool radial;
  final double opacity;

  /// Todas as paradas na ordem em que o pincel as usa.
  List<Color> get paradas => [colorA, ...extras, colorB];

  ShapeGradientFill copyWith({
    Color? colorA,
    Color? colorB,
    double? angleDeg,
    bool? radial,
    double? opacity,
    List<Color>? extras,
    List<double>? stops,
    Offset? center,
    double? radiusScale,
    List<Keyframe<List<Color>>>? colorFrames,
  }) {
    return ShapeGradientFill(
      id: id,
      colorA: colorA ?? this.colorA,
      colorB: colorB ?? this.colorB,
      angleDeg: angleDeg ?? this.angleDeg,
      radial: radial ?? this.radial,
      opacity: opacity ?? this.opacity,
      extras: extras ?? this.extras,
      stops: stops ?? this.stops,
      center: center ?? this.center,
      radiusScale: radiusScale ?? this.radiusScale,
      colorFrames: colorFrames ?? this.colorFrames,
    );
  }
}

/// Caminho SVG cru (icones do Iconify e afins): o icone entra como VETOR
/// editavel — Trim, Repeater, morph e gradiente funcionam normalmente.
class ShapeSvgPath extends ShapeItem {
  ShapeSvgPath({super.id, required this.pathData, this.size = 420});

  final String pathData;
  final double size;

  Path? _cache;

  Path build() => _cache ??= _buildNow();

  Path _buildNow() {
    final raw = parseSvgPathData(pathData);
    return fitPathToBox(raw, size);
  }

  ShapeSvgPath copyWith({double? size}) =>
      ShapeSvgPath(id: id, pathData: pathData, size: size ?? this.size);
}

/// ---------------------------------------------------------- caminho editavel

/// CAMINHO BEZIER ANIMAVEL — o que faltava para o motion "Apple".
///
/// As outras geometrias sao primitivas ou uma string SVG: dao para
/// desenhar, nao dao para pegar um no com o dedo nem para virar outra
/// forma com keyframe. Este item guarda um [AnimatedPath] — o mesmo
/// modelo das mascaras, que ja sabe interpolar dois caminhos igualando
/// a contagem de vertices por comprimento de arco, corrigindo o sentido
/// e alinhando o vertice inicial. Um retangulo que vira um card com
/// recortes e exatamente um keyframe deste caminho para outro.
class ShapeBezier extends ShapeItem {
  ShapeBezier({super.id, required this.path});

  final AnimatedPath path;

  Path buildAt(Duration t) => path.valueAt(t).build();

  ShapeBezier copyWith({AnimatedPath? path}) =>
      ShapeBezier(id: id, path: path ?? this.path);
}

/// Amostra um Path do Flutter como poligono de [n] cantos — o caminho de
/// volta para o que nasceu como caixa preta (anel, arco, onda, geometria
/// parametrica). Perde a curvatura exata entre os pontos; ganha um
/// caminho que se edita e se anima.
BezierPath sampleBezier(Path path, {int n = 32}) {
  final metrics = path.computeMetrics().toList();
  if (metrics.isEmpty) return BezierPath(vertices: const []);
  final m = metrics.first;
  if (m.length <= 0) return BezierPath(vertices: const []);
  final pts = <PathVertex>[];
  for (var i = 0; i < n; i++) {
    final tg = m.getTangentForOffset(m.length * i / n);
    if (tg != null) pts.add(PathVertex(p: tg.position));
  }
  return BezierPath(vertices: pts, closed: m.isClosed);
}

/// O caminho bezier equivalente a um item de geometria, no instante [t].
///
/// Primitivas com formula fechada (retangulo, elipse, estrela, coracao)
/// viram os nos EXATOS; o resto e amostrado. SVG entra pelo parser de
/// nos e e ajustado a caixa como o desenho ja fazia.
BezierPath? bezierOfShapeItem(ShapeItem item, Duration t) {
  switch (item) {
    case ShapeBezier b:
      return b.path.valueAt(t);
    case ShapePath p:
      switch (p.primitive) {
        case ShapePrimitive.rectangle:
          return BezierPath.rect(p.width, p.height);
        case ShapePrimitive.ellipse:
          return BezierPath.ellipse(p.width, p.height);
        case ShapePrimitive.star:
          return BezierPath.star(
            p.points,
            p.width / 2,
            p.width / 2 * p.innerRadiusRatio,
          );
        case ShapePrimitive.polygon:
          return BezierPath(
            vertices: [
              for (var i = 0; i < p.points; i++)
                PathVertex(
                  p: Offset(
                    math.cos(-math.pi / 2 + i * 2 * math.pi / p.points) *
                        p.width /
                        2,
                    math.sin(-math.pi / 2 + i * 2 * math.pi / p.points) *
                        p.width /
                        2,
                  ),
                ),
            ],
          );
        default:
          return sampleBezier(p.build());
      }
    case ShapeParametric p:
      // Nos EXATOS onde a formula e fechada: um retangulo tem quatro
      // cantos, nao 48 amostras — editar 48 nos para mexer num canto e
      // o oposto do que o editor de nos promete.
      final sx = math.max(0.0, p.sizeX.valueAt(t));
      final sy = math.max(0.0, p.sizeY.valueAt(t));
      final n = p.points.valueAt(t).clamp(2.0, 100.0).round();
      final rOut = math.max(0.0, p.outerRadius.valueAt(t));
      final rIn = math.max(0.0, p.innerRadius.valueAt(t));
      final rot = p.shapeRotation.valueAt(t) * math.pi / 180;
      // O raio do canto pela MESMA regra do desenho: % do menor lado ou
      // px fixo, saturando em metade do menor lado.
      final halfMin = math.min(sx, sy) / 2;
      final round = p.roundness.valueAt(t);
      final raio = (p.roundnessPercent ? round / 100 * halfMin : round).clamp(
        0.0,
        halfMin,
      );
      final pontasRetas =
          p.outerRoundness.valueAt(t).abs() < 1e-6 &&
          p.innerRoundness.valueAt(t).abs() < 1e-6;

      BezierPath girado(BezierPath b) {
        if (rot.abs() < 1e-9) return b;
        final c = math.cos(rot), sn = math.sin(rot);
        Offset g(Offset o) =>
            Offset(o.dx * c - o.dy * sn, o.dx * sn + o.dy * c);
        return BezierPath(
          closed: b.closed,
          vertices: [
            for (final v in b.vertices)
              PathVertex(
                p: g(v.p),
                inT: g(v.inT),
                outT: g(v.outT),
                corner: v.corner,
              ),
          ],
        );
      }

      switch (p.kind) {
        case ParamShapeKind.rect:
          // Canto arredondado e um quarto de circulo: uma cubica por
          // canto, oito nos. Nao precisa amostrar.
          return BezierPath.roundedRect(sx, sy, raio);
        case ParamShapeKind.ellipse:
          return BezierPath.ellipse(sx, sy);
        case ParamShapeKind.polygon when pontasRetas:
          return girado(
            BezierPath(
              vertices: [
                for (var i = 0; i < n; i++)
                  PathVertex(
                    p: Offset(
                      math.cos(-math.pi / 2 + i * 2 * math.pi / n) * rOut,
                      math.sin(-math.pi / 2 + i * 2 * math.pi / n) * rOut,
                    ),
                  ),
              ],
            ),
          );
        case ParamShapeKind.star when pontasRetas:
          return girado(BezierPath.star(n, rOut, rIn));
        default:
          return sampleBezier(p.buildAt(t), n: 48);
      }
    case ShapeSvgPath p:
      return _fitBezierToBox(svgPathToBezier(p.pathData), p.size);
    case ShapeMorph m:
      return sampleBezier(m.build(t), n: 64);
    default:
      return null;
  }
}

/// Escala e centraliza um caminho bezier numa caixa de [size] — a mesma
/// regra de [fitPathToBox], aplicada aos nos em vez de ao desenho.
BezierPath _fitBezierToBox(BezierPath path, double size) {
  if (path.isEmpty) return path;
  final b = path.build().getBounds();
  final maior = math.max(b.width, b.height);
  if (maior <= 0) return path;
  final k = size / maior;
  final c = b.center;
  return BezierPath(
    closed: path.closed,
    vertices: [
      for (final v in path.vertices)
        PathVertex(
          p: Offset((v.p.dx - c.dx) * k, (v.p.dy - c.dy) * k),
          inT: v.inT * k,
          outT: v.outT * k,
          corner: v.corner,
        ),
    ],
  );
}

/// ----------------------------------------------------------------- morph

/// Morph entre duas formas parametricas (fundamento do motion/AE):
/// os dois contornos sao REAMOSTRADOS em N pontos por comprimento de
/// arco, o sentido (winding) e igualado, o ponto inicial do destino e
/// alinhado ao da origem (menor soma de distancias) e cada ponto
/// interpola linearmente. Progresso 0..1 animavel por keyframe.
class ShapeMorph extends ShapeItem {
  ShapeMorph({
    super.id,
    required this.from,
    required this.to,
    AnimatedDouble? progress,
  }) : progress = progress ?? AnimatedDouble(0);

  final ShapePath from;
  final ShapePath to;

  /// 0 = so a forma de origem; 1 = so o destino.
  final AnimatedDouble progress;

  static const int _samples = 144;

  ShapeMorph copyWith({ShapePath? to, AnimatedDouble? progress}) => ShapeMorph(
    id: id,
    from: from,
    to: to ?? this.to,
    progress: progress ?? this.progress,
  );

  Path build(Duration t) {
    final p = progress.valueAt(t).clamp(0.0, 1.0);
    if (p <= 0.0001) return from.build();
    if (p >= 0.9999) return to.build();

    final a = _samplePoints(from.build());
    var b = _samplePoints(to.build());
    if (a.length != _samples || b.length != _samples) return from.build();

    if (_signedArea(a) * _signedArea(b) < 0) {
      b = b.reversed.toList();
    }
    b = _alignStart(a, b);

    final path = Path()
      ..moveTo(
        a[0].dx + (b[0].dx - a[0].dx) * p,
        a[0].dy + (b[0].dy - a[0].dy) * p,
      );
    for (var i = 1; i < _samples; i++) {
      path.lineTo(
        a[i].dx + (b[i].dx - a[i].dx) * p,
        a[i].dy + (b[i].dy - a[i].dy) * p,
      );
    }
    path.close();
    return path;
  }

  /// N pontos igualmente espacados no PRIMEIRO contorno (o dominante).
  static List<Offset> _samplePoints(Path path) {
    final metrics = path.computeMetrics().toList();
    if (metrics.isEmpty) return const [];
    final metric = metrics.first;
    if (metric.length <= 0) return const [];
    return [
      for (var i = 0; i < _samples; i++)
        metric.getTangentForOffset(metric.length * i / _samples)?.position ??
            Offset.zero,
    ];
  }

  static double _signedArea(List<Offset> pts) {
    var area = 0.0;
    for (var i = 0; i < pts.length; i++) {
      final j = (i + 1) % pts.length;
      area += pts[i].dx * pts[j].dy - pts[j].dx * pts[i].dy;
    }
    return area / 2;
  }

  /// Gira a lista destino para o inicio que minimiza a soma de
  /// distancias (evita o morph "torcido").
  static List<Offset> _alignStart(List<Offset> a, List<Offset> b) {
    var bestK = 0;
    var bestCost = double.infinity;
    const stride = 6; // custo amostrado: suficiente e barato
    for (var k = 0; k < _samples; k += 2) {
      var cost = 0.0;
      for (var i = 0; i < _samples; i += stride) {
        cost += (a[i] - b[(i + k) % _samples]).distanceSquared;
      }
      if (cost < bestCost) {
        bestCost = cost;
        bestK = k;
      }
    }
    if (bestK == 0) return b;
    return [for (var i = 0; i < _samples; i++) b[(i + bestK) % _samples]];
  }
}

/// -------------------------------------------------------------- operadores

/// Trim Paths: mantem só o trecho [start..end] (+offset).
///
/// [individually] (PR-M8): true = cada caminho e aparado separadamente
/// (varias linhas se desenham em cascata com um controle); false =
/// "Simultaneously": TODOS os caminhos viram um só comprimento continuo
/// e o intervalo atravessa de um para o outro.
class TrimOperator extends ShapeItem {
  TrimOperator({
    super.id,
    AnimatedDouble? start,
    AnimatedDouble? end,
    AnimatedDouble? offset,
    this.individually = true,
  }) : start = start ?? AnimatedDouble(0),
       end = end ?? AnimatedDouble(1),
       offset = offset ?? AnimatedDouble(0);

  final AnimatedDouble start;
  final AnimatedDouble end;
  final AnimatedDouble offset;
  final bool individually;

  TrimOperator copyWith({
    AnimatedDouble? start,
    AnimatedDouble? end,
    AnimatedDouble? offset,
    bool? individually,
  }) => TrimOperator(
    id: id,
    start: start ?? this.start,
    end: end ?? this.end,
    offset: offset ?? this.offset,
    individually: individually ?? this.individually,
  );

  /// Faixas [0..1] normalizadas apos offset (com wrap na emenda).
  List<(double, double)> _ranges(double s, double e, double o) {
    if (e - s >= 0.999) return const [(0.0, 1.0)];
    var lo = (s + o) % 1.0;
    var hi = (e + o) % 1.0;
    if (lo < 0) lo += 1;
    if (hi < 0) hi += 1;
    if (lo <= hi) return [(lo, hi)];
    return [(lo, 1.0), (0.0, hi)];
  }

  List<Path> apply(List<Path> paths, Duration t) {
    var s = start.valueAt(t).clamp(0.0, 1.0);
    var e = end.valueAt(t).clamp(0.0, 1.0);
    final o = offset.valueAt(t);
    if (s > e) {
      final tmp = s;
      s = e;
      e = tmp;
    }
    final ranges = _ranges(s, e, o);

    if (individually) {
      final out = <Path>[];
      for (final path in paths) {
        for (final metric in path.computeMetrics()) {
          for (final (lo, hi) in ranges) {
            if (hi <= lo) continue;
            out.add(metric.extractPath(lo * metric.length, hi * metric.length));
          }
        }
      }
      return out;
    }

    // Simultaneously: comprimento continuo atraves de todos os caminhos.
    final metrics = <PathMetric>[
      for (final path in paths) ...path.computeMetrics(),
    ];
    var total = 0.0;
    for (final m in metrics) {
      total += m.length;
    }
    if (total <= 0) return const [];
    final out = <Path>[];
    for (final (lo, hi) in ranges) {
      final gs = lo * total;
      final ge = hi * total;
      var cum = 0.0;
      for (final m in metrics) {
        final ms = math.max(gs - cum, 0.0);
        final me = math.min(ge - cum, m.length);
        if (me > ms) out.add(m.extractPath(ms, me));
        cum += m.length;
      }
    }
    return out;
  }
}

/// Repeater: N copias com deslocamento/rotacao/escala incrementais.
class RepeaterOperator extends ShapeItem {
  RepeaterOperator({
    super.id,
    this.copies = 3,
    this.dx = 160,
    this.dy = 0,
    AnimatedDouble? rotation,
    this.scaleStep = 1.0,
  }) : rotation = rotation ?? AnimatedDouble(0);

  final int copies;
  final double dx;
  final double dy;

  /// Graus por copia (animavel).
  final AnimatedDouble rotation;

  /// Fator de escala por copia (1 = igual).
  final double scaleStep;

  RepeaterOperator copyWith({
    int? copies,
    double? dx,
    double? dy,
    AnimatedDouble? rotation,
    double? scaleStep,
  }) {
    return RepeaterOperator(
      id: id,
      copies: copies ?? this.copies,
      dx: dx ?? this.dx,
      dy: dy ?? this.dy,
      rotation: rotation ?? this.rotation,
      scaleStep: scaleStep ?? this.scaleStep,
    );
  }

  List<Path> apply(List<Path> paths, Duration t) {
    final rot = rotation.valueAt(t) * math.pi / 180;
    final out = <Path>[];
    for (var c = 0; c < copies; c++) {
      final m = Matrix4Utils.compose(
        dx * c,
        dy * c,
        rot * c,
        math.pow(scaleStep, c).toDouble(),
      );
      for (final path in paths) {
        out.add(path.transform(m));
      }
    }
    return out;
  }
}

abstract final class Matrix4Utils {
  /// translate * rotate * scale como Float64List para Path.transform.
  static Float64List compose(double dx, double dy, double angle, double scale) {
    final cosA = math.cos(angle) * scale;
    final sinA = math.sin(angle) * scale;
    return Float64List.fromList([
      cosA, sinA, 0, 0, //
      -sinA, cosA, 0, 0, //
      0, 0, 1, 0, //
      dx, dy, 0, 1,
    ]);
  }
}

/// ------------------------------------------------------------- avaliacao

/// Comando de desenho resultante da avaliacao da arvore.
class ShapeDraw {
  const ShapeDraw({required this.path, required this.paint});

  final Path path;
  final Paint paint;
}

Path _dashPath(
  Path source,
  double dashLength,
  double gapLength, [
  double offset = 0,
]) {
  if (dashLength <= 0) return source;
  final cycle = dashLength + math.max(0.1, gapLength);
  final shift = -(offset % cycle);
  final out = Path();
  for (final metric in source.computeMetrics()) {
    var distance = shift;
    while (distance < metric.length) {
      final start = math.max(0.0, distance);
      final next = math.min(distance + dashLength, metric.length);
      if (next > start) {
        out.addPath(metric.extractPath(start, next), Offset.zero);
      }
      distance += cycle;
    }
  }
  return out;
}

/// DESLOCAR CAMINHO: engorda ou afina a forma andando na NORMAL de cada
/// ponto. Diferente de escalar, que afasta do centro.
class OffsetPathOperator extends ShapeItem {
  OffsetPathOperator({super.id, AnimatedDouble? amount})
    : amount = amount ?? AnimatedDouble(6);

  final AnimatedDouble amount;

  List<Path> apply(List<Path> paths, Duration t) {
    final a = amount.valueAt(t);
    return [for (final p in paths) offsetPath(p, a)];
  }

  OffsetPathOperator copyWith({AnimatedDouble? amount}) =>
      OffsetPathOperator(id: id, amount: amount ?? this.amount);
}

/// ARREDONDAR CANTOS: troca quina por arco, so onde ha quina de verdade.
class RoundCornersOperator extends ShapeItem {
  RoundCornersOperator({super.id, AnimatedDouble? radius})
    : radius = radius ?? AnimatedDouble(12);

  final AnimatedDouble radius;

  List<Path> apply(List<Path> paths, Duration t) {
    final r = radius.valueAt(t);
    return [for (final p in paths) roundCorners(p, r)];
  }

  RoundCornersOperator copyWith({AnimatedDouble? radius}) =>
      RoundCornersOperator(id: id, radius: radius ?? this.radius);
}

/// ZIG ZAG: serra ou onda ao longo do contorno.
class ZigZagOperator extends ShapeItem {
  ZigZagOperator({
    super.id,
    AnimatedDouble? amplitude,
    AnimatedDouble? ridges,
    this.smooth = false,
  }) : amplitude = amplitude ?? AnimatedDouble(10),
       ridges = ridges ?? AnimatedDouble(0.5);

  final AnimatedDouble amplitude;
  final AnimatedDouble ridges;

  /// Onda em vez de serra.
  final bool smooth;

  List<Path> apply(List<Path> paths, Duration t) {
    final a = amplitude.valueAt(t);
    final r = ridges.valueAt(t);
    return [for (final p in paths) zigZag(p, a, r, smooth: smooth)];
  }

  ZigZagOperator copyWith({
    AnimatedDouble? amplitude,
    AnimatedDouble? ridges,
    bool? smooth,
  }) => ZigZagOperator(
    id: id,
    amplitude: amplitude ?? this.amplitude,
    ridges: ridges ?? this.ridges,
    smooth: smooth ?? this.smooth,
  );
}

/// INCHAR E ENCOLHER: circulo vira flor, estrela vira bolha.
class PuckerBloatOperator extends ShapeItem {
  PuckerBloatOperator({super.id, AnimatedDouble? amount})
    : amount = amount ?? AnimatedDouble(0);

  /// Positivo incha, negativo encolhe.
  final AnimatedDouble amount;

  List<Path> apply(List<Path> paths, Duration t) {
    final a = amount.valueAt(t);
    return [for (final p in paths) puckerBloat(p, a)];
  }

  PuckerBloatOperator copyWith({AnimatedDouble? amount}) =>
      PuckerBloatOperator(id: id, amount: amount ?? this.amount);
}

/// TORCER: o centro fica parado, a borda gira.
class TwistOperator extends ShapeItem {
  TwistOperator({super.id, AnimatedDouble? angle})
    : angle = angle ?? AnimatedDouble(45);

  final AnimatedDouble angle;

  List<Path> apply(List<Path> paths, Duration t) {
    final a = angle.valueAt(t);
    return [for (final p in paths) twist(p, a)];
  }

  TwistOperator copyWith({AnimatedDouble? angle}) =>
      TwistOperator(id: id, angle: angle ?? this.angle);
}

/// BAGUNCAR O CAMINHO: ruido deterministico na normal.
class WigglePathOperator extends ShapeItem {
  WigglePathOperator({
    super.id,
    AnimatedDouble? amount,
    AnimatedDouble? detail,
    AnimatedDouble? evolution,
    this.seed = 1,
  }) : amount = amount ?? AnimatedDouble(8),
       detail = detail ?? AnimatedDouble(1),
       evolution = evolution ?? AnimatedDouble(0);

  final AnimatedDouble amount;
  final AnimatedDouble detail;

  /// Anima a bagunca sem sortear de novo.
  final AnimatedDouble evolution;
  final int seed;

  List<Path> apply(List<Path> paths, Duration t) {
    final a = amount.valueAt(t);
    final d = detail.valueAt(t);
    final e = evolution.valueAt(t);
    return [
      for (final p in paths)
        wigglePath(p, a, seed: seed, detail: d, evolution: e),
    ];
  }

  WigglePathOperator copyWith({
    AnimatedDouble? amount,
    AnimatedDouble? detail,
    AnimatedDouble? evolution,
    int? seed,
  }) => WigglePathOperator(
    id: id,
    amount: amount ?? this.amount,
    detail: detail ?? this.detail,
    evolution: evolution ?? this.evolution,
    seed: seed ?? this.seed,
  );
}

/// COMBINAR CAMINHOS: as booleanas. E o que faz furo de verdade, em vez
/// de pintar por cima com a cor do fundo.
class MergePathsOperator extends ShapeItem {
  MergePathsOperator({super.id, this.mode = MergeMode.union});

  final MergeMode mode;

  List<Path> apply(List<Path> paths, Duration t) =>
      paths.isEmpty ? paths : [mergePaths(paths, mode)];

  MergePathsOperator copyWith({MergeMode? mode}) =>
      MergePathsOperator(id: id, mode: mode ?? this.mode);
}

/// Avalia a lista de itens (de baixo para cima como no AE: operadores e
/// pinturas afetam os caminhos que vieram ANTES na lista).
List<ShapeDraw> evaluateShape(
  List<ShapeItem> items,
  Duration t, {
  double opacity = 1,
}) {
  final draws = <ShapeDraw>[];
  var paths = <Path>[];

  for (final item in items) {
    switch (item) {
      case ShapePath p:
        paths = [...paths, p.build()];
      // Geometria PARAMETRICA avaliada no tempo: Tamanho e parametro do
      // caminho — o traco pintado depois fica com a espessura intacta.
      case ShapeParametric p:
        paths = [...paths, p.buildAt(t)];
      case ShapeSvgPath p:
        paths = [...paths, p.build()];
      case ShapeMorph m:
        paths = [...paths, m.build(t)];
      case ShapeBezier b:
        paths = [...paths, b.buildAt(t)];
      case TrimOperator op:
        paths = op.apply(paths, t);
      case RepeaterOperator op:
        paths = op.apply(paths, t);
      case OffsetPathOperator op:
        paths = op.apply(paths, t);
      case RoundCornersOperator op:
        paths = op.apply(paths, t);
      case ZigZagOperator op:
        paths = op.apply(paths, t);
      case PuckerBloatOperator op:
        paths = op.apply(paths, t);
      case TwistOperator op:
        paths = op.apply(paths, t);
      case WigglePathOperator op:
        paths = op.apply(paths, t);
      case MergePathsOperator op:
        paths = op.apply(paths, t);
      case ShapeFill fill:
        for (final path in paths) {
          // Regra de preenchimento no proprio Path (furo/auto-cruzado).
          final drawn = fill.evenOdd
              ? (Path.from(path)..fillType = PathFillType.evenOdd)
              : path;
          draws.add(
            ShapeDraw(
              path: drawn,
              paint: Paint()
                ..style = PaintingStyle.fill
                ..color = fill.color.withValues(
                  alpha: fill.color.a * fill.opacity * opacity,
                ),
            ),
          );
        }
      case ShapeGradientFill g:
        for (final path in paths) {
          final b = path.getBounds();
          if (b.isEmpty) continue;
          final rad = g.angleDeg * math.pi / 180;
          final dir = Offset(math.cos(rad), math.sin(rad));
          final half = Offset(dir.dx * b.width / 2, dir.dy * b.height / 2);
          final colors = [
            for (final c in g.colorsAt(t))
              c.withValues(alpha: c.a * g.opacity * opacity),
          ];
          final stops = g.resolvedStops;
          final center =
              b.center +
              Offset(
                g.center.dx.isFinite ? g.center.dx * b.width : 0,
                g.center.dy.isFinite ? g.center.dy * b.height : 0,
              );
          final radius = g.radiusScale.isFinite
              ? g.radiusScale.clamp(0.001, 100.0)
              : 1.0;
          draws.add(
            ShapeDraw(
              path: path,
              paint: Paint()
                ..style = PaintingStyle.fill
                ..shader = g.radial
                    ? Gradient.radial(
                        center,
                        b.longestSide / 2 * radius,
                        colors,
                        stops,
                      )
                    : Gradient.linear(
                        center - half * radius,
                        center + half * radius,
                        colors,
                        stops,
                      ),
            ),
          );
        }
      case ShapeStroke stroke:
        for (final path in paths) {
          draws.add(
            ShapeDraw(
              path: _dashPath(
                path,
                stroke.dashLength.valueAt(t),
                stroke.gapLength.valueAt(t),
                stroke.dashOffset.valueAt(t),
              ),
              paint: Paint()
                ..style = PaintingStyle.stroke
                ..strokeWidth = stroke.width.valueAt(t)
                ..strokeCap = stroke.cap
                ..strokeJoin = stroke.join
                ..strokeMiterLimit = stroke.miterLimit
                ..color = stroke.color.withValues(
                  alpha:
                      stroke.color.a *
                      stroke.opacity.valueAt(t).clamp(0.0, 1.0) *
                      opacity,
                ),
            ),
          );
        }
    }
  }
  return draws;
}

/// Bounds de todos os draws (para dimensionar o widget e a selecao).
Rect shapeBounds(List<ShapeDraw> draws) {
  Rect? acc;
  for (final d in draws) {
    final b = d.path.getBounds();
    acc = acc == null ? b : acc.expandToInclude(b);
  }
  return acc ?? const Rect.fromLTWH(-210, -210, 420, 420);
}

/// Presets prontos do menu "+ Forma".
abstract final class ShapePresets {
  // ------ PARAMETRICOS (novos): Tamanho e parametro do caminho ------

  static List<ShapeItem> paramRect() => [
    ShapeParametric(
      kind: ParamShapeKind.rect,
      sizeX: AnimatedDouble(320),
      sizeY: AnimatedDouble(320),
      roundness: AnimatedDouble(12),
    ),
    ShapeFill(color: const Color(0xFF4A7BA6)),
  ];

  static List<ShapeItem> paramEllipse() => [
    ShapeParametric(
      kind: ParamShapeKind.ellipse,
      sizeX: AnimatedDouble(320),
      sizeY: AnimatedDouble(320),
    ),
    ShapeFill(),
  ];

  static List<ShapeItem> paramStar() => [
    ShapeParametric(
      kind: ParamShapeKind.star,
      outerRadius: AnimatedDouble(170),
      innerRadius: AnimatedDouble(85),
    ),
    ShapeFill(color: const Color(0xFFFFB020)),
  ];

  static List<ShapeItem> paramPolygon() => [
    ShapeParametric(
      kind: ParamShapeKind.polygon,
      points: AnimatedDouble(6),
      outerRadius: AnimatedDouble(170),
    ),
    ShapeFill(color: const Color(0xFF9C5BD1)),
  ];

  static List<ShapeItem> paramSector() => [
    ShapeParametric(
      kind: ParamShapeKind.sector,
      outerRadius: AnimatedDouble(170),
      sweep: AnimatedDouble(270),
    ),
    ShapeFill(color: const Color(0xFFE85B81)),
  ];

  static List<ShapeItem> paramRing() => [
    ShapeParametric(
      kind: ParamShapeKind.sector,
      outerRadius: AnimatedDouble(170),
      sectorInner: AnimatedDouble(110),
      sweep: AnimatedDouble(360),
    ),
    ShapeFill(color: const Color(0xFF2BE3A0)),
  ];

  // ------ legados (paths cozidos; projetos antigos continuam iguais) ---

  static List<ShapeItem> circle() => [
    ShapePath(primitive: ShapePrimitive.ellipse),
    ShapeFill(),
  ];

  static List<ShapeItem> roundedRect() => [
    ShapePath(primitive: ShapePrimitive.roundedRectangle),
    ShapeFill(color: const Color(0xFF4A7BA6)),
  ];

  static List<ShapeItem> star() => [
    ShapePath(primitive: ShapePrimitive.star),
    ShapeFill(color: const Color(0xFFFFB020)),
  ];

  static List<ShapeItem> ring() => [
    ShapePath(primitive: ShapePrimitive.ring),
    ShapeFill(color: const Color(0xFF2BE3A0)),
  ];

  static List<ShapeItem> arc() => [
    ShapePath(primitive: ShapePrimitive.arc),
    ShapeFill(color: const Color(0xFF7C62FF)),
  ];

  static List<ShapeItem> wave() => [
    ShapePath(primitive: ShapePrimitive.wave, width: 640),
    ShapeStroke(color: const Color(0xFF35C4E7), width: AnimatedDouble(16)),
  ];

  static List<ShapeItem> polygon() => [
    ShapePath(primitive: ShapePrimitive.polygon, points: 6),
    ShapeFill(color: const Color(0xFF35C4E7)),
  ];

  static List<ShapeItem> heart() => [
    ShapePath(primitive: ShapePrimitive.heart),
    ShapeFill(color: const Color(0xFFFF3B52)),
  ];

  static List<ShapeItem> gear() => [
    ShapePath(primitive: ShapePrimitive.gear, points: 8),
    ShapeFill(color: const Color(0xFF8A97AD)),
  ];

  static List<ShapeItem> arrow() => [
    ShapePath(primitive: ShapePrimitive.arrow, width: 520, height: 300),
    ShapeFill(color: const Color(0xFFB8FF3D)),
  ];

  static List<ShapeItem> check() => [
    ShapePath(primitive: ShapePrimitive.check),
    ShapeFill(color: const Color(0xFF2BE3A0)),
  ];

  static List<ShapeItem> drop() => [
    ShapePath(primitive: ShapePrimitive.drop, width: 320, height: 440),
    ShapeGradientFill(
      colorA: const Color(0xFF35C4E7),
      colorB: const Color(0xFF7C62FF),
      angleDeg: 90,
    ),
  ];

  static List<ShapeItem> flower() => [
    ShapePath(primitive: ShapePrimitive.flower, points: 6),
    ShapeFill(color: const Color(0xFFFF5C77)),
  ];

  static List<ShapeItem> sparkle() => [
    ShapePath(primitive: ShapePrimitive.sparkle),
    ShapeFill(color: const Color(0xFFFFFFFF)),
  ];
}
