import 'package:flutter/material.dart';

import '../theme/app_theme.dart';

/// Logo do app desenhado em vetor (mesma geometria do SVG original, base 108).
class AureaLogo extends StatelessWidget {
  const AureaLogo({
    super.key,
    this.size = 48,
    this.withBackground = true,
    this.borderRadius,
  });

  final double size;
  final bool withBackground;
  final BorderRadius? borderRadius;

  @override
  Widget build(BuildContext context) {
    final logo = CustomPaint(
      size: Size.square(size),
      painter: _AureaLogoPainter(withBackground: withBackground),
    );
    if (!withBackground) return logo;
    return ClipRRect(
      borderRadius: borderRadius ?? BorderRadius.circular(size * 0.22),
      child: logo,
    );
  }
}

class _AureaLogoPainter extends CustomPainter {
  const _AureaLogoPainter({required this.withBackground});

  final bool withBackground;

  @override
  void paint(Canvas canvas, Size size) {
    final s = size.width / 108.0;
    canvas.scale(s, s);

    if (withBackground) {
      canvas.drawRect(
        const Rect.fromLTWH(0, 0, 108, 108),
        Paint()..color = AppColors.background,
      );
    }

    final stroke = Path()
      ..moveTo(26, 77)
      ..cubicTo(26, 45, 39, 29, 62, 29)
      ..cubicTo(77, 29, 86, 37, 86, 49)
      ..cubicTo(86, 61, 76, 68, 59, 68)
      ..lineTo(44, 68);

    canvas.drawPath(
      stroke,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 9
        ..strokeCap = StrokeCap.round
        ..strokeJoin = StrokeJoin.round
        ..color = AppColors.lime,
    );

    canvas.drawCircle(
      const Offset(87, 76),
      8,
      Paint()..color = AppColors.violet,
    );
  }

  @override
  bool shouldRepaint(_AureaLogoPainter oldDelegate) =>
      oldDelegate.withBackground != withBackground;
}
