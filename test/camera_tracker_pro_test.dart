import 'dart:convert';
import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:aurea/src/features/editor/domain/plano_do_rastreio.dart';
import 'package:aurea/src/features/editor/domain/pontos_seguidos.dart';
import 'package:flutter_test/flutter_test.dart';

/// AS QUINZE FILMAGENS, feitas em cima da mesa.
///
/// Um rastreador de câmera não se testa olhando: quando erra, o sintoma
/// é a cena escorregando no vídeo, e aí já é tarde. O jeito é o
/// contrário do uso normal — MONTAR uma cena 3D que se conhece de cor,
/// projetar nos quadros, jogar só as projeções no solver, e conferir se
/// ele devolve o que se sabe que é a resposta.
///
/// Cada teste aqui é uma situação real de filmagem. A metade que
/// importa mais não é a que resolve: é a que RECUSA. Um solver que
/// sempre devolve alguma coisa devolve mentira em tripé, em parede lisa
/// e em cena sem textura — e a mentira só aparece no dia da entrega.

enum Movimento { frente, lateral, orbita, mao, tripe, rapido }

/// Uma filmagem sintética: escolhe o movimento, a profundidade, o ruído
/// e a quantidade de pontos, e devolve o que o seguidor teria devolvido.
({List<PontoSeguido> pontos, List<List<double>> caminho}) filmagem({
  required Movimento movimento,
  int quadros = 24,
  int largura = 240,
  int altura = 135,
  double? focal,
  int pontosNoMundo = 120,
  double profundidade = 900,
  double ruidoPx = 0,

  /// Chance de um ponto sumir num quadro — é o que o borrão de
  /// movimento e a pouca luz fazem de verdade: o seguidor perde o
  /// ponto por alguns quadros e o reencontra depois.
  double sumico = 0,
  double espalhamentoNoPlano = 0,
  int semente = 11,
}) {
  final f = focal ?? largura * 1.2;
  final rng = math.Random(semente);
  final mundo = <List<double>>[
    for (var i = 0; i < pontosNoMundo; i++)
      espalhamentoNoPlano > 0
          // Uma parede: tudo praticamente na mesma distância.
          ? [
              (rng.nextDouble() - .5) * 1400,
              (rng.nextDouble() - .5) * 800,
              1100 + (rng.nextDouble() - .5) * espalhamentoNoPlano,
            ]
          : [
              (rng.nextDouble() - .5) * 900,
              (rng.nextDouble() - .5) * 500,
              300 + rng.nextDouble() * profundidade,
            ],
  ];

  final ruido = math.Random(semente * 3 + 1);
  final caminho = <List<double>>[];
  final obs = <int, Map<int, Offset>>{};

  for (var q = 0; q < quadros; q++) {
    final u = quadros == 1 ? 0.0 : q / (quadros - 1);
    late final List<double> pos;
    late final Mat3 r;
    switch (movimento) {
      case Movimento.frente:
        // Andando para dentro da cena. A paralaxe vem da profundidade:
        // o que está perto cresce depressa, o que está longe quase não
        // se mexe.
        pos = [0, 0, -700 + 900 * u];
        r = rotacaoDeVetor([0.02 * math.sin(u * 4), 0.02 * u, 0.0]);
      case Movimento.lateral:
        pos = [-420 + 840 * u, 30 * math.sin(u * math.pi), -60 * u];
        r = rotacaoDeVetor([0.03 * math.sin(u * 3), -0.22 * (u - .5), 0.0]);
      case Movimento.orbita:
        final a = (u - .5) * 0.9;
        pos = [900 * math.sin(a), 60 * math.sin(u * math.pi), 900 * math.cos(a) - 900];
        r = rotacaoDeVetor([0.0, -a, 0.0]);
      case Movimento.mao:
        // Câmera na mão: um caminho com tremor de várias frequências.
        pos = [
          -300 + 600 * u + 18 * math.sin(u * 21),
          14 * math.sin(u * 17) + 9 * math.cos(u * 31),
          -40 * u + 11 * math.sin(u * 13),
        ];
        r = rotacaoDeVetor([
          0.012 * math.sin(u * 19),
          -0.16 * (u - .5) + 0.010 * math.cos(u * 23),
          0.008 * math.sin(u * 11),
        ]);
      case Movimento.tripe:
        // O caso que precisa ser RECUSADO: gira e não anda.
        pos = [0, 0, 0];
        r = rotacaoDeVetor([0.05 * (u - .5), -0.55 * (u - .5), 0.0]);
      case Movimento.rapido:
        // Um chicote: quase todo o deslocamento em poucos quadros.
        final v = math.pow(u, 2.4).toDouble();
        pos = [-500 + 1000 * v, 0, -120 * v];
        r = rotacaoDeVetor([0.0, -0.5 * (v - .5), 0.0]);
    }

    final rt = r.aplicar(pos);
    final t = [-rt[0], -rt[1], -rt[2]];
    caminho.add(pos);

    for (var i = 0; i < mundo.length; i++) {
      if (sumico > 0 && ruido.nextDouble() < sumico) continue;
      final p = projetar(r, t, mundo[i]);
      if (p == null) continue;
      final px = largura / 2 + p[0] * f + (ruido.nextDouble() - .5) * ruidoPx;
      final py = altura / 2 + p[1] * f + (ruido.nextDouble() - .5) * ruidoPx;
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

/// A PROVA: as distâncias entre as posições da câmera, na solução e na
/// verdade, têm de estar todas na MESMA proporção. Se estão, a solução é
/// a verdade a menos de escala — que é tudo o que se pode pedir de uma
/// reconstrução a partir de imagens.
double variacaoDaEscala(SolucaoCamera3D s, List<List<double>> verdade) {
  final achado = [for (final p in s.poses) (p.quadro, p.posicao)];
  final razoes = <double>[];
  for (var i = 0; i < achado.length; i++) {
    for (var j = i + 1; j < achado.length; j++) {
      final (qa, a) = achado[i];
      final (qb, b) = achado[j];
      final da = norma([a[0] - b[0], a[1] - b[1], a[2] - b[2]]);
      final dv = norma([
        verdade[qa][0] - verdade[qb][0],
        verdade[qa][1] - verdade[qb][1],
        verdade[qa][2] - verdade[qb][2],
      ]);
      if (dv > 1e-6) razoes.add(da / dv);
    }
  }
  if (razoes.length < 3) return double.infinity;
  final ordenadas = [...razoes]..sort();
  final mediana = ordenadas[ordenadas.length ~/ 2];
  // A MEDIANA, e não a média: um par de quadros vizinhos tem distância
  // quase zero, e a razão dele estoura sem que a solução esteja errada.
  var pior = 0.0;
  final descartar = razoes.length ~/ 10;
  final desvios = [
    for (final r in razoes) (r - mediana).abs() / mediana,
  ]..sort();
  for (var i = 0; i < desvios.length - descartar; i++) {
    pior = math.max(pior, desvios[i]);
  }
  return pior;
}

SolucaoCamera3D resolver(
  ({List<PontoSeguido> pontos, List<List<double>> caminho}) c, {
  int largura = 240,
  int altura = 135,
  double? focal,
  TipoDeTomada tipo = TipoDeTomada.auto,
}) => resolverCamera3D(
  c.pontos,
  largura: largura,
  altura: altura,
  quadros: c.caminho.length,
  focalPx: focal,
  semente: 7,
  tipoDeTomada: tipo,
);

void main() {
  group('as quinze filmagens', () {
    test('1. camera andando para a frente', () {
      final c = filmagem(movimento: Movimento.frente);
      final s = resolver(c, focal: 240 * 1.2);
      expect(s.erroPixels, lessThan(1.0));
      expect(variacaoDaEscala(s, c.caminho), lessThan(0.12));
    });

    test('2. camera lateral', () {
      final c = filmagem(movimento: Movimento.lateral);
      final s = resolver(c, focal: 240 * 1.2);
      expect(s.erroPixels, lessThan(1.0));
      expect(variacaoDaEscala(s, c.caminho), lessThan(0.10));
    });

    test('3. orbita em volta do assunto', () {
      final c = filmagem(movimento: Movimento.orbita);
      final s = resolver(c, focal: 240 * 1.2);
      expect(s.erroPixels, lessThan(1.0));
      expect(variacaoDaEscala(s, c.caminho), lessThan(0.12));
    });

    test('4. camera na mao', () {
      final c = filmagem(movimento: Movimento.mao, ruidoPx: 0.4);
      final s = resolver(c, focal: 240 * 1.2);
      // O TREMOR NAO PODE SER ALISADO. Uma camera na mao treme, e o
      // objeto colado na cena tem de tremer junto — solucao suavizada
      // e objeto que desliza.
      expect(s.erroPixels, lessThan(1.6));
      expect(variacaoDaEscala(s, c.caminho), lessThan(0.20));
    });

    test('5. tripe: RECUSA, e diz por que', () {
      final c = filmagem(movimento: Movimento.tripe);
      expect(
        () => resolver(c, focal: 240 * 1.2),
        throwsA(
          isA<RastreioException>()
              .having((e) => e.falha, 'falha', FalhaDoRastreio.semParalaxe)
              .having((e) => e.mensagem, 'diz o que fazer', contains('andando')),
        ),
      );
    });

    test('6. cena predominantemente plana: RECUSA em vez de inventar', () {
      // Uma parede lisa filmada de lado: uma homografia explica tudo o
      // que os pontos fizeram. Ha movimento de camera, mas a
      // profundidade nao esta na imagem — e uma cena "resolvida" aqui
      // poria o objeto a qualquer distancia.
      final c = filmagem(
        movimento: Movimento.lateral,
        espalhamentoNoPlano: 4,
      );
      expect(
        () => resolver(c, focal: 240 * 1.2),
        throwsA(isA<RastreioException>()),
      );
    });

    test('7. cena com forte profundidade', () {
      final c = filmagem(movimento: Movimento.lateral, profundidade: 2600);
      final s = resolver(c, focal: 240 * 1.2);
      expect(s.erroPixels, lessThan(1.0));
      expect(s.nuvem.length, greaterThan(40));
      expect(variacaoDaEscala(s, c.caminho), lessThan(0.10));
    });

    test('8. poucas features: RECUSA, e explica o que falta', () {
      final c = filmagem(movimento: Movimento.lateral, pontosNoMundo: 9);
      expect(
        () => resolver(c, focal: 240 * 1.2),
        throwsA(
          isA<RastreioException>()
              .having((e) => e.falha, 'falha', FalhaDoRastreio.poucosPontos)
              .having((e) => e.mensagem, 'fala de textura', contains('textura')),
        ),
      );
    });

    test('9. movimento rapido', () {
      final c = filmagem(movimento: Movimento.rapido);
      final s = resolver(c, focal: 240 * 1.2);
      expect(s.erroPixels, lessThan(1.2));
      expect(variacaoDaEscala(s, c.caminho), lessThan(0.20));
    });

    test('10. borrao de movimento: pontos somem e voltam', () {
      // O borrao nao move o ponto: ele FAZ O SEGUIDOR PERDER o ponto por
      // alguns quadros, e devolve rastros picotados.
      final c = filmagem(
        movimento: Movimento.lateral,
        ruidoPx: 1.2,
        sumico: 0.28,
        quadros: 30,
      );
      final s = resolver(c, focal: 240 * 1.2);
      expect(s.erroPixels, lessThan(2.5));
      expect(variacaoDaEscala(s, c.caminho), lessThan(0.30));
    });

    test('11. pouca luz: muito ruido em cada ponto', () {
      final c = filmagem(movimento: Movimento.lateral, ruidoPx: 2.0);
      final s = resolver(c, focal: 240 * 1.2);
      // Ruido de dois pixels nao pode virar erro de dez: se virar, o
      // solver esta acomodando o ruido dentro da geometria.
      expect(s.erroPixels, lessThan(3.0));
      expect(variacaoDaEscala(s, c.caminho), lessThan(0.35));
    });

    test('12. video de iPhone (16:9, lente larga)', () {
      const l = 240, a = 135;
      final c = filmagem(
        movimento: Movimento.mao,
        largura: l,
        altura: a,
        focal: l * 1.05,
      );
      // SEM DIZER A FOCAL: e o caso real de um video importado da
      // galeria, que nao traz a lente escrita em lugar nenhum.
      final s = resolver(c, largura: l, altura: a);
      expect(s.erroPixels, lessThan(2.0));
      expect(s.focalPx, closeTo(l * 1.05, l * 0.35));
    });

    test('13. video de Android (proporcao e lente diferentes)', () {
      const l = 256, a = 144;
      final c = filmagem(
        movimento: Movimento.lateral,
        largura: l,
        altura: a,
        focal: l * 1.45,
      );
      final s = resolver(c, largura: l, altura: a);
      expect(s.erroPixels, lessThan(2.0));
      expect(s.focalPx, closeTo(l * 1.45, l * 0.45));
    });

    test('14. video comprimido: o ponto anda em degraus', () {
      // A compressao nao borra por igual: ela QUANTIZA. O ponto fica
      // preso a meio pixel e pula quando o bloco muda.
      final base = filmagem(movimento: Movimento.lateral, quadros: 26);
      final pontos = [
        for (final p in base.pontos)
          PontoSeguido(p.id, p.primeiroQuadro, {
            for (final e in p.observacoes.entries)
              e.key: Offset(
                (e.value.dx * 2).roundToDouble() / 2,
                (e.value.dy * 2).roundToDouble() / 2,
              ),
          }),
      ];
      final s = resolverCamera3D(
        pontos,
        largura: 240,
        altura: 135,
        quadros: base.caminho.length,
        focalPx: 240 * 1.2,
        semente: 7,
      );
      expect(s.erroPixels, lessThan(1.5));
      expect(variacaoDaEscala(s, base.caminho), lessThan(0.20));
    });

    test('15. video longo: cem quadros', () {
      final c = filmagem(movimento: Movimento.lateral, quadros: 100);
      final s = resolver(c, focal: 240 * 1.2);
      expect(s.poses.length, greaterThan(80));
      expect(s.erroPixels, lessThan(1.2));
      expect(variacaoDaEscala(s, c.caminho), lessThan(0.12));
    });
  });

  group('a leitura da cena, antes de resolver', () {
    test('reconhece o tripe', () {
      final c = filmagem(movimento: Movimento.tripe);
      expect(
        lerCena(c.pontos, largura: 240, quadros: c.caminho.length),
        LeituraDaCena.tripeOuGiro,
      );
    });

    test('reconhece a parede', () {
      final c = filmagem(
        movimento: Movimento.lateral,
        espalhamentoNoPlano: 4,
      );
      expect(
        lerCena(c.pontos, largura: 240, quadros: c.caminho.length),
        anyOf(LeituraDaCena.tripeOuGiro, LeituraDaCena.quaseChata),
      );
    });

    test('reconhece a cena com profundidade', () {
      final c = filmagem(movimento: Movimento.lateral, profundidade: 2600);
      expect(
        lerCena(c.pontos, largura: 240, quadros: c.caminho.length),
        LeituraDaCena.profundidadeBoa,
      );
    });

    test('quem diz que filmou no tripe nao espera pela conta', () {
      // Nao ha o que resolver, e dizer isso na hora e melhor do que
      // gastar segundos para chegar na mesma resposta.
      final c = filmagem(movimento: Movimento.lateral, profundidade: 2600);
      expect(
        () => resolver(c, focal: 240 * 1.2, tipo: TipoDeTomada.tripe),
        throwsA(
          isA<RastreioException>()
              .having((e) => e.falha, 'falha', FalhaDoRastreio.semParalaxe),
        ),
      );
    });
  });

  group('a ficha do solve', () {
    late SolucaoCamera3D s;
    setUpAll(() {
      final c = filmagem(movimento: Movimento.lateral, profundidade: 2600);
      s = resolver(c, focal: 240 * 1.2);
    });

    test('cada ponto tem erro, permanencia e qualidade', () {
      expect(s.errosPorPonto.length, s.nuvem.length);
      expect(s.vistasPorPonto.length, s.nuvem.length);
      expect(s.pontosSeguidos, greaterThanOrEqualTo(s.nuvem.length));
      for (final id in s.nuvem.keys) {
        expect(s.errosPorPonto[id], isNotNull);
        expect(s.qualidadeDoPonto(id), isNotNull);
      }
      expect(s.pontosBons, greaterThan(0));
    });

    test('as estrelas nao passam do que a quantidade de pontos sustenta', () {
      // Erro baixissimo com poucos pontos nao pode dar cinco estrelas: a
      // cena e fragil, e uma ficha otimista faria a pessoa confiar nela.
      final poucos = s.copiarCom(
        nuvem: {for (final e in s.nuvem.entries.take(10)) e.key: e.value},
        erroPixels: 0.1,
      );
      expect(poucos.estrelas, lessThanOrEqualTo(2));
    });

    test('um ponto visto em poucos quadros nao passa de fraco', () {
      final id = s.nuvem.keys.first;
      final apertado = s.copiarCom(
        errosPorPonto: {...s.errosPorPonto, id: 0.2},
        vistasPorPonto: {...s.vistasPorPonto, id: 2},
      );
      expect(apertado.qualidadeDoPonto(id), QualidadeDoPonto.fraco);
    });

    test('apagar os ruins melhora o erro medio', () {
      final ruins = s.pontosDaQualidade({
        QualidadeDoPonto.ruim,
        QualidadeDoPonto.fraco,
      });
      final limpa = semPontos(s, ruins.toSet());
      expect(limpa.nuvem.length, s.nuvem.length - ruins.length);
      if (ruins.isNotEmpty) {
        expect(limpa.erroPixels, lessThanOrEqualTo(s.erroPixels + 1e-9));
      }
      // A camera nao se mexe: apagar pontos e uma operacao reversivel.
      expect(limpa.poses.length, s.poses.length);
    });

    test('a solucao atravessa a gravacao com a ficha inteira', () {
      final voltou = SolucaoCamera3D.decode(jsonEncode(s.toJson()))!;
      expect(voltou.nuvem.length, s.nuvem.length);
      expect(voltou.errosPorPonto.length, s.errosPorPonto.length);
      expect(voltou.pontosSeguidos, s.pontosSeguidos);
      expect(voltou.estrelas, s.estrelas);
    });
  });

  group('o plano, que e onde o objeto entra', () {
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
  });
}
