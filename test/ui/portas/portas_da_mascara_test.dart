import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/mascara.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import '../paineis/apoio_paineis.dart';

// AS PORTAS DO PAINEL MASCARA: Revelar (o "Pronto" antigo), trocar o
// caminho de uma mascara (retangulo, elipse, a forma da camada, desenhar)
// e o aviso de quem recorta (a camada de cima).

Future<void> _ver(WidgetTester tester, Finder alvo) => tester.scrollUntilVisible(
  alvo,
  80,
  scrollable: find
      .descendant(
        of: find.byKey(const ValueKey('painel-mascara')),
        matching: find.byType(Scrollable),
      )
      .first,
);

void main() {
  testWidgets('Revelar: um toque cria a mascara com dois keyframes, um '
      'desfazer', (tester) async {
    final (b, id) = await montarPainel(
      tester,
      altura: 600,
      preparar: (c) {
        c.addShapeLayer(Duration.zero, name: 'Forma');
        return c.projetoCompleto.layers.single.id;
      },
      painel: (id) => PainelMascara(layerId: id),
    );
    final chip = find.byKey(const ValueKey('mascara-revelar-esquerda'));
    await _ver(tester, chip);
    await tester.tap(chip);
    await tester.pumpAndSettle();
    final m = b.camada(id).masks.single;
    expect(m.path.keyframes.length, greaterThanOrEqualTo(2));
    b.c.undo();
    expect(b.camada(id).masks, isEmpty);
    // Todos os presets do dominio tem porta.
    await tester.pumpAndSettle();
    for (final p in MaskRevealPreset.values) {
      final chip = find.byKey(ValueKey('mascara-revelar-${p.name}'));
      await _ver(tester, chip);
      expect(chip, findsOneWidget, reason: p.name);
    }
    expect(tester.takeException(), isNull);
  });

  testWidgets('⋯ da mascara: trocar por elipse e retangulo, usar a forma '
      'da camada, desenhar', (tester) async {
    late String maskId;
    final (b, id) = await montarPainel(
      tester,
      altura: 600,
      preparar: (c) {
        c.addShapeLayer(Duration.zero, name: 'Forma');
        final id = c.projetoCompleto.layers.single.id;
        final m = LayerMask(
          name: 'M',
          path: AnimatedPath(BezierPath.star(5, 250, 125)),
        );
        maskId = m.id;
        c.addMask(id, m);
        return id;
      },
      painel: (id) => PainelMascara(layerId: id),
    );
    BezierPath caminho() =>
        b.camada(id).masks.single.path.valueAt(Duration.zero);
    Future<void> escolher(String item) async {
      await tester.tap(find.byKey(ValueKey('mascara-$maskId-menu')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(ValueKey('menu-mascara-$item')));
      await tester.pumpAndSettle();
    }

    final estrela = caminho().vertices.length;
    await escolher('elipse');
    expect(caminho().vertices.length, isNot(estrela));
    expect(caminho().closed, isTrue);
    final caixa = b.c.maskBox(id, Duration.zero);
    final limites = caminho().build().getBounds();
    expect(limites.width, closeTo(caixa.width, 1));

    await escolher('retangulo');
    expect(caminho().vertices, hasLength(4));

    await escolher('da-forma');
    expect(caminho().vertices, isNotEmpty);
    b.c.undo();
    expect(caminho().vertices, hasLength(4), reason: 'um passo so');

    await escolher('desenhar');
    expect(caminho().vertices, isEmpty);
    expect(caminho().closed, isFalse);
    expect(tester.takeException(), isNull);
  });

  testWidgets('recorte: o aviso diz quem esta acima (ou pede uma)', (
    tester,
  ) async {
    final (b, id) = await montarPainel(
      tester,
      altura: 600,
      preparar: (c) {
        c.addShapeLayer(Duration.zero, name: 'Base');
        return c.projetoCompleto.layers.single.id;
      },
      painel: (id) => PainelMascara(layerId: id),
    );
    expect(b.c.matteSourceAbove(id), isNull);
    expect(
      find.text('Coloque uma camada acima desta para recortar por ela.'),
      findsOneWidget,
    );
    b.c.addShapeLayer(Duration.zero, name: 'Fonte');
    // A nova entra por cima da base.
    final acima = b.c.matteSourceAbove(id);
    expect(acima?.name, startsWith('Fonte'));
    await tester.pumpAndSettle();
    expect(
      find.text('Fonte acima: ${acima!.name}. Ela fica oculta.'),
      findsOneWidget,
    );
    expect(tester.takeException(), isNull);
  });
}
