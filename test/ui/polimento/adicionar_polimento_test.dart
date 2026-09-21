// POLIMENTO DO "+":
//  5. o Solido 3D nascia de frente e chapado (um quadrado 2D);
//  7. na aba 3D os blocos ficavam soltos embaixo, com um vao vazio acima.
import 'dart:ui' as ui;

import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/adicionar_acoes.dart';
import 'package:aurea/src/features/editor/presentation/widgets/element3d_painter.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import '../paineis/apoio_editor.dart';

/// Quantos TONS diferentes o solido mostra: pinta o solido num quadro de
/// 400, agrupa os pixels do objeto pelo brilho (degraus de 12) e conta os
/// grupos com pelo menos 5% da area — as faces, e nao as arestas.
Future<int> _tonsDoSolido(
  WidgetTester tester,
  Element3DLayer l, {
  required double rotX,
  required double rotY,
}) async {
  const lado = 400;
  const fundo = Color(0xFF000000);
  late List<int> contagem;
  await tester.runAsync(() async {
    final gravador = ui.PictureRecorder();
    final canvas = Canvas(gravador)
      ..drawRect(
        const Rect.fromLTWH(0, 0, lado + 0.0, lado + 0.0),
        Paint()..color = fundo,
      );
    Element3DPainter(
      layer: l,
      rotXDeg: rotX,
      rotYDeg: rotY,
    ).paint(canvas, const Size(lado + 0.0, lado + 0.0));
    final imagem = await gravador.endRecording().toImage(lado, lado);
    final bytes = (await imagem.toByteData())!;
    imagem.dispose();
    contagem = List.filled(256 ~/ 12 + 1, 0);
    for (var i = 0; i < bytes.lengthInBytes; i += 4) {
      final r = bytes.getUint8(i), g = bytes.getUint8(i + 1);
      final b = bytes.getUint8(i + 2);
      if (r == 0 && g == 0 && b == 0) continue;
      final luz = (0.299 * r + 0.587 * g + 0.114 * b).round();
      contagem[luz ~/ 12]++;
    }
  });
  final total = contagem.fold<int>(0, (a, b) => a + b);
  return contagem.where((n) => n >= total * .05).length;
}

void main() {
  testWidgets('aba 3D: os blocos comecam no topo, sem vao em cima', (
    tester,
  ) async {
    await abrirEditorInteiro(tester);
    await tester.tap(find.byKey(const ValueKey('editor-adicionar')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('adicionar-aba-3d')));
    await tester.pumpAndSettle();
    final abas = tester.getRect(find.byKey(const ValueKey('adicionar-aba-3d')));
    final primeiro = tester.getRect(
      find.byKey(const ValueKey('adicionar-3d-aparelho')),
    );
    // O respiro de cima da grade (10), e nao metade da sobra da folha.
    expect(primeiro.top - abas.bottom, closeTo(AureaDims.e10, 1));
    expect(tester.takeException(), isNull);
  });

  testWidgets('Solido 3D nasce girado (volume a vista), com sombreado nas '
      'faces, e criar + girar e UM desfazer', (tester) async {
    final c = await abrirEditorInteiro(tester);
    await tester.tap(find.byKey(const ValueKey('editor-adicionar')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('adicionar-aba-3d')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('adicionar-3d-solido')));
    await tester.pumpAndSettle();

    final solido = c
        .read(editorControllerProvider)
        .layers
        .whereType<Element3DLayer>()
        .single;
    expect(c.read(selectedLayerProvider), solido.id);
    const giro = giroInicialDoSolido3D;
    expect(solido.rotationX.valueAt(Duration.zero), giro.x);
    expect(solido.rotationY.valueAt(Duration.zero), giro.y);
    expect(giro.x, isNot(0));
    expect(giro.y, isNot(0));
    // O giro e a POSE de nascenca, e nao uma animacao.
    expect(solido.rotationX.isAnimated, isFalse);
    expect(solido.rotationY.isAnimated, isFalse);

    // A LUZ DO PINTOR DA VOLUME: tres faces, tres tons. De frente, um so.
    final girado = await _tonsDoSolido(
      tester,
      solido,
      rotX: giro.x,
      rotY: giro.y,
    );
    final deFrente = await _tonsDoSolido(tester, solido, rotX: 0, rotY: 0);
    expect(deFrente, 1, reason: 'a medida nao separa as faces');
    expect(girado, greaterThanOrEqualTo(3), reason: 'faces sem sombreado');

    // UM desfazer tira o solido inteiro (e nao so o giro).
    c.read(editorControllerProvider.notifier).undo();
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).layers, isEmpty);
    expect(tester.takeException(), isNull);
  });
}
