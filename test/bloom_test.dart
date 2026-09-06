import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/bloom.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';

void main() {
  group('Piramide de bloom', () {
    // "Conservacao de energia" tem um significado exato: a soma dos
    // pesos e 1. Sem isso, acrescentar um nivel para suavizar tambem
    // clareia, e a pessoa fica ajustando intensidade para compensar uma
    // suavidade que nao pediu.
    test('os pesos somam 1 em qualquer qualidade', () {
      for (final q in [0, 1, 2]) {
        final pesos = bloomWeights(bloomLevels(q));
        final soma = pesos.reduce((a, b) => a + b);
        expect(soma, closeTo(1, 1e-9), reason: 'qualidade $q');
      }
    });

    test('mais qualidade, mais niveis', () {
      expect(bloomLevels(0), lessThan(bloomLevels(1)));
      expect(bloomLevels(1), lessThan(bloomLevels(2)));
    });

    // Cada oitava com o MESMO peso: e o que faz o halo ir longe. Dar
    // mais peso ao nivel estreito deixava o largo com 6% da luz, e o
    // halo sumia a um palmo da forma.
    test('todos os niveis pesam igual', () {
      final p = bloomWeights(4);
      expect(p.every((v) => (v - 0.25).abs() < 1e-9), isTrue);
    });

    test('um nivel so pesa tudo', () {
      expect(bloomWeights(1), [1.0]);
      expect(bloomWeights(0), isEmpty);
    });

    // Sigma dobrando PARA CIMA a partir do raio: o raio da ficha e a
    // base da piramide, nao o teto. E o que faz um raio de 40 chegar a
    // centenas de pixels no nivel largo, como no plugin de referencia.
    test('o sigma dobra a cada nivel a partir do raio', () {
      final s = bloomSigmas(40, 3);
      expect(s[0], closeTo(40, 1e-9));
      expect(s[1], closeTo(80, 1e-9));
      expect(s[2], closeTo(160, 1e-9));
    });

    test('raio zero nao gera sigma', () {
      expect(bloomSigmas(0, 3), isEmpty);
    });
  });

  group('Limiar', () {
    // NEUTRALIDADE: abaixo do limiar, nada entra no glow.
    test('abaixo do limiar nao entra nada', () {
      expect(bloomThreshold(0.2, 1.0, 0.0), 0);
      expect(bloomThreshold(0.2, 1.0, 0.2), 0);
    });

    test('acima do limiar entra so o excesso', () {
      expect(bloomThreshold(1.5, 1.0, 0.0), closeTo(0.5, 1e-9));
    });

    // Limiar duro cria uma linha reta onde o brilho cruza o valor; a
    // rampa transforma o degrau em transicao.
    test('a suavidade abre uma rampa em volta do limiar', () {
      final duro = bloomThreshold(0.95, 1.0, 0.0);
      final suave = bloomThreshold(0.95, 1.0, 0.3);
      expect(duro, 0);
      expect(suave, greaterThan(0));
    });

    test('a rampa cresce sem pular', () {
      var anterior = -1.0;
      for (var i = 0; i <= 40; i++) {
        final v = bloomThreshold(i / 20, 1.0, 0.3);
        expect(v, greaterThanOrEqualTo(anterior - 1e-9));
        anterior = v;
      }
    });
  });

  group('Exposicao e tom', () {
    test('cada parada dobra a luz', () {
      expect(exposureGain(0), closeTo(1, 1e-9));
      expect(exposureGain(1), closeTo(2, 1e-9));
      expect(exposureGain(-1), closeTo(0.5, 1e-9));
    });

    test('todo mapeamento devolve 0..1 e preto continua preto', () {
      for (final m in [0, 1, 2, 3]) {
        expect(tonemap(0, m), closeTo(0, 1e-6), reason: 'modo $m');
        for (final v in [0.5, 1.0, 4.0, 20.0]) {
          expect(tonemap(v, m), inInclusiveRange(0, 1),
              reason: 'modo $m valor $v');
        }
      }
    });

    test('mapeamento e monotono', () {
      for (final m in [0, 1, 2, 3]) {
        var anterior = -1.0;
        for (var i = 0; i <= 50; i++) {
          final v = tonemap(i / 10, m);
          expect(v, greaterThanOrEqualTo(anterior - 1e-9), reason: 'modo $m');
          anterior = v;
        }
      }
    });
  });

  group('Ficha do Deep Glow', () {
    test('tem os parametros do documento com os padroes certos', () {
      final p = effectSpecs[EffectType.glowVol]!.params;
      expect(p['exposure']!.initial, 1.0);
      expect(p['threshold']!.initial, 1.0);
      expect(p['threshold_softness']!.initial, 0.2);
      expect(p['downsample']!.initial, 2.0);
      expect(p['red_radius_multiplier']!.initial, 1.0);
      expect(p['glow_saturation']!.initial, 100.0);
      expect(p['aspect_ratio']!.initial, 1.0);
      expect(p['lens_dirt_amount']!.initial, 50.0);
      expect(p['noise_reduction']!.initial, 0.0);
      expect(p['glow_only']!.initial, 0.0);
    });

    test('os nomes antigos ainda resolvem', () {
      expect(resolveParamKey(EffectType.glowVol, 'raio'), 'radius');
      expect(resolveParamKey(EffectType.glowVol, 'intensidade'), 'exposure');
    });
  });

  group('Limiar como matriz de cor', () {
    // POR QUE ESTE GRUPO EXISTE: a conta antiga amplificava por vinte, e
    // com o limiar no maximo um meio-tom saturava em branco e brilhava —
    // exatamente o oposto do que um limiar alto promete. Deu para ver no
    // aparelho: um circulo marrom com halo branco.
    test('sem ganho, o branco continua branco em qualquer limiar', () {
      for (final t in [0.0, 0.3, 0.55, 0.9]) {
        expect(glowAfterThreshold(1.0, t, 0), closeTo(1.0, 1e-9),
            reason: 'limiar $t');
      }
    });

    test('abaixo do limiar nao sobra nada', () {
      expect(glowAfterThreshold(0.4, 0.55, 0), 0);
      expect(glowAfterThreshold(0.7, 1.0, 0), 0);
      expect(glowAfterThreshold(0.0, 0.0, 0), 0);
    });

    // O PADRAO DA FICHA: limiar 1,0 com uma parada de exposicao. E o do
    // plugin de referencia, e so faz sentido com o ganho entrando ANTES
    // do limiar — aplicado depois, nada jamais passa de 1 e o efeito
    // nasce invisivel, que foi o que apareceu no aparelho.
    test('no padrao da ficha, meio-tom brilha e sombra nao', () {
      const ganho = 2.0; // exposicao 1 = uma parada
      expect(glowAfterThreshold(0.7, 1.0, 0, ganho), greaterThan(0));
      expect(glowAfterThreshold(0.3, 1.0, 0, ganho), 0);
    });

    test('mais exposicao faz mais coisa passar do limiar', () {
      final pouca = glowAfterThreshold(0.6, 1.0, 0, 1.5);
      final muita = glowAfterThreshold(0.6, 1.0, 0, 3.0);
      expect(muita, greaterThan(pouca));
    });

    test('acima do limiar sobra a parte que passou', () {
      // Meio caminho entre 0,5 e 1 devolve meio.
      expect(glowAfterThreshold(0.75, 0.5, 0), closeTo(0.5, 0.01));
    });

    test('limiar maior deixa passar menos', () {
      final baixo = glowAfterThreshold(0.8, 0.3, 0);
      final alto = glowAfterThreshold(0.8, 0.6, 0);
      expect(alto, lessThan(baixo));
    });

    test('a suavidade adianta a rampa, e nunca estoura', () {
      final duro = glowAfterThreshold(0.6, 0.7, 0);
      final macio = glowAfterThreshold(0.6, 0.7, 1);
      expect(duro, 0);
      expect(macio, greaterThan(0));
      expect(macio, lessThanOrEqualTo(1));
    });

    test('sem ganho nunca amplifica: a saida nao passa a entrada', () {
      for (var l = 0.0; l <= 1.0; l += 0.05) {
        for (final t in [0.0, 0.2, 0.55, 0.9]) {
          expect(glowAfterThreshold(l, t, 0.2), lessThanOrEqualTo(l + 1e-9),
              reason: 'l=$l limiar=$t');
        }
      }
    });
  });
}
