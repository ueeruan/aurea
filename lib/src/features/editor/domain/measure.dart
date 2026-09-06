import 'dart:ui';

import 'package:flutter/widgets.dart'
    show FontWeight, TextDirection, TextPainter, TextSpan, TextStyle;

import 'layer.dart';
import 'shape.dart';

/// Caixa RENDERIZADA de uma camada, em px logicos. E a medida que
/// alinhamento, distribuicao, forma-conteiner e empilhamento usam — por
/// isso vive num lugar so, e nao copiada em cada um.
Size measureLayerBox(Layer layer, Duration local,
    {double fallbackWidth = 1080}) {
  final sx = layer.scaleX.valueAt(local).abs();
  final sy = layer.scaleY.valueAt(local).abs();
  final base = switch (layer) {
    ShapeLayer l => shapeBounds(evaluateShape(l.contents, local)).size,
    Element3DLayer l => Size(l.size * 2, l.size * 2),
    ParticlesLayer _ => const Size(420, 420),
    TextLayer l => measureText(l.text, l.fontSize, l.bold),
    NullLayer _ => Size.zero,
    AudioLayer _ => Size.zero,
    AdjustmentLayer _ => const Size(220, 220),
    CaptionLayer l => measureText(
        l.cueAt(local)?.text ?? '', l.style.fontSize, l.style.bold),
    _ => Size(fallbackWidth, fallbackWidth * 9 / 16),
  };
  return Size(base.width * sx, base.height * sy);
}

/// Caixa do texto medida de verdade (nao estimada).
Size measureText(String text, double fontSize, bool bold) {
  if (text.isEmpty) return Size(0, fontSize * 1.2);
  final painter = TextPainter(
    text: TextSpan(
      text: text,
      style: TextStyle(
        fontSize: fontSize,
        fontWeight: bold ? FontWeight.w700 : FontWeight.w400,
        height: 1.2,
      ),
    ),
    textDirection: TextDirection.ltr,
  )..layout();
  return painter.size;
}
