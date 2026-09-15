import 'dart:math' as math;

import 'package:flutter/widgets.dart';

/// The output rectangle in screen coordinates, shared by 2D and 3D.
/// Padding belongs to the editor, never to the exported composition.
Rect compositionRect(Size viewport, Size output, {double inset = 8}) {
  if (!viewport.isFinite ||
      viewport.isEmpty ||
      !output.isFinite ||
      output.isEmpty) {
    return Rect.zero;
  }
  final padding = math.min(
    inset,
    math.min(viewport.width, viewport.height) / 4,
  );
  final scale = math.min(
    (viewport.width - 2 * padding) / output.width,
    (viewport.height - 2 * padding) / output.height,
  );
  return Rect.fromCenter(
    center: viewport.center(Offset.zero),
    width: output.width * scale,
    height: output.height * scale,
  );
}

/// One screen-pixel outline remains legible for black/transparent footage.
class CompositionFrame extends StatelessWidget {
  const CompositionFrame({
    super.key,
    required this.child,
    this.safeAreas = false,
  });
  final Widget child;
  final bool safeAreas;

  @override
  Widget build(BuildContext context) => CustomPaint(
    foregroundPainter: CompositionFramePainter(safeAreas: safeAreas),
    child: ClipRect(child: child),
  );
}

class CompositionFramePainter extends CustomPainter {
  const CompositionFramePainter({this.safeAreas = false});
  final bool safeAreas;
  @override
  void paint(Canvas canvas, Size size) {
    if (size.isEmpty) return;
    final stroke = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1
      ..color = const Color(0x998B94A3);
    canvas.drawRect((Offset.zero & size).deflate(.5), stroke);
    if (safeAreas) {
      stroke.color = const Color(0x66FFFFFF);
      for (final fraction in [.8, .9]) {
        canvas.drawRect(
          Rect.fromCenter(
            center: size.center(Offset.zero),
            width: size.width * fraction,
            height: size.height * fraction,
          ),
          stroke,
        );
      }
    }
  }

  @override
  bool shouldRepaint(CompositionFramePainter old) => old.safeAreas != safeAreas;
}
