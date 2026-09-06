import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/text_anim.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';

const _dur = Duration(seconds: 5);

TextAnimator _compile(TextAnim a, {int n = 5}) =>
    compileTextAnim(a, layerDuration: _dur, unitCount: n);

double _cov(TextAnimator a, int i, int n, int ms) =>
    a.coverageAt(i, n, Duration(milliseconds: ms));

void main() {
  group('Catalogo', () {
    test('toda animacao tem id unico, rotulo e ao menos uma posicao', () {
      final ids = <String>{};
      for (final s in textAnimCatalog) {
        expect(ids.add(s.id), isTrue, reason: 'id repetido: ${s.id}');
        expect(s.label.trim(), isNotEmpty);
        expect(s.slots, isNotEmpty);
      }
      expect(textAnimCatalog.length, greaterThanOrEqualTo(30));
    });

    test('toda animacao mexe em ao menos uma propriedade', () {
      for (final s in textAnimCatalog) {
        final v = s.build(s.defaults);
        expect(v, isNotEmpty, reason: '${s.id} nao mexe em nada');
      }
    });

    test('cada posicao tem opcoes de sobra', () {
      expect(textAnimsForSlot(TextAnimSlot.entrada).length,
          greaterThanOrEqualTo(18));
      expect(textAnimsForSlot(TextAnimSlot.saida).length,
          greaterThanOrEqualTo(12));
      expect(textAnimsForSlot(TextAnimSlot.enfase).length,
          greaterThanOrEqualTo(9));
    });

    test('parametro exposto tem faixa valida e inicial dentro dela', () {
      for (final s in textAnimCatalog) {
        for (final p in s.params) {
          expect(p.min, lessThan(p.max), reason: '${s.id}/${p.key}');
          expect(p.initial, inInclusiveRange(p.min, p.max),
              reason: '${s.id}/${p.key}');
        }
      }
    });
  });

  group('Entrada', () {
    // O que a animacao de entrada TEM de fazer: no frame 0 a unidade
    // esta no estado animado, e no fim ela esta neutra. O animador
    // antigo errava justo isso quando se esquecia do "hold".
    test('comeca no estado animado e termina neutra', () {
      final anim = TextAnim(specId: 'slideUp', slot: TextAnimSlot.entrada);
      final a = _compile(anim);
      expect(_cov(a, 0, 5, 0), closeTo(1, 1e-9));

      final total = anim.totalFor(5).inMilliseconds;
      for (var i = 0; i < 5; i++) {
        expect(_cov(a, i, 5, total + 10), closeTo(0, 1e-9),
            reason: 'unidade $i ainda animada no fim');
      }
    });

    test('unidade seguinte entra depois, pelo atraso', () {
      final anim = TextAnim(
        specId: 'fade',
        slot: TextAnimSlot.entrada,
        duration: const Duration(milliseconds: 200),
        stagger: const Duration(milliseconds: 100),
        ease: TextAnimEase.linear,
      );
      final a = _compile(anim);
      // Em 200ms a primeira ja terminou; a segunda esta na metade; a
      // terceira nem comecou.
      expect(_cov(a, 0, 5, 200), closeTo(0, 1e-9));
      expect(_cov(a, 1, 5, 200), closeTo(0.5, 1e-9));
      expect(_cov(a, 2, 5, 200), closeTo(1, 1e-9));
    });

    test('atraso zero faz todas entrarem juntas', () {
      final anim = TextAnim(
        specId: 'fade',
        slot: TextAnimSlot.entrada,
        stagger: Duration.zero,
        ease: TextAnimEase.linear,
        duration: const Duration(milliseconds: 400),
      );
      final a = _compile(anim);
      final c0 = _cov(a, 0, 5, 200);
      for (var i = 1; i < 5; i++) {
        expect(_cov(a, i, 5, 200), closeTo(c0, 1e-9));
      }
    });

    test('maquina de escrever liga de uma vez, sem meio-termo', () {
      final anim = TextAnim(specId: 'typewriter', slot: TextAnimSlot.entrada);
      final a = _compile(anim);
      for (final ms in [0, 30, 59, 61, 200, 400]) {
        for (var i = 0; i < 5; i++) {
          final c = _cov(a, i, 5, ms);
          expect(c == 0 || c == 1, isTrue,
              reason: 'unidade $i em ${ms}ms deu $c');
        }
      }
      // Cada letra acende no seu tempo: 60ms de atraso.
      expect(_cov(a, 0, 5, 10), 0);
      expect(_cov(a, 1, 5, 10), 1);
      expect(_cov(a, 1, 5, 70), 0);
    });
  });

  group('Saida', () {
    // Saida conta do FIM da camada para tras: a ultima unidade termina
    // de sair exatamente quando a camada acaba.
    test('esta neutra no comeco e animada no fim da camada', () {
      final anim = TextAnim(specId: 'fade', slot: TextAnimSlot.saida);
      final a = _compile(anim);
      expect(_cov(a, 0, 5, 0), closeTo(0, 1e-9));
      for (var i = 0; i < 5; i++) {
        expect(_cov(a, i, 5, _dur.inMilliseconds), closeTo(1, 1e-9),
            reason: 'unidade $i nao saiu');
      }
    });

    test('saida mais longa que a camada nao estoura o inicio', () {
      final anim = TextAnim(
        specId: 'fade',
        slot: TextAnimSlot.saida,
        duration: const Duration(seconds: 9),
      );
      final a = _compile(anim);
      // Nao ha tempo de sobra: comeca a sair ja no frame 0, mas nunca
      // com cobertura negativa nem NaN.
      final c = _cov(a, 0, 5, 0);
      expect(c.isFinite, isTrue);
      expect(c, inInclusiveRange(0, 1));
    });
  });

  group('Ordem', () {
    test('do fim inverte quem entra primeiro', () {
      final base = TextAnim(
        specId: 'fade',
        slot: TextAnimSlot.entrada,
        duration: const Duration(milliseconds: 100),
        stagger: const Duration(milliseconds: 100),
        ease: TextAnimEase.linear,
      );
      final frente = _compile(base);
      final tras =
          _compile(base.copyWith(order: TextAnimOrder.reverse));
      // Em 50ms a primeira ja esta entrando na ordem normal...
      expect(_cov(frente, 0, 5, 50), lessThan(1));
      // ...e na ordem invertida quem entra e a ultima.
      expect(_cov(tras, 4, 5, 50), lessThan(1));
      expect(_cov(tras, 0, 5, 50), closeTo(1, 1e-9));
    });

    test('todas as ordens sao permutacoes: ninguem fica de fora', () {
      const n = 7;
      for (final o in TextAnimOrder.values) {
        final seen = <int>{};
        for (var i = 0; i < n; i++) {
          seen.add(orderMapIndex(selectorOrderFor(o), i, n, 42));
        }
        expect(seen.length, n, reason: 'ordem $o perdeu unidades');
      }
    });

    test('aleatoria e deterministica pela semente', () {
      final a = TextAnim(
          specId: 'fade',
          slot: TextAnimSlot.entrada,
          order: TextAnimOrder.random,
          seed: 7);
      final x = _compile(a);
      final y = _compile(a);
      for (var i = 0; i < 5; i++) {
        expect(_cov(x, i, 5, 120), _cov(y, i, 5, 120));
      }
      final z = _compile(a.copyWith(seed: 8));
      var diferente = false;
      for (var i = 0; i < 5; i++) {
        if (_cov(z, i, 5, 120) != _cov(x, i, 5, 120)) diferente = true;
      }
      expect(diferente, isTrue, reason: 'semente nao mudou nada');
    });
  });

  group('Curvas', () {
    test('mola passa do alvo — e o que da vida ao movimento', () {
      var passou = false;
      for (var i = 1; i <= 100; i++) {
        if (springEase(i / 100) > 1.0001) passou = true;
      }
      expect(passou, isTrue);
      expect(springEase(0), 0);
      expect(springEase(1), closeTo(1, 0.02));
    });

    test('mola da extensao: amplitude, frequencia e decaimento mandam', () {
      final forte = springEase(0.25, amplitude: 1, frequency: 3, decay: 2);
      final fraca = springEase(0.25, amplitude: 0.2, frequency: 3, decay: 2);
      expect((forte - 1).abs(), greaterThan((fraca - 1).abs()));

      // Decaimento alto mata a oscilacao mais cedo.
      final lento = springEase(0.6, decay: 1);
      final rapido = springEase(0.6, decay: 12);
      expect((rapido - 1).abs(), lessThan((lento - 1).abs()));
    });

    test('quicar nunca passa do alvo', () {
      for (var i = 0; i <= 100; i++) {
        final v = bounceEase(i / 100);
        expect(v, inInclusiveRange(0, 1.0001));
      }
      expect(bounceEase(1), closeTo(1, 1e-9));
    });

    test('curvas comecam em 0 e terminam em 1', () {
      for (final e in TextAnimEase.values) {
        expect(applyEase(e, 0), closeTo(0, 1e-9), reason: '$e no inicio');
        expect(applyEase(e, 1), closeTo(1, 0.02), reason: '$e no fim');
      }
    });

    test('mola liga o overshoot no animador, senao seria cortada', () {
      final a = _compile(TextAnim(
          specId: 'fade',
          slot: TextAnimSlot.entrada,
          ease: TextAnimEase.mola));
      expect(a.allowOvershoot, isTrue);
    });
  });

  group('Enfase', () {
    test('fica repetindo em vez de terminar', () {
      final a = _compile(
          TextAnim(specId: 'wave', slot: TextAnimSlot.enfase));
      // Um ciclo depois, a cobertura volta ao mesmo ponto.
      final c0 = _cov(a, 0, 5, 300);
      final c1 = _cov(a, 0, 5, 300 + 1200);
      expect(c1, closeTo(c0, 1e-6));
      // E continua viva muito depois do "fim".
      expect(_cov(a, 0, 5, 60000).isFinite, isTrue);
    });

    test('onda defasa uma unidade da outra', () {
      final a = _compile(
          TextAnim(specId: 'wave', slot: TextAnimSlot.enfase));
      expect(_cov(a, 0, 5, 400), isNot(closeTo(_cov(a, 1, 5, 400), 1e-6)));
    });

    test('tremor e ruido puro: mesmo tempo, mesmo valor', () {
      final a = _compile(
          TextAnim(specId: 'shake', slot: TextAnimSlot.enfase));
      final b = _compile(
          TextAnim(specId: 'shake', slot: TextAnimSlot.enfase));
      for (final ms in [17, 133, 940]) {
        expect(_cov(a, 2, 5, ms), _cov(b, 2, 5, ms));
      }
    });
  });

  group('Compilacao', () {
    test('a animacao vira um animador de verdade, inspecionavel', () {
      final a = _compile(
          TextAnim(specId: 'blurIn', slot: TextAnimSlot.entrada));
      expect(a.selectors.length, 1);
      expect(a.selectors.first, isA<StaggerSelector>());
      final tipos = a.properties.map((p) => p.type).toSet();
      expect(tipos, contains(TextAnimProp.blur));
      expect(tipos, contains(TextAnimProp.opacity));
    });

    test('a unidade escolhida vira a base do seletor', () {
      for (final u in TextAnimUnit.values) {
        final a = _compile(TextAnim(
            specId: 'fade', slot: TextAnimSlot.entrada, unit: u));
        expect(a.selectors.first.basedOn, basedOnFor(u));
      }
      expect(basedOnFor(TextAnimUnit.word), SelectorBasedOn.words);
      expect(basedOnFor(TextAnimUnit.line), SelectorBasedOn.lines);
    });

    test('"tudo junto" ignora o atraso', () {
      final a = _compile(TextAnim(
        specId: 'fade',
        slot: TextAnimSlot.entrada,
        unit: TextAnimUnit.all,
        stagger: const Duration(milliseconds: 200),
      ));
      expect((a.selectors.first as StaggerSelector).stagger, Duration.zero);
    });

    test('a lista compila na ordem entrada, enfase, saida', () {
      final out = compileTextAnims([
        TextAnim(specId: 'fade', slot: TextAnimSlot.saida),
        TextAnim(specId: 'wave', slot: TextAnimSlot.enfase),
        TextAnim(specId: 'pop', slot: TextAnimSlot.entrada),
      ], layerDuration: _dur, unitCount: 4);
      expect(out.map((a) => a.name).toList(),
          ['Estourar', 'Onda', 'Aparecer']);
    });

    test('animacao desligada nao anima nada', () {
      final a = _compile(TextAnim(
          specId: 'fade',
          slot: TextAnimSlot.entrada,
          enabled: false));
      expect(a.coverageAt(0, 5, Duration.zero), 0);
    });

    test('todo o catalogo compila sem estourar', () {
      for (final s in textAnimCatalog) {
        for (final slot in s.slots) {
          final a = _compile(TextAnim(specId: s.id, slot: slot));
          for (final ms in [0, 250, 1000, 4999, 9000]) {
            for (var i = 0; i < 5; i++) {
              expect(_cov(a, i, 5, ms).isFinite, isTrue,
                  reason: '${s.id}/$slot em ${ms}ms');
            }
          }
        }
      }
    });
  });

  group('Camada', () {
    test('sem animacao, o caminho rapido continua valendo', () {
      final l = TextLayer(
          name: 'T',
          startTime: Duration.zero,
          duration: _dur,
          text: 'oi');
      expect(l.hasTextAnimation, isFalse);
      expect(l.effectiveAnimators(2), isEmpty);
    });

    test('animacao do catalogo e animador cru convivem', () {
      final l = TextLayer(
        name: 'T',
        startTime: Duration.zero,
        duration: _dur,
        text: 'oi',
        anims: [TextAnim(specId: 'fade', slot: TextAnimSlot.entrada)],
        animators: [
          TextAnimator(properties: [
            AnimatorProperty(type: TextAnimProp.rotation),
          ]),
        ],
      );
      expect(l.hasTextAnimation, isTrue);
      final all = l.effectiveAnimators(2);
      expect(all.length, 2);
      // O compilado vem primeiro; o cru fica por cima.
      expect(all.first.selectors.first, isA<StaggerSelector>());
    });
  });

  group('Propriedades novas', () {
    test('escala por eixo, saturacao e brilho sao multiplicativas', () {
      for (final t in [
        TextAnimProp.scaleX,
        TextAnimProp.scaleY,
        TextAnimProp.saturation,
        TextAnimProp.brightness,
      ]) {
        expect(AnimatorProperty.isMultiplicative(t), isTrue);
        expect(AnimatorProperty(type: t).neutral, 100);
      }
    });

    test('desfoque, inclinacao e matiz sao aditivos e neutros em zero', () {
      for (final t in [
        TextAnimProp.blur,
        TextAnimProp.skew,
        TextAnimProp.hue,
      ]) {
        expect(AnimatorProperty.isMultiplicative(t), isFalse);
        expect(AnimatorProperty(type: t).neutral, 0);
      }
    });

    test('I2: cobertura zero nao altera nada, em nenhuma propriedade', () {
      for (final t in TextAnimProp.values) {
        final p = AnimatorProperty(type: t);
        expect(p.apply(42, Duration.zero, 0), 42, reason: '$t');
      }
    });

    test('toda propriedade tem rotulo', () {
      for (final t in TextAnimProp.values) {
        expect(textAnimPropLabel(t).trim(), isNotEmpty);
      }
    });
  });
}
