import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/transformar.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_paineis.dart';

/// Toca na sub-aba [i] (a fileira rola de lado: a aba pode estar fora
/// da tela).
Future<void> _aba(WidgetTester tester, int i) async {
  final f = find.byKey(ValueKey('painel-transformar-aba-$i'));
  final fileira = find.descendant(
    of: find.byType(AureaTabs),
    matching: find.byType(Scrollable),
  );
  if (f.evaluate().isEmpty) {
    // Volta ao comeco da fileira e procura andando para a frente.
    await tester.drag(fileira, const Offset(2000, 0));
    await tester.pumpAndSettle();
    await tester.scrollUntilVisible(f, 60, scrollable: fileira);
    await tester.pumpAndSettle();
  }
  await tester.tap(f);
  await tester.pumpAndSettle();
}

void main() {
  group('painel Transformar', () {
    testWidgets('arrastar a caixa X da Posicao move a camada para a direita, '
        'e o losango crava a marca no cabecote', (tester) async {
      final (b, id) = await montarPainel(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Forma');
          return c.projetoCompleto.layers.single.id;
        },
        painel: (id) => PainelTransformar(layerId: id),
      );
      final antes = b.camada(id).position.valueAt(Duration.zero);
      expect(find.byKey(const ValueKey('valor-posicao-x')), findsOneWidget);

      await arrastarEmPassos(
        tester,
        find.byKey(const ValueKey('valor-posicao-x')),
        cada: const Offset(10, 0),
      );
      final depois = b.camada(id).position.valueAt(Duration.zero);
      expect(depois.dx, greaterThan(antes.dx + 50), reason: 'direita aumenta');
      expect(depois.dy, closeTo(antes.dy, 1e-9), reason: 'Y fica');
      // O arrasto inteiro e UM passo de desfazer.
      b.c.undo();
      expect(b.camada(id).position.valueAt(Duration.zero), antes);
      b.c.redo();

      // Sem marca, o losango esta apagado; o toque crava a marca em 0.
      expect(b.camada(id).position.isAnimated, isFalse);
      await tester.tap(find.byKey(const ValueKey('kf-posicao')));
      await tester.pumpAndSettle();
      final l = b.camada(id);
      expect(l.position.isAnimated, isTrue);
      expect(l.positionTimesUs, {0});
      expect(tester.takeException(), isNull);
    });

    testWidgets('trocar de aba muda a propriedade ativa (a timeline acende '
        'as marcas dela)', (tester) async {
      final (b, _) = await montarPainel(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Forma');
          return c.projetoCompleto.layers.single.id;
        },
        painel: (id) => PainelTransformar(layerId: id),
      );
      PropriedadeAtiva? ativa() => b.container.read(propriedadeAtivaProvider);
      expect(ativa(), const PropriedadeAtiva.transformacao(LayerProp.position));

      final esperado = {
        1: LayerProp.scale,
        2: LayerProp.rotation,
        3: LayerProp.opacity,
        4: LayerProp.skew,
        5: LayerProp.pivot,
        0: LayerProp.position,
      };
      for (final MapEntry(key: aba, value: prop) in esperado.entries) {
        await _aba(tester, aba);
        expect(
          ativa(),
          PropriedadeAtiva.transformacao(prop),
          reason: 'aba $aba',
        );
      }
      // A aba Rotacao mostra X e Y so com a camada em 3D.
      await _aba(tester, 2);
      expect(find.byKey(const ValueKey('prop-rotacao-x')), findsNothing);
      await tester.tap(find.byKey(const ValueKey('transformar-3d')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('prop-rotacao-x')), findsOneWidget);
      expect(find.byKey(const ValueKey('prop-rotacao-y')), findsOneWidget);

      // Fechar o painel devolve a timeline a "todas as propriedades".
      await tester.pumpWidget(const SizedBox());
      await tester.pumpAndSettle();
      expect(ativa(), isNull);
    });
  });
}
