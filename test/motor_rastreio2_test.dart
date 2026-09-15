// O MOTOR 2.0 (C++, packages/aurea_tracker2) contra filmagens sinteticas
// com a camera CONHECIDA — montar a cena, projetar, entregar so as
// projecoes e conferir a resposta. E o unico jeito honesto de testar um
// solver: quando ele erra no mundo real, o sintoma e a cena escorregando,
// e ai ja e tarde.
//
// O que se exige e o que da para exigir: a FORMA do caminho (distancias em
// proporcao — a escala absoluta nao existe numa camera so), a focal, a
// distorcao, e — tao importante quanto — os CODIGOS DE ERRO: filmagem que
// nao sustenta cena tem de virar recusa nomeada, nunca cena inventada.
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea_tracker2/aurea_tracker2.dart';
import 'package:flutter_test/flutter_test.dart';

enum _Mov { lateral, orbita, mao, frente, tripe }

class _Filmagem {
  _Filmagem(this.obs, this.centros, this.rotacoes);
  final Float64List obs;
  final List<List<double>> centros;
  final List<Mat3> rotacoes;
}

_Filmagem _filmar(
  _Mov mov, {
  int quadros = 60,
  int largura = 640,
  int altura = 360,
  double focal = 760,
  double ruido = 0,
  double k1 = 0,
  bool plano = false,
  int semente = 7,
}) {
  final rng = math.Random(semente);
  final mundo = [
    for (var i = 0; i < 260; i++)
      plano
          // O CHAO: tudo em y = 150 (abaixo da camera), espalhado em x/z.
          ? [(rng.nextDouble() - .5) * 1600, 150.0, 350 + rng.nextDouble() * 1400]
          : [
              (rng.nextDouble() - .5) * 1100,
              (rng.nextDouble() - .5) * 600,
              350 + rng.nextDouble() * 1100,
            ],
  ];
  final ruidoRng = math.Random(semente * 5 + 1);
  final obs = <double>[];
  final centros = <List<double>>[];
  final rotacoes = <Mat3>[];
  for (var q = 0; q < quadros; q++) {
    final u = q / (quadros - 1);
    late List<double> c;
    late Mat3 r;
    switch (mov) {
      case _Mov.lateral:
        c = [-260 + 520 * u, 10 * math.sin(u * 3), 0];
        r = rotacaoDeVetor([0.0, -0.18 * (u - .5), 0.0]);
      case _Mov.orbita:
        final a = (u - .5) * 0.7;
        c = [700 * math.sin(a), 0, 700 * (1 - math.cos(a))];
        r = rotacaoDeVetor([0.0, -a, 0.0]);
      case _Mov.mao:
        c = [
          -200 + 400 * u + 12 * math.sin(u * 23),
          9 * math.sin(u * 17),
          7 * math.sin(u * 13),
        ];
        r = rotacaoDeVetor([
          0.01 * math.sin(u * 19),
          -0.12 * (u - .5),
          0.006 * math.sin(u * 11),
        ]);
      case _Mov.frente:
        c = [30 * math.sin(u * 2), 0, -150 + 450 * u];
        r = rotacaoDeVetor([0.0, 0.05 * u, 0.0]);
      case _Mov.tripe:
        c = [0, 0, 0];
        r = rotacaoDeVetor([0.04 * (u - .5), -0.5 * (u - .5), 0.0]);
    }
    final rc = r.aplicar(c);
    final t = [-rc[0], -rc[1], -rc[2]];
    centros.add(c);
    rotacoes.add(r);
    for (var i = 0; i < mundo.length; i++) {
      final p = projetarComDistorcao(r, t, mundo[i], k1);
      if (p == null) continue;
      final x = largura / 2 + p[0] * focal + (ruidoRng.nextDouble() - .5) * ruido;
      final y = altura / 2 + p[1] * focal + (ruidoRng.nextDouble() - .5) * ruido;
      if (x < 6 || y < 6 || x > largura - 6 || y > altura - 6) continue;
      obs.addAll([i.toDouble(), q.toDouble(), x, y]);
    }
  }
  return _Filmagem(Float64List.fromList(obs), centros, rotacoes);
}

/// A projecao com a MESMA distorcao radial do motor: p * (1 + k1 * r^2)
/// em coordenadas normalizadas.
List<double>? projetarComDistorcao(
  Mat3 r,
  List<double> t,
  List<double> x,
  double k1,
) {
  final c = r.aplicar(x);
  final z = c[2] + t[2];
  if (z < 1e-6) return null;
  final nx = (c[0] + t[0]) / z, ny = (c[1] + t[1]) / z;
  final d = 1 + k1 * (nx * nx + ny * ny);
  return [nx * d, ny * d];
}

List<List<double>> _centrosDe(Float64List poses) {
  final out = <List<double>>[];
  for (var i = 0; i + 13 <= poses.length; i += 13) {
    final r = Mat3([for (var k = 1; k <= 9; k++) poses[i + k]]);
    final t = [poses[i + 10], poses[i + 11], poses[i + 12]];
    final rt = r.transposta.aplicar(t);
    out.add([-rt[0], -rt[1], -rt[2]]);
  }
  return out;
}

double _dist(List<double> a, List<double> b) => math.sqrt(
  math.pow(a[0] - b[0], 2) + math.pow(a[1] - b[1], 2) + math.pow(a[2] - b[2], 2),
);

/// Quanto o caminho resolvido difere do verdadeiro em FORMA (a escala do
/// mundo e livre): razao das distancias ao primeiro centro.
double _erroDeForma(List<List<double>> verdade, Float64List poses) {
  final centros = _centrosDe(poses);
  final quadrosDe = [
    for (var i = 0; i + 13 <= poses.length; i += 13) poses[i].round(),
  ];
  final escalaV = _dist(verdade.first, verdade.last);
  final escalaS = _dist(centros.first, centros.last);
  var pior = 0.0;
  for (var i = 1; i < centros.length; i++) {
    final dv = _dist(verdade.first, verdade[quadrosDe[i]]) / escalaV;
    final ds = _dist(centros.first, centros[i]) / escalaS;
    pior = math.max(pior, (dv - ds).abs());
  }
  return pior;
}

ResultadoDoMotor2 _resolver(_Filmagem f, {double focal = 0, bool tripe = false}) =>
    resolverCenaNativa(
      observacoes: f.obs,
      largura: 640,
      altura: 360,
      quadros: 60,
      fps: 30,
      focal: focal,
      tripe: tripe,
    );

void main() {
  test('o motor carrega e diz quem e', () {
    expect(versaoDoMotorNativo(), '2.0.0');
  });

  for (final mov in [_Mov.lateral, _Mov.orbita, _Mov.mao, _Mov.frente]) {
    test('resolve ${mov.name}: focal, todos os quadros e a forma do caminho', () {
      final f = _filmar(mov, ruido: .4);
      final r = _resolver(f);
      expect(r.codigo, at2Ok);
      expect(r.tripe, isFalse);
      expect(r.poses.length ~/ 13, 60, reason: 'pose de todo quadro');
      expect(r.erro, lessThan(1.0));
      expect(r.focal, closeTo(760, 760 * .08), reason: 'focal livre no ajuste');
      expect(_erroDeForma(f.centros, r.poses), lessThan(.06));
      expect(r.pontos.length ~/ 6, greaterThan(80), reason: 'nuvem povoada');
    });
  }

  test('chao plano resolve (a cena quase-degenerada classica)', () {
    final f = _filmar(_Mov.lateral, plano: true, ruido: .3);
    final r = _resolver(f);
    expect(r.codigo, at2Ok);
    expect(r.poses.length ~/ 13, 60);
    expect(r.erro, lessThan(1.2));
  });

  test('a distorcao radial da lente e medida, nao ignorada', () {
    // Lente larga de celular: k1 negativo de verdade. O motor 1 assumia
    // pinhole e espalhava esse erro pela cena inteira.
    final f = _filmar(_Mov.mao, ruido: .3, k1: -0.08);
    final r = _resolver(f);
    expect(r.codigo, at2Ok);
    expect(r.erro, lessThan(1.2));
    expect(r.focal, closeTo(760, 760 * .08));
    expect(r.distorcao, closeTo(-0.08, 0.04), reason: 'k1 recuperado');
    expect(_erroDeForma(f.centros, r.poses), lessThan(.06));
  });

  test('tripe vira rotacao pura em vez de recusa', () {
    final f = _filmar(_Mov.tripe, ruido: .3);
    final r = _resolver(f);
    expect(r.codigo, at2Ok);
    expect(r.tripe, isTrue);
    expect(r.focal, closeTo(760, 760 * .12));
    // Giro total entre o primeiro e o ultimo quadro: 0,5 rad em Y.
    final r0 = Mat3([for (var k = 1; k <= 9; k++) r.poses[k]]);
    final ultimo = r.poses.length - 13;
    final r1 = Mat3([for (var k = 1; k <= 9; k++) r.poses[ultimo + k]]);
    final rel = r1 * r0.transposta;
    final ang = math.acos(
      ((rel.at(0, 0) + rel.at(1, 1) + rel.at(2, 2) - 1) / 2).clamp(-1.0, 1.0),
    );
    expect(ang, closeTo(.5, .05));
  });

  test('quem pede tripe de proposito tambem recebe rotacao pura', () {
    final f = _filmar(_Mov.tripe, ruido: .3);
    final r = _resolver(f, tripe: true);
    expect(r.codigo, at2Ok);
    expect(r.tripe, isTrue);
  });

  test('poucos quadros e um ERRO NOMEADO, nunca uma cena de um segundo', () {
    // O defeito de campo que motivou o motor 2: uma analise que "resolve"
    // em um segundo com meia duzia de quadros e mentira. Aqui ela tem de
    // ser recusada com codigo proprio.
    final f = _filmar(_Mov.lateral, quadros: 8);
    final r = resolverCenaNativa(
      observacoes: f.obs,
      largura: 640,
      altura: 360,
      quadros: 8,
      fps: 30,
    );
    expect(r.codigo, at2ErrPoucosQuadros);
    expect(r.poses, isEmpty, reason: 'recusa nao carrega cena');
  });

  test('sem pontos que prestem, o erro diz isso', () {
    final r = resolverCenaNativa(
      observacoes: Float64List.fromList([
        // Tres trilhas curtas: nada para sustentar uma cena.
        for (var q = 0; q < 60; q += 20) ...[1, q.toDouble(), 100, 100],
        for (var q = 0; q < 60; q += 20) ...[2, q.toDouble(), 200, 150],
        for (var q = 0; q < 60; q += 20) ...[3, q.toDouble(), 300, 200],
      ]),
      largura: 640,
      altura: 360,
      quadros: 60,
      fps: 30,
    );
    expect(r.codigo, at2ErrPoucosPontos);
  });

  test('a mesma filmagem da a mesma resposta, sempre', () {
    // Sem determinismo, o objeto colado no chao mudaria de lugar entre
    // duas analises do mesmo clipe.
    final f = _filmar(_Mov.mao, ruido: .4);
    final a = _resolver(f);
    final b = _resolver(f);
    expect(a.focal, b.focal);
    expect(a.erro, b.erro);
    expect(a.poses, b.poses);
  });

  test('o seguidor LK acompanha uma textura andando com subpixel', () {
    const w = 320, h = 180;
    final rng = math.Random(3);
    final base = List<double>.generate(w * h, (_) => rng.nextDouble());
    // Textura suave: media 5x5 do ruido.
    double tex(double x, double y) {
      final xi = x.floor(), yi = y.floor();
      final ax = x - xi, ay = y - yi;
      double v(int a, int b) {
        var s = 0.0;
        for (var dy = -2; dy <= 2; dy++) {
          for (var dx = -2; dx <= 2; dx++) {
            final xx = (a + dx) % w, yy = (b + dy) % h;
            s += base[(yy < 0 ? yy + h : yy) * w + (xx < 0 ? xx + w : xx)];
          }
        }
        return s / 25;
      }

      return (1 - ay) * ((1 - ax) * v(xi, yi) + ax * v(xi + 1, yi)) +
          ay * ((1 - ax) * v(xi, yi + 1) + ax * v(xi + 1, yi + 1));
    }

    final seguidor = SeguidorDePontos2(w, h, maximoDePontos: 120);
    addTearDown(seguidor.fechar);
    for (var q = 0; q < 10; q++) {
      final quadro = Uint8List(w * h);
      for (var y = 0; y < h; y++) {
        for (var x = 0; x < w; x++) {
          quadro[y * w + x] =
              (tex(x - 1.3 * q, y - .7 * q) * 255).round().clamp(0, 255);
        }
      }
      seguidor.empurrar(quadro, q);
    }
    final obs = seguidor.observacoes();
    final porId = <int, List<(int, double, double)>>{};
    for (var i = 0; i < obs.length; i += 4) {
      (porId[obs[i].round()] ??= []).add((obs[i + 1].round(), obs[i + 2], obs[i + 3]));
    }
    final longos = porId.values.where((v) => v.length == 10).toList();
    expect(longos.length, greaterThan(30));
    var erro = 0.0;
    for (final v in longos) {
      final dx = v.last.$2 - v.first.$2, dy = v.last.$3 - v.first.$3;
      erro += (dx - 1.3 * 9).abs() + (dy - .7 * 9).abs();
    }
    expect(erro / longos.length, lessThan(.25), reason: 'subpixel preservado');
  });

  test('a trava de deriva mata o ponto coberto em vez de deixa-lo vagar', () {
    // Uma textura parada e um "cartaz" que passa por cima da metade
    // esquerda no meio do clipe. Os pontos cobertos tem de MORRER (a
    // trava de NCC contra o molde de nascimento) — o fantasma classico e
    // exatamente o ponto que adota o cartaz e sai andando com ele.
    const w = 320, h = 180;
    final rng = math.Random(5);
    final base = List<int>.generate(
      w * h,
      (_) => 60 + rng.nextInt(160),
    );
    Uint8List quadro(int q) {
      final px = Uint8List.fromList(base);
      if (q >= 5) {
        // O cartaz: um bloco liso que cobre a esquerda.
        for (var y = 0; y < h; y++) {
          for (var x = 0; x < w ~/ 2; x++) {
            px[y * w + x] = 230;
          }
        }
      }
      return px;
    }

    final seguidor = SeguidorDePontos2(w, h, maximoDePontos: 120);
    addTearDown(seguidor.fechar);
    for (var q = 0; q < 10; q++) {
      seguidor.empurrar(quadro(q), q);
    }
    final obs = seguidor.observacoes();
    // Nenhuma observacao na metade coberta depois da chegada do cartaz.
    var fantasmas = 0;
    for (var i = 0; i < obs.length; i += 4) {
      final q = obs[i + 1].round();
      final x = obs[i + 2];
      // Margem folgada da borda do cartaz: deriva de verdade iria longe.
      if (q >= 6 && x < w / 2 - 20) fantasmas++;
    }
    expect(fantasmas, 0, reason: 'ponto coberto morre, nao vira fantasma');
  });
}
