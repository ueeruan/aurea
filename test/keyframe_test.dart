import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';

void main() {
  group('AnimatedDouble', () {
    test('sem keyframes devolve o valor base', () {
      final prop = AnimatedDouble(2.5);
      expect(prop.valueAt(const Duration(seconds: 3)), 2.5);
      expect(prop.isAnimated, false);
    });

    test('interpola linearmente entre dois keyframes', () {
      final prop = AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0, Easing.linear)
          .withKeyframe(const Duration(seconds: 2), 100, Easing.linear);
      expect(prop.valueAt(const Duration(seconds: 1)), closeTo(50, 0.001));
    });

    test('segura o valor antes do primeiro e depois do ultimo keyframe', () {
      final prop = AnimatedDouble(0)
          .withKeyframe(const Duration(seconds: 1), 10)
          .withKeyframe(const Duration(seconds: 2), 20);
      expect(prop.valueAt(Duration.zero), 10);
      expect(prop.valueAt(const Duration(seconds: 5)), 20);
    });

    test('busca binaria acha o segmento certo com varios keyframes', () {
      var prop = AnimatedDouble(0);
      for (var s = 0; s <= 10; s++) {
        prop = prop.withKeyframe(
            Duration(seconds: s), s * 10.0, Easing.linear);
      }
      expect(prop.valueAt(const Duration(milliseconds: 7500)),
          closeTo(75, 0.001));
    });

    test('keyframe no mesmo frame substitui o anterior', () {
      final prop = AnimatedDouble(0)
          .withKeyframe(const Duration(seconds: 1), 10)
          .withKeyframe(const Duration(seconds: 1), 99);
      expect(prop.keyframes.length, 1);
      expect(prop.valueAt(const Duration(seconds: 1)), 99);
    });

    test('remover o ultimo keyframe congela o valor atual como base', () {
      final prop = AnimatedDouble(0)
          .withKeyframe(const Duration(seconds: 1), 42);
      final still = prop.withoutKeyframe(const Duration(seconds: 1));
      expect(still.isAnimated, false);
      expect(still.valueAt(Duration.zero), 42);
    });

    // EDITAR UM VALOR NUNCA CRIA KEYFRAME (`docs/keyframe-explicito.md`).
    // O teste fixava a regra ANTIGA — "animada + edicao = marca nova" —
    // que e exatamente o defeito das marcas que apareciam sozinhas.
    test('edited: sobre a marca atualiza; fora dela nao mexe', () {
      final estatica = AnimatedDouble(1).edited(const Duration(seconds: 1), 5);
      expect(estatica.isAnimated, false);
      expect(estatica.base, 5);

      final animada = AnimatedDouble(1).withKeyframe(Duration.zero, 1);

      // FORA de qualquer marca: a trilha volta intacta, e quem chama
      // trata a recusa (a edicao fica pendente ate o losango).
      final fora = animada.edited(const Duration(seconds: 1), 5);
      expect(fora.keyframes.length, 1);
      expect(fora.valueAt(const Duration(seconds: 1)), 1);
      expect(animada.aceitaEdicaoEm(const Duration(seconds: 1)), isFalse);

      // SOBRE a marca: atualiza aquela marca, com a curva dela.
      final sobre = animada.edited(Duration.zero, 5);
      expect(sobre.keyframes.length, 1);
      expect(sobre.keyframes.single.value, 5);
      expect(animada.aceitaEdicaoEm(Duration.zero), isTrue);
    });
  });

  group('AnimatedOffset', () {
    test('interpola posicao com easing linear', () {
      final prop = AnimatedOffset(Offset.zero)
          .withKeyframe(Duration.zero, Offset.zero, Easing.linear)
          .withKeyframe(
              const Duration(seconds: 2), const Offset(100, 50), Easing.linear);
      final mid = prop.valueAt(const Duration(seconds: 1));
      expect(mid.dx, closeTo(50, 0.001));
      expect(mid.dy, closeTo(25, 0.001));
    });
  });
}
