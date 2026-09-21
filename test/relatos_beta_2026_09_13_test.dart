// OS RELATOS DOS TESTADORES DE 13/09/2026, um teste por relato.
//
//   - "Tbm tem isso de arrastar a camada sem modificar a posicao"
//   - "vc consegue colocar pra editar o grafico de elastico"
//   - o grafico do Time Remapping saindo por cima da moldura
//   - "quando eu mexia 1 o outro nao ia junto" (vinculo pai/filho)
//   - "you should move to the K-frame, not to the last project"
//   - "it should remain as is without changing the program's direction"
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/curva/curva.dart'
    show AlcasParametricas;
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

({ProviderContainer c, EditorController e}) _motor() {
  final container = ProviderContainer();
  addTearDown(container.dispose);
  return (c: container, e: container.read(editorControllerProvider.notifier));
}

void main() {
  group('elastico e quicar tem parametros de verdade', () {
    test('mudar a contagem muda a curva desenhada', () {
      const a = Easing(type: EasingType.elastic, count: 2, intensity: .5);
      const b = Easing(type: EasingType.elastic, count: 6, intensity: .5);
      var diferentes = 0;
      for (var i = 1; i < 20; i++) {
        final t = i / 20;
        if ((a.transform(t) - b.transform(t)).abs() > 1e-3) diferentes++;
      }
      expect(
        diferentes,
        greaterThan(10),
        reason: 'antes `count` existia e nao mudava um pixel',
      );
    });

    test('mudar a forca muda a curva, e as pontas continuam em 0 e 1', () {
      const a = Easing(type: EasingType.elastic, count: 3, intensity: .1);
      const b = Easing(type: EasingType.elastic, count: 3, intensity: .9);
      expect((a.transform(0.3) - b.transform(0.3)).abs(), greaterThan(1e-3));
      for (final e in [a, b, Easing.bounce, Easing.elastic]) {
        expect(e.transform(0), closeTo(0, 1e-9));
        expect(e.transform(1), closeTo(1, 1e-9));
      }
    });

    test('quicar: mais toques no chao com count maior', () {
      const dois = Easing(type: EasingType.bounce, count: 2, intensity: .5);
      const seis = Easing(type: EasingType.bounce, count: 6, intensity: .5);
      int toques(Easing e) {
        var n = 0;
        var anterior = e.transform(0.001);
        var subindo = true;
        for (var i = 2; i < 400; i++) {
          final v = e.transform(i / 400);
          if (subindo && v < anterior) {
            subindo = false;
          } else if (!subindo && v > anterior) {
            subindo = true;
            n++;
          }
          anterior = v;
        }
        return n;
      }

      expect(toques(seis), greaterThan(toques(dois)));
    });

    test('as alcas amarelas escrevem nos parametros', () {
      const e = Easing(type: EasingType.elastic, count: 3, intensity: .5);
      expect(AlcasParametricas.de(e), hasLength(2));
      // A alca A para a esquerda = mais oscilacoes.
      expect(AlcasParametricas.comAlcaA(e, 0.1).count, greaterThan(e.count));
      expect(AlcasParametricas.comAlcaA(e, 0.9).count, lessThan(e.count));
      // A alca B para cima = mais forca.
      expect(
        AlcasParametricas.comAlcaB(e, 1.45).intensity,
        greaterThan(AlcasParametricas.comAlcaB(e, 1.05).intensity),
      );
      // Bezier nao tem alca amarela nenhuma.
      expect(AlcasParametricas.de(Easing.easeInOut), isEmpty);
    });
  });

  group('vinculo pai/filho', () {
    test('mover o pai leva o filho junto, e o filho nao pula ao parear', () {
      final m = _motor();
      m.e.addTextLayer(Duration.zero, text: 'Filho');
      m.e.addNullLayer(Duration.zero);
      final camadas = m.c.read(editorControllerProvider).layers;
      final filho = camadas.firstWhere((l) => l is TextLayer);
      final pai = camadas.firstWhere((l) => l is NullLayer);
      m.e.editPosition(filho.id, Duration.zero, const Offset(300, 400));

      Offset ondeEsta(String id) => effectiveTransform(
        m.c.read(editorControllerProvider),
        m.c.read(editorControllerProvider).layerById(id)!,
        Duration.zero,
      ).pos;

      final antes = ondeEsta(filho.id);
      m.e.linkProperty(filho.id, LayerProp.parent, pai.id, Duration.zero);
      expect(
        (ondeEsta(filho.id) - antes).distance,
        lessThan(0.001),
        reason: 'parear nao pode mover o filho',
      );

      final p = m.c.read(editorControllerProvider).layerById(pai.id)!;
      final origem = p.position.valueAt(Duration.zero);
      m.e.editPosition(pai.id, Duration.zero, origem + const Offset(120, -50));

      final depois = ondeEsta(filho.id);
      expect(depois.dx - antes.dx, closeTo(120, 0.01));
      expect(depois.dy - antes.dy, closeTo(-50, 0.01));
    });
  });
}
