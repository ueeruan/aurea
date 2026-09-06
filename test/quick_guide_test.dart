import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/help/presentation/quick_guide_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/am/effects_panel.dart';

import 'editor_hierarchy_test.dart' show openEditor;

void main() {
  testWidgets(
    'effects help opens and returns to the same layer on a small phone',
    (tester) async {
      final container = await openEditor(tester, size: const Size(375, 667));
      final layer = container.read(editorControllerProvider).layers.first;
      container
          .read(editorControllerProvider.notifier)
          .addEffect(layer.id, EffectType.posterize);
      container.read(selectedLayerProvider.notifier).state = layer.id;
      await tester.pumpAndSettle();
      await tester.tap(find.text('Efeitos'));
      await tester.pumpAndSettle();
      expect(find.byType(EffectsPanel), findsOneWidget);
      await tester.tap(find.byTooltip('Como usar os efeitos'));
      await tester.pumpAndSettle();
      expect(find.byType(QuickGuideScreen), findsOneWidget);
      await tester.binding.handlePopRoute();
      await tester.pumpAndSettle();
      expect(find.byType(EditorScreen), findsOneWidget);
      expect(find.byType(EffectsPanel), findsOneWidget);
      expect(container.read(selectedLayerProvider), layer.id);
      expect(tester.takeException(), isNull);
    },
  );
  test('all 43 catalog entries have concrete offline instructions', () {
    expect(effectSpecs.length, 43);
    expect(quickStartSteps.length, 6);
    for (final type in EffectType.values) {
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
