import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/help/presentation/quick_guide_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('todo efeito do catalogo tem instrucao concreta', () {
    // O QUE SE COBRA E O CATALOGO, e nao o enum.
    //
    // Antes isto exigia `effectSpecs.length == EffectType.values.length`.
    // Era verdade ate 16/09, quando o dono cortou o catalogo de 102 para 37
    // e deixou o enum inteiro de pe de proposito: o enum guarda POSICOES
    // antigas (compatibilidade de indice de arquivo) e por isso tem mais
    // variantes do que fichas. Cobrar igualdade aqui seria cobrar de volta
    // o catalogo velho.
    //
    // A invariante que importa: quem aparece na galeria sabe se explicar.
    expect(efeitosDoCatalogo, isNotEmpty);
    expect(quickStartSteps.length, 6);
    for (final type in efeitosDoCatalogo) {
      expect(effectHelp(type).length, greaterThan(65), reason: type.name);
    }
  });

  testWidgets('phone guide can search, open help and clear search', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(375, 812);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(
      const MaterialApp(home: QuickGuideScreen(initialQuery: 'posterize')),
    );
    final field = find.byType(TextField);
    await tester.scrollUntilVisible(
      field,
      300,
      scrollable: find.byType(Scrollable).first,
    );
    await tester.enterText(field, 'posterize');
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.text('Posterize'));
    await tester.tap(find.text('Posterize'));
    await tester.pumpAndSettle();
    expect(find.text(effectHelp(EffectType.posterize)), findsOneWidget);
    await tester.ensureVisible(field);
    await tester.enterText(field, 'naoexiste123');
    await tester.pumpAndSettle();
    expect(find.textContaining('Nenhum efeito encontrado'), findsOneWidget);
    await tester.tap(find.byTooltip('Limpar busca'));
    await tester.pumpAndSettle();
    expect(find.textContaining('Nenhum efeito encontrado'), findsNothing);
    expect(tester.takeException(), isNull);
  });
}
