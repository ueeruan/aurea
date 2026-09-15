import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:aurea/src/features/editor/domain/pontos_seguidos.dart';
import 'package:flutter_test/flutter_test.dart';

/// O RASTREIO DE CAMERA 3D, provado numa cena que se conhece de cor.
///
/// Um solver de camera nao da para testar "olhando": quando erra, o
/// sintoma e a cena escorregando no video, e ai ja e tarde. O jeito de
/// saber se funciona e o contrario do uso normal — MONTAR uma cena 3D
/// conhecida, projetar nos quadros, jogar so as projecoes no solver e
/// conferir se ele devolve o que se sabe que e a resposta.
///
/// O que da para exigir e o que nao da:
///   - a FORMA do caminho da camera tem de bater (as distancias entre as
///     posicoes, em proporcao);
///   - a ESCALA nao: duas fotos de uma maquete e de um predio sao
///     iguais. O solver devolve uma escala arbitraria de proposito.
void main() {
  group('algebra', () {
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
  });

  group('geometria de duas vistas', () {
    test('a essencial de um par conhecido devolve a pose certa', () {
      // Camera 2 andou para a direita e girou um pouco.
      final r = rotacaoDeVetor([0.05, 0.12, -0.02]);
      final t = [-0.9, 0.05, 0.1];

      final rng = math.Random(7);
      final mundo = [
        for (var i = 0; i < 60; i++)
          [
            (rng.nextDouble() - .5) * 4,
            (rng.nextDouble() - .5) * 3,
            4 + rng.nextDouble() * 4,
          ],
      ];
      final pares = <(List<double>, List<double>)>[];
      for (final x in mundo) {
        final a = [x[0] / x[2], x[1] / x[2], 1.0];
        final c = r.aplicar(x);
        final z = c[2] + t[2];
        pares.add((a, [(c[0] + t[0]) / z, (c[1] + t[1]) / z, 1.0]));
      }

      final e = essencialDePares(pares);
      expect(e, isNotNull);
      // Todo par tem de satisfazer x2^T E x1 = 0.
      for (final (a, b) in pares) {
        expect(produtoInterno(b, e!.aplicar(a)).abs(), lessThan(1e-6));
      }

      final escolha = escolherPorCheiralidade(
        e!,
        pares,
        [for (var i = 0; i < pares.length; i++) i],
      );
      expect(escolha, isNotNull);
      // A rotacao tem de bater; a translacao so na DIRECAO (a escala e
      // livre, e por isso se compara o vetor unitario).
      for (var i = 0; i < 9; i++) {
        expect(escolha!.r.m[i], closeTo(r.m[i], 1e-5));
      }
      final tn = normalizar(t);
      final ts = normalizar(escolha!.t);
      for (var i = 0; i < 3; i++) {
        expect(ts[i], closeTo(tn[i], 1e-5));
      }
    });

    test('triangular acha o ponto entre duas vistas', () {
      final r = rotacaoDeVetor([0.02, 0.2, 0.0]);
      final t = [-1.0, 0.0, 0.0];
      final x = [0.6, -0.4, 5.0];
      final a = [x[0] / x[2], x[1] / x[2], 1.0];
      final c = r.aplicar(x);
      final z = c[2] + t[2];
      final b = [(c[0] + t[0]) / z, (c[1] + t[1]) / z, 1.0];

      final achado = triangular([
        (Mat3.identidade, const [0.0, 0.0, 0.0], a),
        (r, t, b),
      ]);
      expect(achado, isNotNull);
      for (var i = 0; i < 3; i++) {
        expect(achado![i], closeTo(x[i], 1e-8));
      }
    });

    test('resolverPose recupera a pose de um quadro', () {
      final r = rotacaoDeVetor([0.1, -0.25, 0.05]);
      final t = [0.4, -0.2, 1.5];
      final rng = math.Random(3);
      final corresp = <(List<double>, List<double>)>[];
      for (var i = 0; i < 40; i++) {
        final x = [
          (rng.nextDouble() - .5) * 6,
          (rng.nextDouble() - .5) * 4,
          5 + rng.nextDouble() * 5,
        ];
        final p = projetar(r, t, x);
        if (p != null) corresp.add((x, p));
      }
      // Palpite errado de proposito: a ressecao tem de convergir a
      // partir de um vizinho, nao da resposta.
      final pose = resolverPose(
        rotacaoDeVetor([0.05, -0.1, 0.0]),
        [0.2, -0.1, 1.2],
        corresp,
      );
      expect(pose, isNotNull);
      expect(pose!.erro, lessThan(1e-6));
      for (var i = 0; i < 9; i++) {
        expect(pose.r.m[i], closeTo(r.m[i], 1e-5));
      }
      for (var i = 0; i < 3; i++) {
        expect(pose.t[i], closeTo(t[i], 1e-5));
      }
    });
  });

  group('solver completo', () {
    /// Uma cena de teste: pontos numa caixa e uma camera fazendo um
    /// travelling lateral com leve arco — o movimento tipico de quem
    /// filma andando de lado, e o que da paralaxe de verdade.
    ({List<PontoSeguido> pontos, List<List<double>> caminho}) cena({
      required int quadros,
      required double focal,
      required int largura,
      required int altura,
      double ruidoPx = 0,
    }) {
      final rng = math.Random(11);
      final mundo = [
        for (var i = 0; i < 90; i++)
          [
            (rng.nextDouble() - .5) * 900,
            (rng.nextDouble() - .5) * 500,
            900 + rng.nextDouble() * 900,
          ],
      ];
      final ruido = math.Random(29);
      final caminho = <List<double>>[];
      final obs = <int, Map<int, Offset>>{};

      for (var q = 0; q < quadros; q++) {
        final u = q / (quadros - 1);
        // A camera anda para a direita e vira um pouco para dentro.
        final pos = [-420 + 840 * u, 40 * math.sin(u * math.pi), -60 * u];
        final r = rotacaoDeVetor([0.03 * math.sin(u * 3), -0.22 * (u - .5), 0.0]);
        final rt = r.aplicar(pos);
        final t = [-rt[0], -rt[1], -rt[2]];
        caminho.add(pos);

        for (var i = 0; i < mundo.length; i++) {
          final p = projetar(r, t, mundo[i]);
          if (p == null) continue;
          final px = largura / 2 + p[0] * focal + (ruido.nextDouble() - .5) * ruidoPx;
          final py = altura / 2 + p[1] * focal + (ruido.nextDouble() - .5) * ruidoPx;
          if (px < 4 || py < 4 || px > largura - 4 || py > altura - 4) continue;
          (obs[i] ??= {})[q] = Offset(px, py);
        }
      }

      return (
        pontos: [
          for (final e in obs.entries)
            if (e.value.length >= 6)
              PontoSeguido(e.key, e.value.keys.reduce(math.min), e.value),
        ],
        caminho: caminho,
      );
    }

    /// A prova: as distancias entre as posicoes da camera, na solucao e
    /// na verdade, tem de estar todas na MESMA proporcao. Se estao, a
    /// solucao e a verdade a menos de escala — que e tudo que se pode
    /// pedir.
    double variacaoDaEscala(
      List<List<double>> achado,
      List<List<double>> verdade,
    ) {
      final razoes = <double>[];
      for (var i = 0; i < achado.length; i++) {
        for (var j = i + 1; j < achado.length; j++) {
          final da = norma([
            achado[i][0] - achado[j][0],
            achado[i][1] - achado[j][1],
            achado[i][2] - achado[j][2],
          ]);
          final dv = norma([
            verdade[i][0] - verdade[j][0],
            verdade[i][1] - verdade[j][1],
            verdade[i][2] - verdade[j][2],
          ]);
          if (dv > 1e-6) razoes.add(da / dv);
        }
      }
      final media = razoes.reduce((a, b) => a + b) / razoes.length;
      var pior = 0.0;
      for (final r in razoes) {
        pior = math.max(pior, (r - media).abs() / media);
      }
      return pior;
    }

    test('com a focal conhecida, recupera o caminho da camera', () {
      const largura = 240, altura = 135, quadros = 20;
      const focal = largura * 1.2;
      final c = cena(
        quadros: quadros,
        focal: focal,
        largura: largura,
        altura: altura,
      );
      expect(c.pontos.length, greaterThan(30));

      final s = resolverCamera3D(
        c.pontos,
        largura: largura,
        altura: altura,
        quadros: quadros,
        focalPx: focal,
      );

      expect(s.poses.length, quadros, reason: 'uma pose por quadro');
      expect(s.erroPixels, lessThan(0.2), reason: 'sem ruido, tem de fechar');
      expect(s.nuvem.length, greaterThan(30));

      final achado = [for (final p in s.poses) p.posicao];
      expect(
        variacaoDaEscala(achado, c.caminho),
        lessThan(0.02),
        reason: 'o caminho e o mesmo a menos de escala',
      );
    });

    test('aguenta ruido de rastreio de meio pixel', () {
      const largura = 240, altura = 135, quadros = 18;
      const focal = largura * 1.1;
      final c = cena(
        quadros: quadros,
        focal: focal,
        largura: largura,
        altura: altura,
        ruidoPx: 1.0,
      );
      final s = resolverCamera3D(
        c.pontos,
        largura: largura,
        altura: altura,
        quadros: quadros,
        focalPx: focal,
      );
      expect(s.erroPixels, lessThan(1.5));
      final achado = [for (final p in s.poses) p.posicao];
      expect(variacaoDaEscala(achado, c.caminho), lessThan(0.20));
    });

    test('descobre a distancia focal quando ela nao e informada', () {
      const largura = 240, altura = 135, quadros = 16;
      const focal = largura * 1.5;
      final c = cena(
        quadros: quadros,
        focal: focal,
        largura: largura,
        altura: altura,
      );
      final s = resolverCamera3D(
        c.pontos,
        largura: largura,
        altura: altura,
        quadros: quadros,
      );
      // A varredura anda de 0,2 em 0,2 da largura: acertar dentro de
      // 25% ja poe a lente na faixa certa.
      expect(s.focalPx / focal, closeTo(1, 0.25));
      expect(s.erroPixels, lessThan(2.0));
    });

    test('o mundo sai em pe e num tamanho utilizavel', () {
      const largura = 240, altura = 135, quadros = 14;
      final c = cena(
        quadros: quadros,
        focal: largura * 1.2,
        largura: largura,
        altura: altura,
      );
      final s = resolverCamera3D(
        c.pontos,
        largura: largura,
        altura: altura,
        quadros: quadros,
        focalPx: largura * 1.2,
      );
      // A nuvem cabe na ordem de grandeza do resto da cena 3D.
      final raios = [for (final v in s.nuvem.values) norma(v)];
      expect(mediana(raios), closeTo(350, 120));
      // E o "cima" das cameras aponta para +Y.
      for (final p in s.poses) {
        expect(p.cima[1], greaterThan(0.7));
      }
    });

    test('avisa quando nao ha paralaxe em vez de inventar uma cena', () {
      // Camera parada: so gira no lugar. Sem deslocamento nao ha
      // profundidade, e o solver tem de dizer isso.
      const largura = 240, altura = 135, quadros = 12;
      final rng = math.Random(5);
      final mundo = [
        for (var i = 0; i < 80; i++)
          [
            (rng.nextDouble() - .5) * 900,
            (rng.nextDouble() - .5) * 500,
            1000 + rng.nextDouble() * 400,
          ],
      ];
      final obs = <int, Map<int, Offset>>{};
      for (var q = 0; q < quadros; q++) {
        final r = rotacaoDeVetor([0, -0.02 * q, 0]);
        const t = [0.0, 0.0, 0.0];
        for (var i = 0; i < mundo.length; i++) {
          final p = projetar(r, t, mundo[i]);
          if (p == null) continue;
          final px = largura / 2 + p[0] * largura * 1.2;
          final py = altura / 2 + p[1] * largura * 1.2;
          if (px < 4 || py < 4 || px > largura - 4 || py > altura - 4) continue;
          (obs[i] ??= {})[q] = Offset(px, py);
        }
      }
      final pontos = [
        for (final e in obs.entries)
          if (e.value.length >= 6)
            PontoSeguido(e.key, e.value.keys.reduce(math.min), e.value),
      ];
      expect(
        () => resolverCamera3D(
          pontos,
          largura: largura,
          altura: altura,
          quadros: quadros,
        ),
        throwsA(isA<RastreioException>()),
      );
    });

    test('poucos pontos: recusa com explicacao, nao com erro cru', () {
      expect(
        () => resolverCamera3D(
          const [],
          largura: 240,
          altura: 135,
          quadros: 10,
        ),
        throwsA(
          isA<RastreioException>().having(
            (e) => e.falha,
            'falha',
            FalhaDoRastreio.poucosPontos,
          ),
        ),
      );
    });

    test('definirChao poe os pontos escolhidos no y = 0', () {
      const largura = 240, altura = 135, quadros = 14;
      final c = cena(
        quadros: quadros,
        focal: largura * 1.2,
        largura: largura,
        altura: altura,
      );
      final s = resolverCamera3D(
        c.pontos,
        largura: largura,
        altura: altura,
        quadros: quadros,
        focalPx: largura * 1.2,
      );
      // Cinco pontos quaisquer da nuvem viram o chao. Eles nao sao
      // coplanares de verdade (a nuvem e um volume), entao o que se pode
      // exigir e o que a conta promete: o plano MEDIO deles vira y = 0, e
      // a direcao y passa a ser a de menor espalhamento — que e o que faz
      // um texto deitado ficar deitado.
      final ids = s.nuvem.keys.take(5).toList();
      final chao = definirChao(s, ids);
      final ys = [for (final id in ids) chao.nuvem[id]![1]];
      expect(
        ys.reduce((a, b) => a + b) / ys.length,
        closeTo(0, 1e-9),
        reason: 'o centro dos pontos escolhidos vira a origem',
      );
      double espalhamento(int eixo) {
        final v = [for (final id in ids) chao.nuvem[id]![eixo]];
        final m = v.reduce((a, b) => a + b) / v.length;
        return math.sqrt(
          v.map((x) => (x - m) * (x - m)).reduce((a, b) => a + b) / v.length,
        );
      }
      expect(espalhamento(1), lessThan(espalhamento(0)));
      expect(espalhamento(1), lessThan(espalhamento(2)));
      // A camera continua vendo a mesma coisa: o erro nao muda.
      expect(chao.erroPixels, closeTo(s.erroPixels, 1e-9));
    });
  });
}
