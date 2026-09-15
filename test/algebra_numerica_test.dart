// A ALGEBRA que sustenta o rastreio e a cena 3D — portada do teste do
// solver antigo quando ele foi removido: as contas continuam no app
// (arrumar o mundo, definir o chao, os planos) e continuam provadas.
import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('Jacobi acha autovalores e autovetores de uma simetrica', () {
    // Diagonal conhecida girada: os autovalores tem de voltar.
    const a = [
      [4.0, 1.0, 0.0],
      [1.0, 3.0, 1.0],
      [0.0, 1.0, 2.0],
    ];
    final r = autovaloresSimetrica(a);
    expect(r.valores.length, 3);
    // Ordem crescente.
    expect(r.valores[0], lessThan(r.valores[1]));
    expect(r.valores[1], lessThan(r.valores[2]));
    // A soma dos autovalores e o traco.
    expect(r.valores.reduce((x, y) => x + y), closeTo(9, 1e-9));
    // Cada par satisfaz A v = lambda v.
    for (var i = 0; i < 3; i++) {
      final v = r.vetores[i];
      for (var linha = 0; linha < 3; linha++) {
        var s = 0.0;
        for (var col = 0; col < 3; col++) {
          s += a[linha][col] * v[col];
        }
        expect(s, closeTo(r.valores[i] * v[linha], 1e-8));
      }
    }
  });

  test('Jacobi nao trava quando dois autovalores sao iguais', () {
    // theta = 0 na formula da rotacao: o caso que fazia o laco rodar
    // sem mudar nada.
    const a = [
      [2.0, 1.0],
      [1.0, 2.0],
    ];
    final r = autovaloresSimetrica(a);
    expect(r.valores[0], closeTo(1, 1e-9));
    expect(r.valores[1], closeTo(3, 1e-9));
  });

  test('nucleo devolve o vetor que zera o sistema', () {
    // Sistema cujo nucleo e (1, -2, 1) normalizado.
    final a = [
      [1.0, 1.0, 1.0],
      [1.0, 0.0, -1.0],
      [2.0, 1.0, 0.0],
    ];
    final v = nucleo(a);
    expect(norma(v), closeTo(1, 1e-9));
    for (final linha in a) {
      expect(produtoInterno(linha, v).abs(), lessThan(1e-8));
    }
  });

  test('resolverSistema resolve e diz quando e singular', () {
    final x = resolverSistema(
      [
        [2.0, 1.0],
        [1.0, 3.0],
      ],
      [5, 10],
    );
    expect(x, isNotNull);
    expect(x![0], closeTo(1, 1e-9));
    expect(x[1], closeTo(3, 1e-9));
    expect(
      resolverSistema(
        [
          [1.0, 2.0],
          [2.0, 4.0],
        ],
        [1, 2],
      ),
      isNull,
    );
  });

  test('Rodrigues ida e volta', () {
    for (final w in [
      [0.3, -0.2, 0.9],
      [0.0, 0.0, 0.0],
      [0.0, 3.1, 0.0],
    ]) {
      final r = rotacaoDeVetor(w);
      expect(r.determinante, closeTo(1, 1e-9));
      final volta = vetorDeRotacao(r);
      final rVolta = rotacaoDeVetor(volta);
      for (var i = 0; i < 9; i++) {
        expect(rVolta.m[i], closeTo(r.m[i], 1e-7));
      }
    }
  });

  test('rotacaoMaisProxima limpa uma matriz suja', () {
    final r = rotacaoDeVetor([0.4, 0.1, -0.3]);
    final suja = Mat3([for (final v in r.m) v * 1.03 + 0.004]);
    final limpa = rotacaoMaisProxima(suja);
    expect(limpa.determinante, closeTo(1, 1e-9));
    final rrt = limpa * limpa.transposta;
    for (var i = 0; i < 3; i++) {
      for (var j = 0; j < 3; j++) {
        expect(rrt.at(i, j), closeTo(i == j ? 1 : 0, 1e-9));
      }
    }
  });
}
