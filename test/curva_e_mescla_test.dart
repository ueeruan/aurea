// CURVA E MESCLA (v1.1.1): as familias da curva com os tipos novos, o
// inverter, e a mesclagem em sete categorias com todos os modos.
import 'package:aurea/src/features/editor/domain/blend_extra.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/mascara.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('os tipos novos saem de 0 e chegam a 1 sem numero impossivel', () {
    const novos = [
      Easing.bounceIn,
      Easing.elasticIn,
      Easing.stepsRandom,
      Easing.oscillate,
      Easing.repeat,
      Easing.sawtooth,
    ];
    for (final e in novos) {
      expect(e.transform(0), 0, reason: e.label);
      expect(e.transform(1), 1, reason: e.label);
      for (var i = 1; i < 100; i++) {
        final v = e.transform(i / 100);
        expect(v.isFinite, isTrue, reason: '${e.label} em ${i / 100}');
      }
    }
    // O quique de entrada e o de saida ao contrario.
    for (final t in [0.1, 0.33, 0.7]) {
      expect(
        Easing.bounceIn.transform(t),
        closeTo(1 - Easing.bounce.transform(1 - t), 1e-9),
      );
    }
    // Oscilar 3 vezes passa pelo valor final no meio do caminho.
    expect(Easing.oscillate.transform(1 / 5), closeTo(1, 1e-9));
    // Dente de serra: rampa reta repetida.
    expect(Easing.sawtooth.transform(1 / 6), closeTo(0.5, 1e-9));
  });

  test('inverter: bezier espelha as alcas, quique e elastico trocam de ponta', () {
    final inv = Easing.easeIn.invertida!;
    expect(inv.x1, closeTo(0, 1e-9));
    expect(inv.x2, closeTo(0.58, 1e-9));
    expect(Easing.bounce.invertida!.type, EasingType.bounceIn);
    expect(Easing.elasticIn.invertida!.type, EasingType.elastic);
    expect(Easing.oscillate.invertida, isNull);
  });

  test('os tipos novos vao e voltam do arquivo pelo indice', () {
    final p = VideoProject(name: 'p', createdAt: DateTime(2026, 9, 15));
    final json = projectToJson(p);
    // O indice dos antigos nao mudou (arquivos velhos continuam lendo).
    expect(EasingType.hold.index, 8);
    expect(EasingType.bounceIn.index, 9);
    expect(projectFromJson(json).name, 'p');
  });

  test('as sete categorias cobrem todos os modos do motor, sem repetir', () {
    expect(categoriasDeMescla, hasLength(7));
    final nativos = <BlendMode>{};
    final proprios = <AureaBlend>{};
    for (final cat in categoriasDeMescla) {
      for (final m in cat.modos) {
        if (m.nativo != null) expect(nativos.add(m.nativo!), isTrue);
        if (m.aurea != null) expect(proprios.add(m.aurea!), isTrue);
      }
    }
    expect(proprios, AureaBlend.values.toSet());
    expect(nativos, containsAll([BlendMode.dstIn, BlendMode.dstOut, BlendMode.plus]));
  });
}
