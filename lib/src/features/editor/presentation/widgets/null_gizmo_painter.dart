import 'dart:math' as math;

import 'package:flutter/material.dart';

/// O GIZMO DO OBJETO NULO — o quadrado tracejado com o X.
///
/// E uma AJUDA de edicao: existe para se ver o que se esta arrastando. O
/// desenho nao pergunta se esta exportando — quem pergunta e quem o monta
/// (ver o `NullLayer` em `preview_stage.dart`), porque um nulo nao tem
/// pixel nenhum para dar e o gizmo saia no video entregue.
class NullGizmoPainter extends CustomPainter {
  const NullGizmoPainter({this.color = const Color(0xFF9F8CFF)});

  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final rect = (Offset.zero & size).deflate(3);
    final stroke = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 3
      ..color = color.withValues(alpha: 0.9);

    void tracejado(Offset a, Offset b) {
      const dash = 14.0, gap = 9.0;
      final delta = b - a;
      final len = delta.distance;
      if (len <= 0) return;
      final dir = delta / len;
      var d = 0.0;
      while (d < len) {
        final e = math.min(d + dash, len);
        canvas.drawLine(a + dir * d, a + dir * e, stroke);
        d = e + gap;
      }
    }

    tracejado(rect.topLeft, rect.topRight);
    tracejado(rect.topRight, rect.bottomRight);
    tracejado(rect.bottomRight, rect.bottomLeft);
    tracejado(rect.bottomLeft, rect.topLeft);

    final cruz = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2.5
      ..color = color.withValues(alpha: 0.65);
    final c = rect.center;
    canvas.drawLine(c - const Offset(26, 0), c + const Offset(26, 0), cruz);
    canvas.drawLine(c - const Offset(0, 26), c + const Offset(0, 26), cruz);
  }

  @override
  bool shouldRepaint(NullGizmoPainter old) => old.color != color;
}
