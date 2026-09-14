import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layout_ops.dart';
import 'package:aurea/src/features/editor/domain/measure.dart';
import 'package:aurea/src/features/editor/presentation/widgets/composition_frame.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// A CAIXA DO GRUPO NO PALCO.
///
/// O grupo caia no retangulo generico 16:9 da largura da composicao: num
/// projeto vertical as alcas nao tinham nada a ver com o conteudo, o grupo
/// "roubava" o toque do meio do quadro mesmo com os filhos num canto, e
/// girava em torno do centro do quadro. Agora a caixa envolve os filhos e
/// o pivo nasce no centro deles, como no Alight Motion.
TextLayer _texto(String t, Offset pos, {double rot = 0, double escala = 1}) => TextLayer(
  name: t,
  startTime: Duration.zero,
  duration: const Duration(seconds: 3),
  text: t,
  fontSize: 80,
  position: AnimatedOffset(pos),
  rotation: AnimatedDouble(rot),
  scaleX: AnimatedDouble(escala),
  scaleY: AnimatedDouble(escala),
);

void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  group('conta', () {
    test('uniao dos filhos no espaco do grupo, com giro, escala e aninhamento', () {
      const cw = 1080.0, ch = 1920.0;
      final a = _texto('AB', const Offset(200, 300));
      final b = _texto('CD', const Offset(700, 1500));
      final g = GroupLayer(
        name: 'g',
        startTime: Duration.zero,
        duration: const Duration(seconds: 3),
        position: AnimatedOffset(const Offset(cw / 2, ch / 2)),
        children: [a, b],
      );
      final r = groupContentRect(g, Duration.zero, compWidth: cw, compHeight: ch)!;
      final sa = measureLayerBox(a, Duration.zero, fallbackWidth: cw, scaled: false);
      final sb = measureLayerBox(b, Duration.zero, fallbackWidth: cw, scaled: false);
      // Espaco do grupo: coordenada da composicao menos o centro.
      expect(r.left, closeTo(200 - sa.width / 2 - cw / 2, 1e-6));
      expect(r.top, closeTo(300 - sa.height / 2 - ch / 2, 1e-6));
      expect(r.right, closeTo(700 + sb.width / 2 - cw / 2, 1e-6));
      expect(r.bottom, closeTo(1500 + sb.height / 2 - ch / 2, 1e-6));

      // Girado 90 graus e dobrado: a caixa troca os lados e dobra.
      final girado = _texto('AB', const Offset(540, 960), rot: 90, escala: 2);
      final rg = groupContentRect(
        g.copyLayer(children: [girado]),
        Duration.zero,
        compWidth: cw,
        compHeight: ch,
      )!;
      expect(rg.width, closeTo(sa.height * 2, 1e-6));
      expect(rg.height, closeTo(sa.width * 2, 1e-6));
      expect(rg.center.dx, closeTo(0, 1e-6));

      // Grupo dentro de grupo: a caixa de dentro entra como um filho.
      final dentro = g.copyLayer(children: [a]);
      final fora = g.copyLayer(children: [dentro, b]);
      expect(groupContentRect(fora, Duration.zero, compWidth: cw, compHeight: ch), r);

      // Filho fora do tempo nao conta; grupo vazio nao tem caixa.
      final tarde = b.copyLayer(startTime: const Duration(seconds: 2));
      final so = groupContentRect(
        g.copyLayer(children: [a, tarde]),
        Duration.zero,
        compWidth: cw,
        compHeight: ch,
      )!;
      expect(so.right, closeTo(200 + sa.width / 2 - cw / 2, 1e-6));
      expect(groupContentRect(g.copyLayer(children: []), Duration.zero, compWidth: cw, compHeight: ch), isNull);
    });
  });

  testWidgets('o grupo pega o toque so onde tem filho, gira em torno deles e alinha pelo conteudo', (tester) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    final p = c.read(editorControllerProvider);
    final compW = p.outputWidth.toDouble(), compH = p.outputHeight.toDouble();
    // As duas formas vao para o canto de cima a esquerda.
    e.openProject(
      p.copyWith(
        layers: [
          for (final (i, l) in p.layers.indexed)
            l.copyLayer(position: AnimatedOffset(Offset(compW * .22 + i * 30, compH * .18))),
        ],
      ),
    );
    e.groupLayers([for (final l in c.read(editorControllerProvider).layers) l.id]);
    c.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpAndSettle();
    final grupo = c.read(editorControllerProvider).layers.single as GroupLayer;
    final caixa = e.layerBoxRect(grupo, Duration.zero, scaled: false);
    final centroDoQuadro = Offset(compW / 2, compH / 2);
    final posicao = grupo.position.valueAt(Duration.zero);

    // O pivo nasce no centro dos filhos; nada se move ao agrupar.
    expect(grupo.pivot.valueAt(Duration.zero), caixa.center);

    // Premissa: o meio do quadro fica FORA do conteudo (antes a caixa do
    // grupo era o 16:9 da largura inteira, centrado ali, e roubava o toque).
    expect(caixa.shift(posicao).contains(centroDoQuadro), isFalse);
    expect(e.layerBoxSize(grupo, Duration.zero, scaled: false), caixa.size);
    expect(caixa.size, isNot(Size(compW, compW * 9 / 16)));

    final palco = tester.getRect(find.byType(PreviewStage));
    final quadro = compositionRect(palco.size, Size(compW, compH)).shift(palco.topLeft);
    Offset noPalco(Offset q) => quadro.topLeft + q * (quadro.width / compW);

    await tester.tapAt(noPalco(centroDoQuadro));
    await tester.pumpAndSettle();
    expect(c.read(selectedLayerProvider), isNull, reason: 'o vazio no meio do quadro nao pega o grupo');

    await tester.tapAt(noPalco(posicao + caixa.center));
    await tester.pumpAndSettle();
    expect(c.read(selectedLayerProvider), grupo.id, reason: 'tocar no conteudo pega o grupo');

    // As alcas ficam nos cantos do conteudo, e nao nos da composicao.
    final alca = tester.getRect(find.byKey(const ValueKey('alca-escala'))).center;
    expect((alca - noPalco(posicao + caixa.bottomRight)).distance, lessThan(36));

    // Alinhar a esquerda encosta o CONTEUDO na borda, e nao a posicao.
    e.alignSelection([grupo.id], AlignEdge.left, Duration.zero);
    final alinhado = c.read(editorControllerProvider).layers.single as GroupLayer;
    final esquerda = alinhado.position.valueAt(Duration.zero).dx +
        e.layerBoxRect(alinhado, Duration.zero).left;
    expect(esquerda, closeTo(0, 1e-6));
  });
}
