import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/presentation/widgets/fx_lote2.dart';

/// Pintor que so anota qual foto recebeu.
class _Espiao extends SnapshotPainter {
  final fotos = <ui.Image>[];

  @override
  void paint(PaintingContext context, Offset offset, Size size,
      PaintingContextCallback painter) {
    painter(context, offset);
  }

  @override
  void paintSnapshot(PaintingContext context, Offset offset, Size size,
      ui.Image image, Size sourceSize, double pixelRatio) {
    fotos.add(image);
    context.canvas.drawImageRect(
        image, Offset.zero & sourceSize, offset & size, Paint());
  }

  @override
  bool shouldRepaint(covariant SnapshotPainter old) => true;
}

void main() {
  testWidgets('a foto do efeito vence quando a camada e reconstruida',
      (tester) async {
    // POR QUE ESTE TESTE EXISTE: o SnapshotWidget guarda o raster no
    // objeto de render e nao o descarta quando o filho muda — o filho e
    // pintado num layer proprio, entao o markNeedsPaint dele nao alcanca
    // a foto. Sem invalidar na mao, TODA camada com efeito de imagem
    // congelava no primeiro quadro: dar play e nada andar, apagar e
    // continuar na tela.
    final espiao = _Espiao();

    Future<void> mostrar(Color cor) => tester.pumpWidget(
          Directionality(
            textDirection: TextDirection.ltr,
            child: Center(
              child: SizedBox(
                width: 40,
                height: 40,
                child: FxSnapshot(
                  painter: espiao,
                  child: ColoredBox(color: cor),
                ),
              ),
            ),
          ),
        );

    await mostrar(const Color(0xFFFF0000));
    await mostrar(const Color(0xFF00FF00));
    await mostrar(const Color(0xFF0000FF));

    expect(espiao.fotos.length, greaterThanOrEqualTo(3),
        reason: 'o pintor tem de receber uma foto a cada reconstrucao');
    expect(identical(espiao.fotos[0], espiao.fotos[1]), isFalse,
        reason: 'repetir a mesma foto e a camada congelada');
    expect(identical(espiao.fotos[1], espiao.fotos[2]), isFalse,
        reason: 'repetir a mesma foto e a camada congelada');
  });
}
