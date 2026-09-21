import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/efeitos.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_paineis.dart';

/// Dois efeitos de cor diferentes, na ordem em que entram.
final _tipos = effectsInCategory('Color').take(2).toList();

void main() {
  group('cartao de efeito', () {
    testWidgets('expandir, desligar, reordenar pela alca (um desfazer) e '
        'apagar pelo ⋯', (tester) async {
      final (b, id) = await montarPainel(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Forma');
          final id = c.projetoCompleto.layers.single.id;
          for (final t in _tipos) {
            c.addEffect(id, t);
          }
          return id;
        },
        painel: (id) => PainelEfeitos(layerId: id),
      );
      List<EffectInstance> efeitos() => b.camada(id).effects;
      final primeiro = efeitos().first.id;
      final segundo = efeitos().last.id;
      final k = 'efeito-$primeiro';

      // EXPANDIR: recolhido nao constroi o corpo; a seta abre.
      expect(find.byKey(ValueKey('$k-corpo')), findsNothing);
      await tester.tap(find.byKey(ValueKey('$k-seta')));
      await tester.pumpAndSettle();
      expect(find.byKey(ValueKey('$k-corpo')), findsOneWidget);
      // Toda linha numerica do efeito tem losango.
      final numerico = efeitos().first.spec.params.entries.firstWhere(
        (e) => e.value.kind == ParamKind.number,
      );
      expect(
        find.byKey(ValueKey('kf-$primeiro-${numerico.key}')),
        findsOneWidget,
      );
      // Abrir o cartao faz dele a propriedade ativa da timeline.
      expect(
        b.container.read(propriedadeAtivaProvider),
        PropriedadeAtiva.efeito(primeiro),
      );
      await tester.tap(find.byKey(ValueKey('$k-seta')));
      await tester.pumpAndSettle();
      expect(find.byKey(ValueKey('$k-corpo')), findsNothing);

      // DESLIGAR sem apagar.
      await tester.tap(find.byKey(ValueKey('$k-olho')));
      await tester.pumpAndSettle();
      expect(efeitos().first.enabled, isFalse);
      expect(efeitos(), hasLength(2));
      await tester.tap(find.byKey(ValueKey('$k-olho')));
      await tester.pumpAndSettle();
      expect(efeitos().first.enabled, isTrue);

      // REORDENAR arrastando a alca do primeiro para baixo do segundo.
      final g = await tester.startGesture(
        tester.getCenter(find.byKey(ValueKey('$k-alca'))),
      );
      for (var i = 0; i < 10; i++) {
        await g.moveBy(const Offset(0, 8));
        await tester.pump(const Duration(milliseconds: 16));
      }
      await g.up();
      await tester.pumpAndSettle();
      expect(efeitos().map((e) => e.id), [segundo, primeiro]);
      // UM desfazer devolve a ordem (e so a ordem: os dois continuam).
      b.c.undo();
      await tester.pumpAndSettle();
      expect(efeitos().map((e) => e.id), [primeiro, segundo]);
      expect(efeitos().every((e) => e.enabled), isTrue);

      // APAGAR pelo ⋯.
      await tester.tap(find.byKey(ValueKey('$k-menu')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('menu-efeito-apagar')));
      await tester.pumpAndSettle();
      expect(efeitos().map((e) => e.id), [segundo]);
      expect(find.byKey(ValueKey('$k-cabecalho')), findsNothing);
      expect(tester.takeException(), isNull);
    });
  });
}
