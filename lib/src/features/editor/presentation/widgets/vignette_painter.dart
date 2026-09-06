import 'dart:math' as math;

import 'package:flutter/rendering.dart';

/// A VINHETA (nivel 3): escurece a borda do quadro.
///
/// Feita a mao, e nao com um degrade radial pronto, por tres motivos que
/// o gradiente nao entrega: **forma** (circulo ou retangulo — a queda
/// retangular acompanha o quadro em vez de virar uma elipse deitada),
/// **centro** deslocavel, e uma **curva de queda** que se controla de
/// verdade (a suavidade muda o expoente, nao so a parada do degrade).
///
/// Neutralidade: `amount` 0 nao chega aqui — quem chama pula o efeito.
class VignettePainter extends CustomPainter {
  const VignettePainter({
    required this.amount,
    required this.radius,
    required this.softness,
    required this.color,
    this.retangular = false,
    this.center = const Offset(0.5, 0.5),
  });

  /// Quanto escurece na borda, 0..1.
  final double amount;

  /// Onde a queda comeca, em fracao do meio-quadro.
  final double radius;

  /// 0 = corte duro; 1 = queda longa.
  final double softness;

  final Color color;

  /// Circulo (distancia euclidiana) ou retangulo (Chebyshev).
  final bool retangular;

  /// Centro em fracao do quadro.
  final Offset center;

  @override
  void paint(Canvas canvas, Size size) {
    if (amount <= 0.001 || size.isEmpty) return;
    final c = Offset(center.dx * size.width, center.dy * size.height);
    // A distancia e medida contra o CANTO mais longe do centro: assim a
    // vinheta cobre o quadro inteiro mesmo com o centro deslocado.
    final ex = math.max(c.dx, size.width - c.dx);
    final ey = math.max(c.dy, size.height - c.dy);
    final r = math.max(1.0, radius) ;

    // Passos do degrade: a queda e calculada, o Canvas so interpola
    // entre paradas proximas (32 e imperceptivel e barato).
    const passos = 32;
    final paradas = <double>[];
    final cores = <Color>[];
    // Expoente: suavidade 0 => queda quase reta na borda; 1 => longa.
    final expoente = 1 + (1 - softness) * 5;
    final inicio = (1 - softness).clamp(0.0, 0.98) * 0.55;
    for (var i = 0; i <= passos; i++) {
      final t = i / passos;
      paradas.add(t);
      // t e a fracao do raio ate o canto; abaixo de `inicio` nao escurece.
      final u = ((t - inicio) / (1 - inicio)).clamp(0.0, 1.0);
      final a = math.pow(u, expoente).toDouble() * amount;
      cores.add(color.withValues(alpha: a.clamp(0.0, 1.0)));
    }

    final paint = Paint();
    if (retangular) {
      // Chebyshev: a queda segue o retangulo do quadro. Como o Canvas
      // nao tem degrade "de caixa", pintamos aneis retangulares.
      for (var i = passos; i >= 1; i--) {
        final t = paradas[i];
        final w = ex * r * t, h = ey * r * t;
        paint.color = cores[i];
        canvas.drawDRRect(
          RRect.fromRectAndRadius(
              Rect.fromLTRB(0, 0, size.width, size.height), Radius.zero),
          RRect.fromRectAndRadius(
              Rect.fromCenter(center: c, width: w * 2, height: h * 2),
              Radius.zero),
          paint,
        );
      }
      return;
    }

    // Circulo (elipse do quadro): um degrade radial com a curva pronta.
    final raio = math.max(ex, ey) * r;
    paint.shader = RadialGradient(
      center: Alignment(center.dx * 2 - 1, center.dy * 2 - 1),
      radius: raio / (math.max(size.width, size.height) / 2),
      colors: cores,
      stops: paradas,
    ).createShader(Offset.zero & size);
    canvas.drawRect(Offset.zero & size, paint);
  }

  @override
  bool shouldRepaint(VignettePainter old) =>
      old.amount != amount ||
      old.radius != radius ||
      old.softness != softness ||
      old.color != color ||
      old.retangular != retangular ||
      old.center != center;
}
