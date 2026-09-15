// O PLANO, que e onde o objeto entra — portado do teste do solver antigo
// quando ele foi removido: os planos sao calculados sobre a NUVEM (que
// qualquer motor entrega) e nao dependem de quem resolveu a camera.
import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/plano_do_rastreio.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('acha o chao de uma nuvem espalhada num plano', () {
    // Um chao de verdade: pontos num plano y = -100, com um pouco de
    // relevo, e uma camera acima dele.
    final rng = math.Random(5);
    final nuvem = <int, List<double>>{
      for (var i = 0; i < 60; i++)
        i: [
          (rng.nextDouble() - .5) * 600,
          -100 + (rng.nextDouble() - .5) * 6,
          (rng.nextDouble() - .5) * 600,
        ],
    };
    final plano = planoDosPontos(
      nuvem,
      nuvem.keys.toList(),
      ladoDeFora: [0, 400, 0],
    )!;
    expect(plano.ehSuperficie, isTrue);
    expect(plano.tipo, TipoDeSuperficie.chao);
    expect(plano.normal[1], greaterThan(0.9));
    expect(plano.origem[1], closeTo(-100, 3));
    // Os tres eixos tem de formar um trio ortonormal, senao o objeto
    // colado entra torto ou esticado.
    expect(produtoInterno(plano.eixoX, plano.normal).abs(), lessThan(1e-6));
    expect(produtoInterno(plano.eixoX, plano.eixoZ).abs(), lessThan(1e-6));
    expect(norma(plano.eixoZ), closeTo(1, 1e-9));
  });

  test('a parede vira parede, e nao chao', () {
    final rng = math.Random(9);
    final nuvem = <int, List<double>>{
      for (var i = 0; i < 60; i++)
        i: [
          (rng.nextDouble() - .5) * 600,
          (rng.nextDouble() - .5) * 400,
          500 + (rng.nextDouble() - .5) * 5,
        ],
    };
    final plano = planoDosPontos(
      nuvem,
      nuvem.keys.toList(),
      ladoDeFora: [0, 0, -400],
    )!;
    expect(plano.tipo, TipoDeSuperficie.parede);
    expect(plano.normal[2], lessThan(-0.9));
  });

  test('menos de tres pontos nao definem plano nenhum', () {
    expect(planoDosPontos({0: [0, 0, 0], 1: [1, 0, 0]}, [0, 1]), isNull);
  });

  test('uma nuvem sem superficie nao e uma superficie', () {
    final rng = math.Random(3);
    final nuvem = <int, List<double>>{
      for (var i = 0; i < 80; i++)
        i: [
          (rng.nextDouble() - .5) * 400,
          (rng.nextDouble() - .5) * 400,
          (rng.nextDouble() - .5) * 400,
        ],
    };
    final plano = planoDosPontos(nuvem, nuvem.keys.toList())!;
    expect(plano.ehSuperficie, isFalse);
  });

  test('o RANSAC acha o chao no meio do resto da cena', () {
    final rng = math.Random(17);
    final nuvem = <int, List<double>>{};
    // Metade num chao, metade solta pelo ar: e a nuvem tipica de um
    // rastreio real, em que o chao e so uma parte do que aparece.
    for (var i = 0; i < 70; i++) {
      nuvem[i] = [
        (rng.nextDouble() - .5) * 600,
        -120 + (rng.nextDouble() - .5) * 4,
        (rng.nextDouble() - .5) * 600,
      ];
    }
    for (var i = 70; i < 130; i++) {
      nuvem[i] = [
        (rng.nextDouble() - .5) * 600,
        (rng.nextDouble()) * 500,
        (rng.nextDouble() - .5) * 600,
      ];
    }
    final plano = maiorPlano(nuvem)!;
    expect(plano.ids.length, greaterThan(50));
    expect(plano.origem[1], closeTo(-120, 40));
    expect(plano.normal[1].abs(), greaterThan(0.9));
  });

  test('o mesmo vidro da o mesmo plano, sempre', () {
    // Semente fixa: sem isso, o objeto colado no plano mudaria de
    // lugar entre duas aberturas do projeto.
    final rng = math.Random(23);
    final nuvem = <int, List<double>>{
      for (var i = 0; i < 90; i++)
        i: [
          (rng.nextDouble() - .5) * 500,
          -40 + (rng.nextDouble() - .5) * 8,
          (rng.nextDouble() - .5) * 500,
        ],
    };
    final a = maiorPlano(nuvem)!;
    final b = maiorPlano(nuvem)!;
    expect(a.ids.length, b.ids.length);
    expect(a.origem[0], closeTo(b.origem[0], 1e-9));
    expect(a.origem[1], closeTo(b.origem[1], 1e-9));
  });

  test('o chao deitado vira rotacao zero em X e Z', () {
    final rng = math.Random(31);
    final nuvem = <int, List<double>>{
      for (var i = 0; i < 40; i++)
        i: [
          (rng.nextDouble() - .5) * 400,
          0,
          (rng.nextDouble() - .5) * 400,
        ],
    };
    final plano = planoDosPontos(
      nuvem,
      nuvem.keys.toList(),
      ladoDeFora: [0, 500, 0],
    )!;
    final (rx, _, rz) = plano.anglesEmGraus;
    // Um objeto posto num chao horizontal nao pode nascer tombado.
    expect(rx.abs(), lessThan(1.0));
    expect(rz.abs(), lessThan(1.0));
  });
}
