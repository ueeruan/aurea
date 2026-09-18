import 'editor_audit_helpers.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/application/ui/pro_mode.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// FASE 5 DO REDESIGN — KEYFRAMES E EASING (docs/UI_REDESIGN_PLAN.md, 3.5).
///
/// - Tocar o losango na barra abre o easing (E5): selecionar, tocar,
///   escolher o preset — tres toques (criterio de aceite).
/// - Simples: grade de presets com miniatura; Pro: o editor de curva.
/// - Expressao por propriedade (Pro) pelo toque longo no valor.
/// - Loop de keyframes (Pro) no menu da curva.
/// - Simples: mover no palco anima (auto-key) sem ligar nada.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  test('editPosition sem keyframe mexe base; com toggle cria keyframes', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'A');
    final id = c.read(editorControllerProvider).layers.single.id;
    e.editPosition(id, const Duration(seconds: 1), const Offset(300, 300));
    expect(c.read(editorControllerProvider).layerById(id)!.position.isAnimated, isFalse);
    e.toggleKeyframe(id, Duration.zero, LayerProp.position);
    e.editPosition(id, const Duration(seconds: 1), const Offset(400, 400));
    final l = c.read(editorControllerProvider).layerById(id)!;
    expect(l.position.isAnimated, isTrue);
  });

  test('expressao por propriedade: poe, le e tira', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(Duration.zero, name: 'A');
    final id = c.read(editorControllerProvider).layers.single.id;
    e.setPropExpression(id, LayerProp.opacity, 'time * 0.5');
    var l = c.read(editorControllerProvider).layerById(id)!;
    expect(l.opacity.expression, 'time * 0.5');
    expect(e.propExpression(l, LayerProp.opacity), 'time * 0.5');
    e.setPropExpression(id, LayerProp.opacity, '  ');
    l = c.read(editorControllerProvider).layerById(id)!;
    expect(l.opacity.hasExpression, isFalse);
  });

  testWidgets('tocar o losango abre o E5 com a grade de presets (Simples) e o preset muda o easing', (tester) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    final id = c.read(editorControllerProvider).layers.first.id;
    e.editOpacity(id, Duration.zero, 1);
    e.toggleKeyframe(id, Duration.zero, LayerProp.opacity);
    e.toggleKeyframe(id, const Duration(seconds: 1), LayerProp.opacity);
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();

    final losango = find.byKey(ValueKey('keyframe-glyph-$id-0'));
    expect(losango, findsOneWidget);
    await tester.tap(losango);
    await tester.pumpAndSettle();
    final s = c.read(editorSessionProvider);
    expect(s.panel, EditorPanel.curve, reason: 'o losango abre o easing');
    expect(s.curveProp, LayerProp.opacity);
    expect(find.byKey(const ValueKey('curve-edit-area')), findsOneWidget);

    await tester.tap(find.text('Ease in').last);
    await tester.pumpAndSettle();
    final l = c.read(editorControllerProvider).layerById(id)!;
    expect(l.opacity.easeAt(Duration.zero).x1, closeTo(Easing.easeIn.x1, 1e-6));

    // Voltar sai da curva para o E2 (de onde veio).
    await tester.tap(find.byKey(const ValueKey('editor-back')));
    await tester.pumpAndSettle();
    expect(c.read(editorSessionProvider).panel, EditorPanel.none);

    // Pro: o editor de curva de verdade.
    c.read(proModeProvider.notifier).set(true);
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(ValueKey('keyframe-glyph-$id-0')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('curve-edit-area')), findsOneWidget);
    expect(find.byKey(const ValueKey('curve-presets-grid')), findsNothing);
  });

  testWidgets('Pro: toque longo no valor abre a expressao; loop pelo menu da curva', (tester) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(proModeProvider.notifier).set(true);
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    await tester.tap(find.text('Movimentação e transformação'));
    await tester.pumpAndSettle();
    await selectTransformTool(tester, 'Opacid.');
    await tester.pumpAndSettle();
    await tester.longPress(find.byKey(const ValueKey('opacidade-valor')));
    await tester.pumpAndSettle();
    final campo = find.byKey(const ValueKey('expressao-campo'));
    expect(campo, findsOneWidget);
    await tester.enterText(campo, '50');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).layerById(id)!.opacity.expression, '50');
    expect(find.text('fx'), findsOneWidget, reason: 'o chip avisa que ha expressao');

    // Loop (Pro) pelo menu da curva.
    e.toggleKeyframe(id, Duration.zero, LayerProp.opacity);
    e.toggleKeyframe(id, const Duration(seconds: 1), LayerProp.opacity);
    await tester.pumpAndSettle();
    await tester.tap(find.byTooltip('Editar curva da propriedade'));
    await tester.pumpAndSettle();
    await tester.tap(find.byTooltip('Opções da curva'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Loop: repetir'));
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).layerById(id)!.opacity.loop.mode, LoopMode.cycle);
  });
}
