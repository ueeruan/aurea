import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';


/// FASE 5 DO REDESIGN — KEYFRAMES E EASING (docs/UI_REDESIGN_PLAN.md, 3.5).
///
/// - Tocar o losango na barra abre o easing (E5): selecionar, tocar,
///   escolher o preset — tres toques (criterio de aceite).
/// - Simples: grade de presets com miniatura; Pro: o editor de curva.
/// - Expressao por propriedade (Pro) pelo toque longo no valor.
/// - Loop de keyframes (Pro) no menu da curva.
/// - Simples: mover no palco anima (auto-key) sem ligar nada.
void main() {
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

}
