// "DEIXE O APP MAIS FACIL DE USAR" + mapa do agrupamento (14/09/2026).
//
// A selecao multipla so existia no toque longo PARADO numa barra; o botao
// Agrupar ficava escondido atras dele. O modo Selecionar, na regua da
// timeline, faz o toque simples marcar e desmarcar — como no Alight
// Motion — e o Agrupar aparece com duas camadas.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

void main() {
  test('alternar marca, desmarca e tira ate a camada principal', () {
    var r = alternarNaSelecao(const {}, null, 'a');
    expect(r.principal, 'a');
    expect(r.multi, isEmpty, reason: 'uma so e selecao simples');

    r = alternarNaSelecao(r.multi, r.principal, 'b');
    expect(r.multi, {'a', 'b'});
    expect(r.principal, 'a');

    // Tocar de novo na principal a tira do conjunto.
    r = alternarNaSelecao(r.multi, r.principal, 'a');
    expect(r.multi, isEmpty);
    expect(r.principal, 'b');

    r = alternarNaSelecao(r.multi, r.principal, 'b');
    expect(r.principal, isNull);
  });

  testWidgets('modo Selecionar: dois toques nas barras e Agrupar', (tester) async {
    final c = await openEditor(tester);
    final ids = [for (final l in c.read(editorControllerProvider).layers) l.id];
    expect(ids, hasLength(2));

    await tester.tap(find.byKey(const ValueKey('timeline-selecionar')));
    await tester.pumpAndSettle();
    expect(c.read(modoSelecionarProvider), isTrue);

    for (final id in ids) {
      // PERTO DA BORDA ESQUERDA do clipe, e nao no centro: o clipe e
      // largo, e com o palco no tamanho da planta o centro dele cai
      // debaixo do botao "+" flutuante, que intercepta o toque. O dedo
      // de verdade tem a barra inteira para acertar.
      // O PRIMEIRO TOQUE ABRE O PAINEL, e o painel come o pe da linha
      // do tempo — com o palco no tamanho da planta, a segunda barra
      // pode ficar debaixo dele. Traz para a vista antes de tocar,
      // como o dedo faria, e mira um terco para dentro (a beirada e a
      // alca de aparar, o canto e o "+").
      final alvo = find.byKey(ValueKey('clip-content-$id'));
      await tester.ensureVisible(alvo);
      await tester.pumpAndSettle();
      final r = tester.getRect(alvo);
      await tester.tapAt(Offset(r.left + r.width / 3, r.center.dy));
      await tester.pumpAndSettle();
    }
    expect(c.read(multiSelectProvider), ids.toSet());
    expect(find.byKey(const ValueKey('selecao-agrupar')), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('selecao-agrupar')));
    await tester.pumpAndSettle();
    final camadas = c.read(editorControllerProvider).layers;
    expect(camadas, hasLength(1));
    final grupo = camadas.single as GroupLayer;
    expect(grupo.children.map((l) => l.id).toSet(), ids.toSet());
    expect(c.read(selectedLayerProvider), grupo.id);
  });
}
